/*
** GigaNES (c) 2026 Kaled Souky <https://github.com/KaledSouky> 
**
**
** GigaNES: High-Performance NES Emulator for Arduino Giga
**
** GigaNES is a specialized port of the Nofrendo NES emulator core, 
** custom-engineered for the Arduino Giga R1 WiFi (STM32H747XI) and the Arduino
** Giga Display Shield. This project leverages the high-speed Cortex-M7 core and 
** low-level STM32 register access to deliver a full-speed, low-latency gaming 
** experience.
**
**
** This program is free software: you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation, either version 3 of the License, or (at your option) any later
** version.
**
** This program is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along with
** this program. If not, see <https://www.gnu.org/licenses/>.
**
**
** nes_rom.cpp  
**
** NES ROM Arduino SD Version
** Adapted for GigaNES
*/

#include <Arduino.h>
#include <rtos.h>
#include <SdFat.h>
#include "SDRAM.h"
#include "hw_config.h"

#ifndef FILE_READ
#define FILE_READ O_RDONLY
#endif
#ifndef FILE_WRITE
#define FILE_WRITE (O_RDWR | O_CREAT | O_AT_END)
#endif

extern SdFs sd;

extern "C" {
#include "noftypes.h"
#include "nes_rom.h"
#include "intro.h"
#include "nes_mmc.h"
#include "nes_ppu.h"
#include "nes.h"
#include "gui.h"
#include "log.h"
#include "osd.h"

int sram_dirty = 0;
uint32 sram_last_write = 0;
}

/* Max length for displayed filename */
#define ROM_DISP_MAXLEN 20

#define ROM_FOURSCREEN 0x08
#define ROM_TRAINER 0x04
#define ROM_BATTERY 0x02
#define ROM_MIRRORTYPE 0x01
#define ROM_INES_MAGIC "NES\x1A"

typedef struct inesheader_s
{
   uint8 ines_magic[4];
   uint8 rom_banks;
   uint8 vrom_banks;
   uint8 rom_type;
   uint8 mapper_hinybble;
   uint8 reserved[8];
} __attribute__((packed)) inesheader_t;

#define TRAINER_OFFSET 0x1000
#define TRAINER_LENGTH 0x200
#define VRAM_LENGTH 0x2000

#define ROM_BANK_LENGTH 0x4000
#define VROM_BANK_LENGTH 0x2000

#define SRAM_BANK_LENGTH 0x0400
#define VRAM_BANK_LENGTH 0x2000

#define ASYNC_SRAM_SIZE 0x2000
static rtos::Thread saveThread(osPriorityBelowNormal, 2048);
static rtos::EventFlags saveEvents;
#define SAVE_EVENT_BIT 0x01

static uint8_t shadow_sram[ASYNC_SRAM_SIZE];
static char shadow_filename[PATH_MAX + 1];
static int shadow_size = 0;
static bool saveThreadStarted = false;

void saveThreadFunc() {
   while (true) {
      saveEvents.wait_any(SAVE_EVENT_BIT);
      
      File fp = sd.open(shadow_filename, O_RDWR | O_CREAT);
      if (fp) {
         fp.seek(0);
         fp.write(shadow_sram, shadow_size);
         fp.close();
      }
      rtos::ThisThread::sleep_for(std::chrono::milliseconds(100));
   }
}

extern "C" void rom_savesram_async(rominfo_t *rominfo) {
   if (rominfo == NULL || !(rominfo->flags & ROM_FLAG_BATTERY) || rominfo->sram == NULL) return;

   if (!saveThreadStarted) {
      saveThread.start(saveThreadFunc);
      saveThreadStarted = true;
   }

   int size = SRAM_BANK_LENGTH * rominfo->sram_banks;
   if (size > ASYNC_SRAM_SIZE) size = ASYNC_SRAM_SIZE;
   
   memcpy(shadow_sram, rominfo->sram, size);
   shadow_size = size;
   
   strncpy(shadow_filename, rominfo->filename, PATH_MAX);
   osd_newextension(shadow_filename, ".sav");
   
   sram_dirty = 0; 
   saveEvents.set(SAVE_EVENT_BIT);
}

