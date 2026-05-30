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
** map076.c
**
** NAMCOT-3446 (Mapper 76) interface
** Adapted for GigaNES
*/

#include "noftypes.h"
#include "nes_mmc.h"
#include "nes_ppu.h"

static uint8 reg8000;

static void map76_write(uint32 address, uint8 value)
{
   switch (address & 0xE001)
   {
      case 0x8000: /* Bank select */
         reg8000 = value;
         break;

      case 0x8001: /* Bank data */
         switch (reg8000 & 0x07)
         {
            case 2: /* Select 2 KB CHR bank at PPU $0000-$07FF */
               mmc_bankvrom(2, 0x0000, value);
               break;

            case 3: /* Select 2 KB CHR bank at PPU $0800-$0FFF */
               mmc_bankvrom(2, 0x0800, value);
               break;

            case 4: /* Select 2 KB CHR bank at PPU $1000-$17FF */
               mmc_bankvrom(2, 0x1000, value);
               break;

            case 5: /* Select 2 KB CHR bank at PPU $1800-$1FFF */
               mmc_bankvrom(2, 0x1800, value);
               break;

            case 6: /* Select 8 KB PRG ROM bank at $8000-$9FFF */
               mmc_bankrom(8, 0x8000, value);
               break;

            case 7: /* Select 8 KB PRG ROM bank at $A000-$BFFF */
               mmc_bankrom(8, 0xA000, value);
               break;
         }
         break;
   }
}

static void map76_getstate(SnssMapperBlock *state)
{
   state->extraData.mapper76.reg8000 = reg8000;
}

static void map76_setstate(SnssMapperBlock *state)
{
   reg8000 = state->extraData.mapper76.reg8000;
}

static void map76_init(void)
{
   reg8000 = 0;
}

static map_memwrite map76_memwrite[] =
{
   { 0x8000, 0xFFFF, map76_write },
   { -1, -1, NULL }
};

mapintf_t map76_intf =
{
   76,            /* mapper number */
   "NAMCOT-3446",  /* mapper name */
   map76_init,    /* init routine */
   NULL,          /* vblank callback */
   NULL,          /* hblank callback */
   map76_getstate, /* get state (snss) */
   map76_setstate, /* set state (snss) */
   NULL,          /* memory read structure */
   map76_memwrite, /* memory write structure */
   NULL           /* external sound device */
};
