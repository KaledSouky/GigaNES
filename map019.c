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
** map019.c
**
** Namcot 163 (Mapper 19) interface
** Adapted for GigaNES (Fixed IRQ Timing & Nametable Mirroring)
*/

#include <string.h>
#include "noftypes.h"
#include "nes_mmc.h"
#include "nes_ppu.h"
#include "nes.h"
#include "log.h"

static struct
{
   uint16 counter;
   bool enabled;
} irq;

static void map19_write(uint32 address, uint8 value)
{
   int reg = address >> 11;
   uint8 *page;

   switch (reg)
   {
   case 0xA:
      irq.counter = (irq.counter & 0x7F00) | value;
      break;

   case 0xB:
      irq.counter = (irq.counter & 0x00FF) | ((value & 0x7F) << 8);
      irq.enabled = (value & 0x80) ? true : false;
      break;

   case 0x10:
   case 0x11:
   case 0x12:
   case 0x13:
   case 0x14:
   case 0x15:
   case 0x16:
   case 0x17:
      mmc_bankvrom(1, (reg & 7) << 10, value);
      break;

   case 0x18:
   case 0x19:
   case 0x1A:
   case 0x1B:
      /* VRAM / Nametable mapping */
      if (value < 0xE0) {
         /* Map VROM as Nametable */
         page = &mmc_getinfo()->vrom[(value % (mmc_getinfo()->vrom_banks * 8)) << 10] - (0x2000 + ((reg & 3) << 10));
      } else {
         /* Normal Nametable RAM */
         page = ppu_getnametable(value & 1) - (0x2000 + ((reg & 3) << 10));
      }
      /* Set primary nametable ($2000-$2FFF) */
      ppu_setpage(1, (reg & 3) + 8, page);
      /* Set mirrored nametable ($3000-$3FFF) */
      ppu_setpage(1, (reg & 3) + 12, page - 0x1000);
      break;

   case 0x1C:
      mmc_bankrom(8, 0x8000, value & 0x3F);
      break;

   case 0x1D:
      mmc_bankrom(8, 0xA000, value & 0x3F);
      break;

   case 0x1E:
      mmc_bankrom(8, 0xC000, value & 0x3F);
      break;

   default:
      break;
   }
}

static uint8 map19_read(uint32 address)
{
   int reg = address >> 11;

   switch (reg)
   {
   case 0xA:
      return irq.counter & 0xFF;

   case 0xB:
      return (irq.counter >> 8) | (irq.enabled ? 0x80 : 0x00);

   default:
      return 0xFF;
   }
}

static void map19_hblank(int scanline)
{
   if (irq.enabled)
   {
      /* Increment counter by CPU cycles per scanline (approx 114) */
      irq.counter += 114;

      if (irq.counter >= 0x7FFF)
      {
         nes_irq();
         /* Hardware triggers IRQ but counter keeps bit 15 set */
         irq.counter |= 0x8000; 
      }
   }
}

static void map19_init(void)
{
   int total_8k_banks = mmc_getinfo()->rom_banks * 2;
   
   irq.counter = 0;
   irq.enabled = false;

   /* Initialize PRG banks */
   mmc_bankrom(8, 0x8000, 0);
   mmc_bankrom(8, 0xA000, 1);
   mmc_bankrom(8, 0xC000, 2);
   mmc_bankrom(8, 0xE000, total_8k_banks - 1);

   /* Default CHR */
   mmc_bankvrom(8, 0x0000, 0);
}

static void map19_getstate(SnssMapperBlock *state)
{
   state->extraData.mapper19.irqCounterLowByte = irq.counter & 0xFF;
   state->extraData.mapper19.irqCounterHighByte = irq.counter >> 8;
   state->extraData.mapper19.irqCounterEnabled = irq.enabled;
}

static void map19_setstate(SnssMapperBlock *state)
{
   irq.counter = (state->extraData.mapper19.irqCounterHighByte << 8) | state->extraData.mapper19.irqCounterLowByte;
   irq.enabled = state->extraData.mapper19.irqCounterEnabled;
}

static map_memwrite map19_memwrite[] =
{
   {0x5000, 0x5FFF, map19_write},
   {0x8000, 0xFFFF, map19_write},
   {-1, -1, NULL}
};

static map_memread map19_memread[] =
{
   {0x5000, 0x5FFF, map19_read},
   {-1, -1, NULL}
};

mapintf_t map19_intf =
{
   19,             /* mapper number */
   "Namco 129/163", /* mapper name */
   map19_init,     /* init routine */
   NULL,           /* vblank callback */
   map19_hblank,   /* hblank callback */
   map19_getstate, /* get state (snss) */
   map19_setstate, /* set state (snss) */
   map19_memread,  /* memory read structure */
   map19_memwrite, /* memory write structure */
   NULL            /* external sound device */
};
