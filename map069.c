/*
** GigaNES (c) 2026 Kaled Souky <https://github.com/KaledSouky>
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
** map069.c 
**
** Sunsoft FME-7 (Mapper 69) interface - "Batman Perfection Version"
** Adapted for GigaNES (Fixed intro rendering, gameplay transitions, and 
** scrolling logic)
*/

#include <string.h>
#include "noftypes.h"
#include "nes_mmc.h"
#include "nes_ppu.h"
#include "nes6502.h"
#include "nes.h"

static struct {
   uint8 command;
   uint16 irq_counter;
   uint8 irq_enabled;
} map69;

static uint8 fme7_ram[0x2000];

/* Mapper 69 write handler */
static void map69_write(uint32 address, uint8 value)
{
   if (address < 0xA000) {
      map69.command = value & 0x0F;
   } else if (address < 0xC000) {
      switch (map69.command) {
         case 0x00: case 0x01: case 0x02: case 0x03:
         case 0x04: case 0x05: case 0x06: case 0x07:
            mmc_bankvrom(1, map69.command << 10, value);
            break;

         case 0x08: /* PRG Bank 0 ($6000) */
            /* Batman uses this for RAM and ROM. Bit 6/7 are control bits. */
            if (value & 0x40) {
               /* RAM Mode: Directly map our buffer to CPU pages 6 & 7 */
               nes6502_context cpu;
               nes6502_getcontext(&cpu);
               cpu.mem_page[6] = fme7_ram;
               cpu.mem_page[7] = fme7_ram + 0x1000;
               nes6502_setcontext(&cpu);
            } else {
               /* ROM Mode: Use standard banking with safety mask */
               mmc_bankrom(8, 0x6000, value & 0x3F);
            }
            break;

         case 0x09: mmc_bankrom(8, 0x8000, value & 0x3F); break;
         case 0x0A: mmc_bankrom(8, 0xA000, value & 0x3F); break;
         case 0x0B: mmc_bankrom(8, 0xC000, value & 0x3F); break;

         case 0x0C: /* Mirroring */
            switch (value & 3) {
               case 0: ppu_mirror(0, 1, 0, 1); break; /* Vertical */
               case 1: ppu_mirror(0, 0, 1, 1); break; /* Horizontal */
               case 2: ppu_mirror(0, 0, 0, 0); break; /* 1-screen 0 */
               case 3: ppu_mirror(1, 1, 1, 1); break; /* 1-screen 1 */
            }
            /* Force PPU to refresh pointers to avoid black scroll areas */
            ppu_mirrorhipages();
            break;

         case 0x0D: map69.irq_enabled = value; break;
         case 0x0E: map69.irq_counter = (map69.irq_counter & 0xFF00) | value; break;
         case 0x0F: map69.irq_counter = (map69.irq_counter & 0x00FF) | (value << 8); break;
      }
   }
}

/* SRAM write handler required to correctly render the Joker's face during the intro sequence */
static void map69_write_sram(uint32 address, uint8 value)
{
   fme7_ram[address & 0x1FFF] = value;
}

static void map69_hblank(int vblank)
{
   /* Using 114-cycle timing based on empirical test results for cycle-accuracy */	
   if (map69.irq_enabled & 0x80) {
      if (map69.irq_counter < 114) {
         if (map69.irq_enabled & 0x01) {
            nes_irq();
         }
         map69.irq_counter = 0xFFFF;
      } else {
         map69.irq_counter -= 114;
      }
   }
}

static void map69_init(void)
{
   memset(&map69, 0, sizeof(map69));
   memset(fme7_ram, 0, sizeof(fme7_ram));

   mmc_bankrom(8, 0x6000, 0);
   mmc_bankrom(8, 0x8000, 0);
   mmc_bankrom(8, 0xA000, 1);
   mmc_bankrom(8, 0xC000, 2);
   mmc_bankrom(8, 0xE000, MMC_LASTBANK);
}

static map_memwrite map69_memwrite[] = {
   {0x6000, 0x7FFF, map69_write_sram},
   {0x8000, 0xFFFF, map69_write},
   {-1, -1, NULL}
};

mapintf_t map69_intf = 
{
   69,              /* mapper number */ 
   "Sunsoft FME-7", /* mapper name */ 
   map69_init,      /* init routine */
   NULL,            /* vblank callback */
   map69_hblank,    /* hblank callback */
   NULL,            /* get state (snss) */
   NULL,            /* set state (snss) */
   NULL,            /* memory read structure */
   map69_memwrite,  /* memory write structure */
   NULL             /* external sound device */
};
