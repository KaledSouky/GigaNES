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
** vrcvisnd.c
**
** VRCVI sound hardware emulation - Optimization: FPU-based gain 
** scaling for improved audio mixing
** $Id: vrcvisnd.c,v 1.2 2001/04/27 14:37:11 neil Exp $
*/

#include "noftypes.h"
#include "vrcvisnd.h"
#include "nes_apu.h"

/* Sound balance attenuation factor (0.0f to 1.0f)
** Adjusted to 0.30f to prevent VRC6 music from saturating the NES channels */
#define VRC6_MIX_LEVEL 0.30f 

typedef struct vrcvirectangle_s
{
   bool enabled;
   uint8 reg[3];
   float accum;
   uint8 adder;
   int32 freq;
   int32 volume;
   uint8 duty_flip;
} vrcvirectangle_t;

typedef struct vrcvisawtooth_s
{
   bool enabled;
   uint8 reg[3];
   float accum;
   uint8 adder;
   uint8 output_acc;
   int32 freq;
   uint8 volume;
} vrcvisawtooth_t;

typedef struct vrcvisnd_s
{
   vrcvirectangle_t rectangle[2];
   vrcvisawtooth_t saw;
   float incsize;
   bool ab_swap;
} vrcvisnd_t;

static vrcvisnd_t vrcvi;

void vrcvi_set_ab_swap(bool swap)
{
   vrcvi.ab_swap = swap;
}

/* VRCVI rectangle wave generation */
static int32 vrcvi_rectangle(vrcvirectangle_t *chan)
{
   chan->accum -= vrcvi.incsize; 
   while (chan->accum < 0)
   {
      chan->accum += (float)chan->freq;
      chan->adder = (chan->adder + 1) & 0x0F;
   }

   if (false == chan->enabled)
      return 0;

   if (chan->adder < chan->duty_flip)
      return -(chan->volume);
   else
      return chan->volume;
}

/* VRCVI sawtooth wave generation */
static int32 vrcvi_sawtooth(vrcvisawtooth_t *chan)
{
   chan->accum -= vrcvi.incsize; 
   while (chan->accum < 0)
   {
      chan->accum += (float)chan->freq;
      chan->output_acc += chan->volume;

      chan->adder++;
      if (7 == chan->adder)
      {
         chan->adder = 0;
         chan->output_acc = 0;
      }
   }

   if (false == chan->enabled)
      return 0;

   /* Original VRC6 scaling */
   return (chan->output_acc >> 3) << 9;
}

/* mix vrcvi sound channels together */
static int32 vrcvi_process(void)
{
   float mixed_output;

   /* Channels are mixed in floating point to leverage the STM32H7 hardware FPU */
   mixed_output = (float)vrcvi_rectangle(&vrcvi.rectangle[0]);
   mixed_output += (float)vrcvi_rectangle(&vrcvi.rectangle[1]);
   mixed_output += (float)vrcvi_sawtooth(&vrcvi.saw);

   /* Attenuation factor is applied to enhance audio balance between the whip sound and SFX in Akumajou Densetsu (Castlevania III) */
   return (int32)(mixed_output * VRC6_MIX_LEVEL);
}