/* Save battery-backed RAM */
extern "C" void rom_savesram(rominfo_t *rominfo)
{
   File fp;
   char fn[PATH_MAX + 1];

   ASSERT(rominfo);

   if (rominfo->flags & ROM_FLAG_BATTERY)
   {
      strncpy(fn, rominfo->filename, PATH_MAX);
      osd_newextension(fn, ".sav");

      // In Arduino SD, usually paths start with /
      // But we assume fn comes correct or relative
      fp = sd.open(fn, O_RDWR | O_CREAT); // Explicit flags to avoid append-only issues
      if (fp)
      {
         fp.seek(0); 
         
         fp.write((const uint8_t*)rominfo->sram, SRAM_BANK_LENGTH * rominfo->sram_banks);
         fp.close();
         sram_dirty = 0; // Reset dirty flag
         nofrendo_log_printf("Wrote battery RAM to %s.\n", fn);
      }
   }
}

/* Load battery-backed RAM from disk */
static void rom_loadsram(rominfo_t *rominfo)
{
   File fp;
   char fn[PATH_MAX + 1];

   ASSERT(rominfo);

   if (rominfo->flags & ROM_FLAG_BATTERY)
   {
      strncpy(fn, rominfo->filename, PATH_MAX);
      osd_newextension(fn, ".sav");

      fp = sd.open(fn, FILE_READ);
      if (fp)
      {
         fp.read(rominfo->sram, SRAM_BANK_LENGTH * rominfo->sram_banks);
         fp.close();
         nofrendo_log_printf("Read battery RAM from %s.\n", fn);
      }
   }
}

/* Allocate space for SRAM */
static int rom_allocsram(rominfo_t *rominfo)
{
   /* Load up SRAM */
   rominfo->sram = (uint8*)mem_alloc(SRAM_BANK_LENGTH * rominfo->sram_banks, false);
   if (NULL == rominfo->sram)
   {
      gui_sendmsg(GUI_RED, "Could not allocate space for battery RAM");
      return -1;
   }

   /* make damn sure SRAM is clear */
   memset(rominfo->sram, 0, SRAM_BANK_LENGTH * rominfo->sram_banks);
   return 0;
}

/* If there's a trainer, load it in at $7000 */
static void rom_loadtrainer(File &fp, rominfo_t *rominfo)
{
   // ASSERT(fp); 
   ASSERT(rominfo);

   if (rominfo->flags & ROM_FLAG_TRAINER)
   {
      fp.read(rominfo->sram + TRAINER_OFFSET, TRAINER_LENGTH);
      nofrendo_log_printf("Read in trainer at $7000\n");
   }
}

static int rom_loadrom(File &fp, rominfo_t *rominfo)
{
   // ASSERT(fp);
   ASSERT(rominfo);

   /* Allocate ROM space, and load it up! */
   /* Use mem_alloc (Automatic Hybrid: SRAM first, then SDRAM) */
   rominfo->rom = (uint8*)mem_alloc(rominfo->rom_banks * ROM_BANK_LENGTH, false);
   if (NULL == rominfo->rom)
   {
      gui_sendmsg(GUI_RED, "Could not allocate space for ROM image");
      return -1;
   }
   
   // _fread(rominfo->rom, ROM_BANK_LENGTH, rominfo->rom_banks, fp);
   fp.read(rominfo->rom, ROM_BANK_LENGTH * rominfo->rom_banks);

   /* If there's VROM, allocate and stuff it in */
   if (rominfo->vrom_banks)
   {
      rominfo->vrom = (uint8*)mem_alloc(rominfo->vrom_banks * VROM_BANK_LENGTH, false);
      if (NULL == rominfo->vrom)
      {
         gui_sendmsg(GUI_RED, "Could not allocate space for VROM");
         return -1;
      }
      // _fread(rominfo->vrom, VROM_BANK_LENGTH, rominfo->vrom_banks, fp);
      fp.read(rominfo->vrom, VROM_BANK_LENGTH * rominfo->vrom_banks);
      
      /* Special case for Mapper 119 (TQROM): also needs 8KB CHR-RAM */
      if (rominfo->mapper_number == 119)
      {
         rominfo->vram = (uint8*)mem_alloc(VRAM_LENGTH, false);
         if (NULL == rominfo->vram)
         {
            gui_sendmsg(GUI_RED, "Could not allocate space for TQROM VRAM");
            return -1;
         }
         memset(rominfo->vram, 0, VRAM_LENGTH);
      }
   }
   else
   {
      // rominfo->vram = NOFRENDO_MALLOC(VRAM_LENGTH);
      rominfo->vram = (uint8*)mem_alloc(VRAM_LENGTH, false);
      if (NULL == rominfo->vram)
      {
         gui_sendmsg(GUI_RED, "Could not allocate space for VRAM");
         return -1;
      }
      memset(rominfo->vram, 0, VRAM_LENGTH);
   }
   
   /* Flush and Invalidate Cache to ensure any SDRAM usage is valid */
   SCB_CleanInvalidateDCache();

   return 0;
}

