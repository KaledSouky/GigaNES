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
** map119.c 
**
** TQROM (Mapper 119) interface
** Adapted for GigaNES (This mapper is a variant of MMC3/Mapper 4 that can use 
** both CHR-ROM and CHR-RAM)
*/

#include "noftypes.h"
#include "nes_mmc.h"
#include "nes.h"
#include "libsnss.h"
#include "nes_ppu.h"

static struct
{
   int counter, latch;
   bool enabled, reset;
} irq __attribute__((section(".dtcmram_bss")));

static uint8 mmc3_regs[8] __attribute__((section(".dtcmram_bss")));
static uint8 mmc3_command __attribute__((section(".dtcmram_bss")));

static void map119_update_prg(void)
{
   int prg_mode = (mmc3_command & 0x40) >> 6;
   int last_bank = (mmc_getinfo()->rom_banks * 2) - 1;
   int second_last = last_bank - 1;

   if (prg_mode == 0)
   {
      mmc_bankrom(8, 0x8000, mmc3_regs[6]);
      mmc_bankrom(8, 0xA000, mmc3_regs[7]);
      mmc_bankrom(8, 0xC000, second_last);
      mmc_bankrom(8, 0xE000, last_bank);
   }
   else
   {
      mmc_bankrom(8, 0x8000, second_last);
      mmc_bankrom(8, 0xA000, mmc3_regs[7]);
      mmc_bankrom(8, 0xC000, mmc3_regs[6]);
      mmc_bankrom(8, 0xE000, last_bank);
   }
}

static void map119_bankvrom(int size, uint32 address, int bank)
{
   uint8 *vptr;
   if (bank & 0x40) {
      // CHR-RAM (bit 6 set)
      vptr = &mmc_getinfo()->vram[(bank & 0x07) << 10]; 
   } else {
      // CHR-ROM (bit 6 clear)
      vptr = &mmc_getinfo()->vrom[(bank % (mmc_getinfo()->vrom_banks * 8)) << 10];
   }
   ppu_setpage(size, address >> 10, vptr - address);
}

static void map119_update_chr(void)
{
   int chr_mode = (mmc3_command & 0x80) >> 7;
   int base = chr_mode ? 0x1000 : 0x0000;

   /* 2KB banks (registers 0 and 1) */
   map119_bankvrom(1, base ^ 0x0000, mmc3_regs[0] & 0xFE);
   map119_bankvrom(1, base ^ 0x0400, mmc3_regs[0] | 0x01);
   map119_bankvrom(1, base ^ 0x0800, mmc3_regs[1] & 0xFE);
   map119_bankvrom(1, base ^ 0x0C00, mmc3_regs[1] | 0x01);

   /* 1KB Banks */
   map119_bankvrom(1, base ^ 0x1000, mmc3_regs[2]);
   map119_bankvrom(1, base ^ 0x1400, mmc3_regs[3]);
   map119_bankvrom(1, base ^ 0x1800, mmc3_regs[4]);
   map119_bankvrom(1, base ^ 0x1C00, mmc3_regs[5]);
}

static void map119_write(uint32 address, uint8 value)
{
   switch (address & 0xE001)
   {
   case 0x8000:
      if ((mmc3_command & 0xC0) != (value & 0xC0))
      {
         mmc3_command = value;
         map119_update_prg();
         map119_update_chr();
      }
      else
      {
         mmc3_command = value;
      }
      break;

   case 0x8001:
   {
      int reg_idx = mmc3_command & 0x07;
      mmc3_regs[reg_idx] = value;
      if (reg_idx <= 5)
         map119_update_chr();
      else
         map119_update_prg();
   }
   break;

   case 0xA000:
      if (0 == (mmc_getinfo()->flags & ROM_FLAG_FOURSCREEN))
      {
         if (value & 1)
            ppu_mirror(0, 0, 1, 1); /* horizontal */
         else
            ppu_mirror(0, 1, 0, 1); /* vertical */
      }
      break;

   case 0xA001:
      break;

   case 0xC000:
      irq.latch = value;
      break;

   case 0xC001:
      irq.reset = true;
      break;

   case 0xE000:
      irq.enabled = false;
      nes_clear_irq();
      break;

   case 0xE001:
      irq.enabled = true;
      break;
   }
}

static void map119_hblank(int vblank)
{
   if (vblank || !ppu_enabled())
      return;

   if (irq.reset)
   {
      irq.counter = irq.latch;
      irq.reset = false;
   }
   else if (irq.counter == 0)
   {
      irq.counter = irq.latch;
   }
   else
   {
      irq.counter--;
   }

   if (irq.counter == 0 && irq.enabled)
   {
      nes_irq();
   }
}

static void map119_getstate(SnssMapperBlock *state)
{
   state->extraData.mapper4.irqCounter = irq.counter;
   state->extraData.mapper4.irqLatchCounter = irq.latch;
   state->extraData.mapper4.irqCounterEnabled = irq.enabled;
   state->extraData.mapper4.last8000Write = mmc3_command;
}

static void map119_setstate(SnssMapperBlock *state)
{
   irq.counter = state->extraData.mapper4.irqCounter;
   irq.latch = state->extraData.mapper4.irqLatchCounter;
   irq.enabled = state->extraData.mapper4.irqCounterEnabled;
   mmc3_command = state->extraData.mapper4.last8000Write;
   map119_update_prg();
   map119_update_chr();
}

static void map119_wram_write(uint32 address, uint8 value)
{
   if (mmc_getinfo()->sram)
   {
      mmc_getinfo()->sram[address - 0x6000] = value;
   }
}

static uint8 map119_wram_read(uint32 address)
{
   if (mmc_getinfo()->sram)
   {
      return mmc_getinfo()->sram[address - 0x6000];
   }
   return 0x00;
}

static void map119_init(void)
{
   int i;
   irq.counter = irq.latch = 0;
   irq.enabled = irq.reset = false;
   mmc3_command = 0;
   for (i = 0; i < 8; i++)
      mmc3_regs[i] = 0;

   mmc3_regs[6] = 0;
   mmc3_regs[7] = 1;

   map119_update_prg();
   map119_update_chr();
}

static map_memread map119_memread[] =
    {
        {0x6000, 0x7FFF, map119_wram_read},
        {-1, -1, NULL}};

static map_memwrite map119_memwrite[] =
    {
        {0x6000, 0x7FFF, map119_wram_write},
        {0x8000, 0xFFFF, map119_write},
        {-1, -1, NULL}};

mapintf_t map119_intf =
    {
        119,            /* mapper number */
        "TQROM",        /* mapper name */
        map119_init,    /* init routine */
        NULL,           /* vblank callback */
        map119_hblank,  /* hblank callback */
        map119_getstate, /* get state (snss) */
        map119_setstate, /* set state (snss) */
        map119_memread, /* memory read structure */
        map119_memwrite, /* memory write structure */
        NULL            /* external sound device */
};