/* write to registers */
static void vrcvi_write(uint32 address, uint8 value)
{
   int chan;

   if (vrcvi.ab_swap)
   {
      uint32 a0 = (address >> 0) & 0x01;
      uint32 a1 = (address >> 1) & 0x01;
      address = (address & ~0x03) | (a0 << 1) | (a1 << 0);
   }

   chan = (address >> 12) - 9;

   switch (address & 0xB003)
   {
   case 0x9000:
   case 0xA000:
      vrcvi.rectangle[chan].reg[0] = value;
      vrcvi.rectangle[chan].volume = (value & 0x0F) << 8;
      vrcvi.rectangle[chan].duty_flip = (value >> 4) + 1;
      break;

   case 0x9001:
   case 0xA001:
      vrcvi.rectangle[chan].reg[1] = value;
      vrcvi.rectangle[chan].freq = ((vrcvi.rectangle[chan].reg[2] & 0x0F) << 8) + value + 1;
      break;

   case 0x9002:
   case 0xA002:
      vrcvi.rectangle[chan].reg[2] = value;
      vrcvi.rectangle[chan].freq = ((value & 0x0F) << 8) + vrcvi.rectangle[chan].reg[1] + 1;
      vrcvi.rectangle[chan].enabled = (value & 0x80) ? true : false;
      break;

   case 0xB000:
      vrcvi.saw.reg[0] = value;
      vrcvi.saw.volume = value & 0x3F;
      break;

   case 0xB001:
      vrcvi.saw.reg[1] = value;
      vrcvi.saw.freq = (((vrcvi.saw.reg[2] & 0x0F) << 8) + value + 1) << 1;
      break;

   case 0xB002:
      vrcvi.saw.reg[2] = value;
      vrcvi.saw.freq = (((value & 0x0F) << 8) + vrcvi.saw.reg[1] + 1) << 1;
      vrcvi.saw.enabled = (value & 0x80) ? true : false;
      break;

   default:
      break;
   }
}

/* reset state of vrcvi sound channels */
static void vrcvi_reset(void)
{
   int i;
   apu_t apu;

   apu_getcontext(&apu);
   vrcvi.incsize = apu.cycle_rate;

   for (i = 0; i < 3; i++)
   {
      vrcvi_write(0x9000 + i, 0);
      vrcvi_write(0xA000 + i, 0);
      vrcvi_write(0xB000 + i, 0);
   }
}

static apu_memwrite vrcvi_memwrite[] =
    {
        {0x9000, 0x9002, vrcvi_write}, 
        {0xA000, 0xA002, vrcvi_write},
        {0xB000, 0xB002, vrcvi_write},
        {-1, -1, NULL}};

apuext_t vrcvi_ext =
    {
        NULL, /* no init */ 
        NULL, /* no shutdown */ 
        vrcvi_reset,
        vrcvi_process,
        NULL, /* no reads */ 
        vrcvi_memwrite};

/*
** $Log: vrcvisnd.c,v $
** Revision 1.2  2001/04/27 14:37:11  neil
** wheeee
**
** Revision 1.1  2001/04/27 12:54:40  neil
** blah
**
** Revision 1.1.1.1  2001/04/27 07:03:54  neil
** initial
**
** Revision 1.2  2000/11/05 22:21:00  matt
** help me!
**
** Revision 1.1  2000/10/24 12:20:00  matt
** changed directory structure
**
** Revision 1.17  2000/10/10 13:58:18  matt
** stroustrup squeezing his way in the door
**
** Revision 1.16  2000/10/03 11:56:20  matt
** better support for optional sound ext routines
**
** Revision 1.15  2000/09/27 12:26:03  matt
** changed sound accumulators back to floats
**
** Revision 1.14  2000/09/15 13:38:40  matt
** changes for optimized apu core
**
** Revision 1.13  2000/09/15 04:58:07  matt
** simplifying and optimizing APU core
**
** Revision 1.12  2000/07/30 04:32:59  matt
** no more apu_getcyclerate hack
**
** Revision 1.11  2000/07/17 01:52:31  matt
** made sure last line of all source files is a newline
**
** Revision 1.10  2000/07/06 11:42:41  matt
** forgot to remove FDS register range
**
** Revision 1.9  2000/07/04 04:51:41  matt
** cleanups
**
** Revision 1.8  2000/07/03 02:18:53  matt
** much better external module exporting
**
** Revision 1.7  2000/06/20 04:06:16  matt
** migrated external sound definition to apu module
**
** Revision 1.6  2000/06/20 00:08:58  matt
** changed to driver based API
**
** Revision 1.5  2000/06/09 16:49:02  matt
** removed all floating point from sound generation
**
** Revision 1.4  2000/06/09 15:12:28  matt
** initial revision
**
*/