/* If we've got a VS. system game, load in the palette, as well */
static void rom_checkforpal(rominfo_t *rominfo, ppu_t *ppu)
{
   File fp;
   rgb_t vs_pal[64];
   char filename[PATH_MAX + 1];
   int i;

   ASSERT(rominfo);

   strncpy(filename, rominfo->filename, PATH_MAX);
   osd_newextension(filename, ".pal");

   fp = sd.open(filename, FILE_READ);
   if (!fp)
      return; /* no palette found  */

   for (i = 0; i < 64; i++)
   {
      vs_pal[i].r = fp.read();
      vs_pal[i].g = fp.read();
      vs_pal[i].b = fp.read();
   }

   fp.close();
   /* TODO: this should really be a *SYSTEM* flag */
   rominfo->flags |= ROM_FLAG_VERSUS;
   
   // Calling C function with struct pointer
   ppu_setpal(ppu, vs_pal);
   nofrendo_log_printf("Game specific palette found -- assuming VS. UniSystem\n");
}

static bool rom_findrom(const char *filename, rominfo_t *rominfo, File &fp)
{
   ASSERT(rominfo);

   if (NULL == filename)
      return false;

   /* Make a copy of the name so we can extend it */
   osd_fullname(rominfo->filename, filename);

   // Try opening
   fp = sd.open(rominfo->filename, FILE_READ);
   
   if (!fp)
   {
      /* Didn't find the file?  Maybe the .NES extension was omitted */
      if (NULL == strrchr(rominfo->filename, '.'))
         strncat(rominfo->filename, ".nes", PATH_MAX - strlen(rominfo->filename));

      /* this will either return NULL or a valid file pointer */
      fp = sd.open(rominfo->filename, FILE_READ);
   }

   return (bool)fp;
}

/* return 0 if this *is* an iNES file */
extern "C" int rom_checkmagic(const char *filename)
{
   inesheader_t head;
   rominfo_t rominfo;
   File fp;

   if (!rom_findrom(filename, &rominfo, fp))
      return -1;

   fp.read(&head, sizeof(head));
   fp.close();

   if (0 == memcmp(head.ines_magic, ROM_INES_MAGIC, 4))
      /* not an iNES file */
      return 0;

   return -1;
}

/*
 * Detects the region (NTSC=0, PAL=1) without loading the entire ROM.
 * Useful for initializing the video hardware with the correct refresh rate from the start.
 */
extern "C" int rom_get_region(const char *filename)
{
   inesheader_t head;
   rominfo_t rominfo;
   File fp;

   if (!rom_findrom(filename, &rominfo, fp))
      return 0; // NTSC defaults if not found

   fp.read(&head, sizeof(head));
   fp.close();

   if (memcmp(head.ines_magic, ROM_INES_MAGIC, 4))
      return 0; // NTSC is the default if it's not inNES

   /* Dynamic region detection (NTSC/PAL) */
   /* 1. Verify iNES 2.0 standard (Byte 7 bits 2-3 == 10) */
   if ((head.mapper_hinybble & 0x0C) == 0x08) {
      uint8_t tv_system = head.reserved[4] & 0x03;
      if (tv_system == 1 || tv_system == 3) {
         return 1; // PAL
      }
   } 
   /* 2. Fallback to iNES 1.0 standard (Byte 9 bit 0) */
   else if (head.reserved[1] & 0x01) {
      return 1; // PAL
   }

   return 0; // NTSC
}

