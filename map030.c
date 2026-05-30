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
** map030.c
** 
** UNROM-512 (Mapper 30) interface
** Adapted for GigaNES 
*/

#include "noftypes.h"
#include "nes_mmc.h"
#include "nes_ppu.h"

static void map30_write(uint32 address, uint8 value)
{
    /* Bit 7 controls single-screen mirroring */
    int page = (value & 0x80) ? 1 : 0;
    ppu_mirror(page, page, page, page);

    /* PRG swap (16KB at $8000) */
    mmc_bankrom(16, 0x8000, value & 0x1F);
    
    /* CHR swap (8KB at $0000) */
    mmc_bankvrom(8, 0x0000, (value >> 5) & 0x03);
}

static void map30_init(void)
{
    mmc_bankrom(16, 0x8000, 0);
    mmc_bankrom(16, 0xC000, MMC_LASTBANK);
}

static map_memwrite map30_memwrite[] =
{
   { 0x8000, 0xFFFF, map30_write },
   { -1, -1, NULL }
};

mapintf_t map30_intf =
{
    30,           /* mapper number */
    "UNROM-512",  /* mapper name */
    map30_init,   /* init routine */
    NULL,         /* vblank callback */
    NULL,         /* hblank callback */
    NULL,         /* get state (snss) */
    NULL,         /* set state (snss) */
    NULL,         /* memory read structure */
    map30_memwrite, /* memory write structure */
    NULL          /* external sound device */
};
