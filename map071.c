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
** map071.c 
**
** Camerica (Mapper 71) interface - Used by: Micro Machines, Fire Hawk, MiG 29
** Adapted for GigaNES (Fixed persistent rendering artifacts and scrolling 
** glitches)
*/

#include "noftypes.h"
#include "nes_mmc.h"
#include "nes_ppu.h"

static void map71_write(uint32 address, uint8 value)
{
   /* Mirroring Control (Fire Hawk / Micro Machines) */
   if ((address & 0xF000) == 0x9000)
   {
      if (value & 0x04)
         ppu_mirror(1, 1, 1, 1); /* Single Screen 1 ($2400) */
      else
         ppu_mirror(0, 0, 0, 0); /* Single Screen 0 ($2000) */
   }
   else if (address >= 0xC000)
   {
      /* Select 16K PRG Bank at $8000 */
      mmc_bankrom(16, 0x8000, value);
   }
}

static void map71_init(void)
{
   /* Initialize Banks */
   mmc_bankrom(16, 0x8000, 0);
   mmc_bankrom(16, 0xC000, MMC_LASTBANK);
}

static map_memwrite map71_memwrite[] = {
   {0x8000, 0xFFFF, map71_write},
   {-1, -1, NULL}
};

mapintf_t map71_intf = 
{
   71,             /* mapper number */
   "Camerica",     /* mapper name */
   map71_init,     /* init routine */
   NULL,           /* vblank callback */
   NULL,           /* hblank callback */
   NULL,           /* get state (snss) */
   NULL,           /* set state (snss) */
   NULL,           /* memory read structure */
   map71_memwrite, /* memory write structure */
   NULL            /* external sound device */
};