static int rom_getheader(File &fp, rominfo_t *rominfo)
{
#define RESERVED_LENGTH 8
   inesheader_t head;
   uint8 reserved[RESERVED_LENGTH];
   bool header_dirty;

   // ASSERT(fp);
   ASSERT(rominfo);

   /* Read in the header */
   fp.read(&head, sizeof(head));

   if (memcmp(head.ines_magic, ROM_INES_MAGIC, 4))
   {
      gui_sendmsg(GUI_RED, "%s is not a valid ROM image", rominfo->filename);
      return -1;
   }

   rominfo->rom_banks = head.rom_banks;
   rominfo->vrom_banks = head.vrom_banks;
   /* iNES assumptions */
   rominfo->sram_banks = 8; /* 1kB banks, so 8KB */
   rominfo->vram_banks = 1; /* 8kB banks, so 8KB */
   rominfo->mirror = (head.rom_type & ROM_MIRRORTYPE) ? MIRROR_VERT : MIRROR_HORIZ;
   
   rominfo->flags = 0;
   
   /* Dynamic region detection (NTSC/PAL) */
   /* 1. Verify iNES 2.0 standard (Byte 7 bits 2-3 == 10) */
   if ((head.mapper_hinybble & 0x0C) == 0x08) {
      uint8_t tv_system = head.reserved[4] & 0x03;
      if (tv_system == 1 || tv_system == 3) {
         rominfo->flags |= ROM_FLAG_PAL;
      }
   } 
   /* 2. Fallback to iNES 1.0 standard (Byte 9 bit 0) */
   else if (head.reserved[1] & 0x01) {
      rominfo->flags |= ROM_FLAG_PAL;
   }

   if (rominfo->flags & ROM_FLAG_PAL) {
      DEBUG_PRINTLN("SYSTEM: PAL Region (50Hz)");
   } else {
      DEBUG_PRINTLN("SYSTEM: NTSC Region (60Hz)");
   }

   if (head.rom_type & ROM_BATTERY)
      rominfo->flags |= ROM_FLAG_BATTERY;
   if (head.rom_type & ROM_TRAINER)
      rominfo->flags |= ROM_FLAG_TRAINER;
   if (head.rom_type & ROM_FOURSCREEN)
      rominfo->flags |= ROM_FLAG_FOURSCREEN;
   /* TODO: fourscreen a mirroring type? */
   rominfo->mapper_number = head.rom_type >> 4;

   /* Do a compare - see if we've got a clean extended header */
   memset(reserved, 0, RESERVED_LENGTH);
   if (0 == memcmp(head.reserved, reserved, RESERVED_LENGTH))
   {
      /* We were clean */
      header_dirty = false;
      rominfo->mapper_number |= (head.mapper_hinybble & 0xF0);
   }
   else
   {
      header_dirty = true;

      /* @!?#@! DiskDude. */
      if (('D' == head.mapper_hinybble) && (0 == memcmp(head.reserved, "iskDude!", 8)))
         nofrendo_log_printf("`DiskDude!' found in ROM header, ignoring high mapper nybble\n");
      else
      {
         nofrendo_log_printf("ROM header dirty, possible problem\n");
         rominfo->mapper_number |= (head.mapper_hinybble & 0xF0);
      }
      
      // Removed rom_adddirty debug logging
   }

   /* TODO: this is an ugly hack, but necessary, I guess */
   /* Check for VS unisystem mapper */
   if (99 == rominfo->mapper_number)
      rominfo->flags |= ROM_FLAG_VERSUS;

   return 0;
}

/* Build the info string for ROM display */
extern "C" char *rom_getinfo(rominfo_t *rominfo)
{
   static char info[PATH_MAX + 1];
   char romname[PATH_MAX + 1], temp[PATH_MAX + 1];

   /* Look to see if we were given a path along with filename */
   /* TODO: strip extensions */
   if (strrchr(rominfo->filename, PATH_SEP))
      strncpy(romname, strrchr(rominfo->filename, PATH_SEP) + 1, PATH_MAX);
   else
      strncpy(romname, rominfo->filename, PATH_MAX);

   /* If our filename is too long, truncate our displayed filename */
   if (strlen(romname) > ROM_DISP_MAXLEN)
   {
      strncpy(info, romname, ROM_DISP_MAXLEN - 3);
      strcpy(info + (ROM_DISP_MAXLEN - 3), "...");
   }
   else
   {
      strcpy(info, romname);
   }

   sprintf(temp, " [%d] %dk/%dk %c", rominfo->mapper_number,
           rominfo->rom_banks * 16, rominfo->vrom_banks * 8,
           (rominfo->mirror == MIRROR_VERT) ? 'V' : 'H');

   /* Stick it on there! */
   strncat(info, temp, PATH_MAX - strlen(info));

   if (rominfo->flags & ROM_FLAG_BATTERY)
      strncat(info, "B", PATH_MAX - strlen(info));
   if (rominfo->flags & ROM_FLAG_TRAINER)
      strncat(info, "T", PATH_MAX - strlen(info));
   if (rominfo->flags & ROM_FLAG_FOURSCREEN)
      strncat(info, "4", PATH_MAX - strlen(info));

   return info;
}

