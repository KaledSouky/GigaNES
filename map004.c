/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
** Modified by Kaled Souky <https://github.com/KaledSouky> on 
** 17-04-2026 for GigaNES.
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General 
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** map4.c
**
** mapper 4 interface
** $Id: map004.c,v 1.2 2001/04/27 14:37:11 neil Exp $
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

static void map4_update_prg(void)
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

static void map4_update_chr(void)
{
   int chr_mode = (mmc3_command & 0x80) >> 7;
   int base = chr_mode ? 0x1000 : 0x0000;

   /* 2KB banks (registers 0 and 1 ignore bit 0) */
   mmc_bankvrom(1, base ^ 0x0000, mmc3_regs[0] & 0xFE);
   mmc_bankvrom(1, base ^ 0x0400, mmc3_regs[0] | 0x01);
   mmc_bankvrom(1, base ^ 0x0800, mmc3_regs[1] & 0xFE);
   mmc_bankvrom(1, base ^ 0x0C00, mmc3_regs[1] | 0x01);

   /* 1KB Banks */
   mmc_bankvrom(1, base ^ 0x1000, mmc3_regs[2]);
   mmc_bankvrom(1, base ^ 0x1400, mmc3_regs[3]);
   mmc_bankvrom(1, base ^ 0x1800, mmc3_regs[4]);
   mmc_bankvrom(1, base ^ 0x1C00, mmc3_regs[5]);
}

/* mapper 4: MMC3 */
static void map4_write(uint32 address, uint8 value)
{
   switch (address & 0xE001)
   {
   case 0x8000:
      if ((mmc3_command & 0xC0) != (value & 0xC0))
      {
         mmc3_command = value;
         map4_update_prg();
         map4_update_chr();
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
         map4_update_chr();
      else
         map4_update_prg();
   }
   break;

   case 0xA000:
      /* four screen mirroring crap */
      if (0 == (mmc_getinfo()->flags & ROM_FLAG_FOURSCREEN))
      {
         if (value & 1)
            ppu_mirror(0, 0, 1, 1); /* horizontal */
         else
            ppu_mirror(0, 1, 0, 1); /* vertical */
      }
      break;

   case 0xA001:
      /* bit 7: RAM enable, bit 6: RAM write protect */
      break;

   case 0xC000:
      irq.latch = value;
      break;

   case 0xC001:
      irq.reset = true;
      break;

   case 0xE000:
      irq.enabled = false;
      nes_clear_irq(); /* Acknowledge/Clear IRQ signal */
      break;

   case 0xE001:
      irq.enabled = true;
      break;

   default:
      break;
   }
}

static void map4_hblank(int vblank)
{
   if (vblank || !ppu_enabled())
      return;

   /* Synchronized reload logic */
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

   /* IRQ triggered when the counter reaches zero */
   if (irq.counter == 0 && irq.enabled)
   {
      nes_irq();
   }
}

static void map4_getstate(SnssMapperBlock *state)
{
   state->extraData.mapper4.irqCounter = irq.counter;
   state->extraData.mapper4.irqLatchCounter = irq.latch;
   state->extraData.mapper4.irqCounterEnabled = irq.enabled;
   state->extraData.mapper4.last8000Write = mmc3_command;
}

static void map4_setstate(SnssMapperBlock *state)
{
   irq.counter = state->extraData.mapper4.irqCounter;
   irq.latch = state->extraData.mapper4.irqLatchCounter;
   irq.enabled = state->extraData.mapper4.irqCounterEnabled;
   mmc3_command = state->extraData.mapper4.last8000Write;
   map4_update_prg();
   map4_update_chr();
}

static void map4_wram_write(uint32 address, uint8 value)
{
   if (mmc_getinfo()->sram)
   {
      mmc_getinfo()->sram[address - 0x6000] = value;
   }
}

static uint8 map4_wram_read(uint32 address)
{
   if (mmc_getinfo()->sram)
   {
      return mmc_getinfo()->sram[address - 0x6000];
   }
   return 0x00; /* Open bus fallback */
}

static void map4_init(void)
{
   int i;
   irq.counter = irq.latch = 0;
   irq.enabled = irq.reset = false;
   mmc3_command = 0;
   for (i = 0; i < 8; i++)
      mmc3_regs[i] = 0;

   /* Initial configuration suggested by actual hardware */
   mmc3_regs[6] = 0;
   mmc3_regs[7] = 1;

   map4_update_prg();
   map4_update_chr();
}

static map_memread map4_memread[] =
    {
        {0x6000, 0x7FFF, map4_wram_read},
        {-1, -1, NULL}};

static map_memwrite map4_memwrite[] =
    {
        {0x6000, 0x7FFF, map4_wram_write},
        {0x8000, 0xFFFF, map4_write},
        {-1, -1, NULL}};

mapintf_t map4_intf =
    {
        4,             /* mapper number */
        "MMC3",        /* mapper name */
        map4_init,     /* init routine */
        NULL,          /* vblank callback */
        map4_hblank,   /* hblank callback */
        map4_getstate, /* get state (snss) */
        map4_setstate, /* set state (snss) */
        map4_memread,  /* memory read structure */
        map4_memwrite, /* memory write structure */
        NULL           /* external sound device */
};

/*
** $Log: map004.c,v $
** Revision 1.2  2001/04/27 14:37:11  neil
** wheeee
**
** Revision 1.1  2001/04/27 12:54:40  neil
** blah
**
** Revision 1.1.1.1  2001/04/27 07:03:54  neil
** initial
**
** Revision 1.2  2000/11/26 15:40:49  matt
** hey, it actually works now
**
** Revision 1.1  2000/10/24 12:19:32  matt
** changed directory structure
**
** Revision 1.12  2000/10/23 15:53:27  matt
** suppressed warnings
**
** Revision 1.11  2000/10/22 19:17:46  matt
** mapper cleanups galore
**
** Revision 1.10  2000/10/22 15:03:13  matt
** simplified mirroring
**
** Revision 1.9  2000/10/21 19:33:38  matt
** many more cleanups
**
** Revision 1.8  2000/10/10 13:58:17  matt
** stroustrup squeezing his way in the door
**
** Revision 1.7  2000/10/08 18:05:44  matt
** kept old version around, just in case....
**
** Revision 1.6  2000/07/15 23:52:19  matt
** rounded out a bunch more mapper interfaces
**
** Revision 1.5  2000/07/10 13:51:25  matt
** using generic nes_irq() routine now
**
** Revision 1.4  2000/07/10 05:29:03  matt
** cleaned up some mirroring issues
**
** Revision 1.3  2000/07/06 02:48:43  matt
** clearly labelled structure members
**
** Revision 1.2  2000/07/05 05:04:39  matt
** minor modifications
**
** Revision 1.1  2000/07/04 23:11:45  matt
** initial revision
**
*/
