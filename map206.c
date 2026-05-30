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
** map206.c
**
** NAMCOT-118 (Mapper 206) interface
** Adapted for GigaNES (Fixed Graphics Artifacts)
*/

#include <string.h>
#include "noftypes.h"
#include "nes_mmc.h"
#include "log.h"

static uint8 reg8000;

static void map206_write(uint32 address, uint8 value)
{
   switch (address & 0xE001)
   {
   case 0x8000: /* Bank select */
      reg8000 = value;
      break;

   case 0x8001: /* Bank data */
      switch (reg8000 & 0x07)
      {
      case 0: /* Select 2 KB CHR bank at PPU $0000-$07FF */
         /* Namcot 108/118 ignores bit 0 for 2KB banks */
         mmc_bankvrom(2, 0x0000, (value & 0x3E) >> 1);
         break;

      case 1: /* Select 2 KB CHR bank at PPU $0800-$0FFF */
         /* Namcot 108/118 ignores bit 0 for 2KB banks */
         mmc_bankvrom(2, 0x0800, (value & 0x3E) >> 1);
         break;

      case 2: /* Select 1 KB CHR bank at PPU $1000-$13FF */
         mmc_bankvrom(1, 0x1000, value & 0x3F);
         break;

      case 3: /* Select 1 KB CHR bank at PPU $1400-$17FF */
         mmc_bankvrom(1, 0x1400, value & 0x3F);
         break;

      case 4: /* Select 1 KB CHR bank at PPU $1800-$1BFF */
         mmc_bankvrom(1, 0x1800, value & 0x3F);
         break;

      case 5: /* Select 1 KB CHR bank at PPU $1C00-$1FFF */
         mmc_bankvrom(1, 0x1C00, value & 0x3F);
         break;

      case 6: /* Select 8 KB PRG ROM bank at $8000-$9FFF */
         mmc_bankrom(8, 0x8000, value & 0x0F);
         break;

      case 7: /* Select 8 KB PRG ROM bank at $A000-$BFFF */
         mmc_bankrom(8, 0xA000, value & 0x0F);
         break;
      }
      break;
   }
}

static void map206_init(void)
{
   int total_8k_banks = mmc_getinfo()->rom_banks * 2;
   reg8000 = 0;

   /* Initialize PRG banks ($8000 and $A000 to first banks, $C000 and $E000 fixed to last) */
   mmc_bankrom(8, 0x8000, 0);
   mmc_bankrom(8, 0xA000, 1);
   
   /* Calculate fixed banks explicitly */
   mmc_bankrom(8, 0xC000, total_8k_banks - 2);
   mmc_bankrom(8, 0xE000, total_8k_banks - 1);

   /* Initialize CHR banks */
   mmc_bankvrom(8, 0x0000, 0);
}

static void map206_getstate(SnssMapperBlock *state)
{
   state->extraData.mapper1.registers[0] = reg8000;
}

static void map206_setstate(SnssMapperBlock *state)
{
   reg8000 = state->extraData.mapper1.registers[0];
}

static map_memwrite map206_memwrite[] =
{
   {0x8000, 0xFFFF, map206_write},
   {-1, -1, NULL}
};

mapintf_t map206_intf =
{
   206,            /* mapper number */
   "NAMCOT-118",   /* mapper name */
   map206_init,    /* init routine */
   NULL,           /* vblank callback */
   NULL,           /* hblank callback */
   map206_getstate, /* get state (snss) */
   map206_setstate, /* set state (snss) */
   NULL,           /* memory read structure */
   map206_memwrite, /* memory write structure */
   NULL            /* external sound device */
};