/* Load a ROM image into memory */
extern "C" rominfo_t *rom_load(const char *filename, ppu_t *ppu)
{
   File fp;
   rominfo_t *rominfo;

   DEBUG_PRINTLN("ROM: rom_load start");
   
   rominfo = (rominfo_t*)mem_alloc(sizeof(rominfo_t), false);
   if (NULL == rominfo)
      return NULL;

   memset(rominfo, 0, sizeof(rominfo_t));

   DEBUG_PRINTLN("ROM: Finding file...");
   if (!rom_findrom(filename, rominfo, fp)) {
       if (strcmp(filename, "intro.nes") != 0 && strcmp(filename, "/intro.nes") != 0) {
          gui_sendmsg(GUI_RED, "%s not found, will use default ROM", filename);
       }
   }

   /* Get the header and stick it into rominfo struct */
   DEBUG_PRINTLN("ROM: Reading header...");
   if (!fp) {
      intro_get_header(rominfo);
   }
   else if (rom_getheader(fp, rominfo))
      goto _fail;

   DEBUG_PRINT("ROM: Mapper="); DEBUG_PRINTLN(rominfo->mapper_number);

   /* Make sure we really support the mapper */
   if (false == mmc_peek(rominfo->mapper_number))
   {
      gui_sendmsg(GUI_RED, "Mapper %d not yet implemented", rominfo->mapper_number);
      goto _fail;
   }

   /* iNES format doesn't tell us if we need SRAM, so
   ** we have to always allocate it -- bleh!
   */
   DEBUG_PRINTLN("ROM: Allocating SRAM...");
   if (rom_allocsram(rominfo))
      goto _fail;

   if (fp)
      rom_loadtrainer(fp, rominfo);

   DEBUG_PRINTLN("ROM: Loading PRG/CHR...");
   if (!fp)
   {
      if (intro_get_rom(rominfo))
         goto _fail;
   }
   else if (rom_loadrom(fp, rominfo))
      goto _fail;

   /* Close the file */
   if (fp)
      fp.close();

   rom_loadsram(rominfo);

   /* See if there's a palette we can load up */
   rom_checkforpal(rominfo, ppu);

   gui_sendmsg(GUI_GREEN, "ROM loaded: %s", rom_getinfo(rominfo));
   DEBUG_PRINTLN("ROM: Load Complete.");

   return rominfo;

_fail:
   DEBUG_PRINTLN("ROM: LOAD FAILED!");
   if (fp)
      fp.close();
   rom_free(&rominfo);
   return NULL;
}

/* Free a ROM */
extern "C" void rom_free(rominfo_t **rominfo)
{
   if (NULL == *rominfo)
   {
      gui_sendmsg(GUI_GREEN, "ROM not loaded");
      return;
   }

   /* Restore palette if we loaded in a VS jobber */
   if ((*rominfo)->flags & ROM_FLAG_VERSUS)
   {
      /* TODO: bad idea calling nes_getcontextptr... */
      ppu_setdefaultpal(nes_getcontextptr()->ppu);
      nofrendo_log_printf("Default NES palette restored\n");
   }

   rom_savesram(*rominfo);

   if ((*rominfo)->sram)
      free((*rominfo)->sram);
   if ((*rominfo)->rom)
      free((*rominfo)->rom);
   if ((*rominfo)->vrom)
      free((*rominfo)->vrom);
   if ((*rominfo)->vram)
      free((*rominfo)->vram);

   free(*rominfo); // Should match allocation method

   gui_sendmsg(GUI_GREEN, "ROM freed");
}
