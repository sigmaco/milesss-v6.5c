//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  MSSXMIDI.C: API module and support routines for XMIDI playback        ##
//##                                                                        ##
//##  Flat-model source compatible with IBM 32-bit ANSI C/C++               ##
//##                                                                        ##
//##  Version 1.00 of 12-Jul-94: Initial                                    ##
//##          1.01 of 11-Oct-94: Sequence volume preference enabled         ##
//##                             AIL_set_sequence_volume() works prior to   ##
//##                               starting sequence                        ##
//##                             Watcom/FlashTek support implemented        ##
//##          1.02 of 11-Nov-94: AIL_map_sequence() sets lock ownership     ##
//##                             All sequence note queues initialized       ##
//##                             Delay during end_sequence() to avoid stuck ##
//##                               notes on slower MPU401 devices           ##
//##          1.03 of  9-Feb-95: Use MEM_alloc_lock() to alloc state tables ##
//##          1.04 of  9-Jun-95: MIDI master volume added                   ##
//##                             XMIDI Channel Mute controller added        ##
//##                             Improved error reporting for INI loading   ##
//##                             Beat/bar callbacks added                   ##
//##                             Convert driver handles to HMDIDRIVER       ##
//##                             Borland/Zortech support added              ##
//##          1.05 of 19-Jun-95: Do not reset loop count at sequence start  ##
//##          1.06 of 25-Jun-95: Initialize EOS at allocation time          ##
//##          1.07 of 10-Aug-95: Exact TIMB chunk size used in copy         ##
//##                             Reset loop count to 1 at end of playback   ##
//##          1.08 of  7-Sep-95: Don't skip event after branch              ##
//##          1.09 of  7-Nov-95: Don't call EOS callback if already done    ##
//##          1.10 of 20-Feb-96: Updates for MSS 3.5                        ##
//##                             Don't dereference S->EVNT_ptr if seq done  ##
//##          1.11 of  2-Nov-97: Various changes made to support DLS        ##
//##                                                                        ##
//##  Author: John Miles                                                    ##
//##                                                                        ##
//############################################################################
//##                                                                        ##
//##  Copyright (C) RAD Game Tools, Inc.                                    ##
//##                                                                        ##
//##  Contact RAD Game Tools at 425-893-4300 for technical support.         ##
//##                                                                        ##
//############################################################################

#include <stdlib.h>
#include <limits.h>

#include "mss.h"

#include "imssapi.h"
//
// Channel lock status
//

#define UNLOCKED  0
#define LOCKED    1
#define PROTECTED 2

//
// Internal prototypes
//

static void cdecl XMI_flush_channel_notes(HSEQUENCE S, S32 channel);

//############################################################################
//##                                                                        ##
//## Locked static data                                                     ##
//##                                                                        ##
//############################################################################

//
// GTL filename prefix
//

static S8 GTL_prefix[128] = "FATMAN"; 

//
// XMI_serve()
//

static U32     entry = 0;
static HSEQUENCE S;
static S32      i,j,n,sequence_done;
static S32      q,t;
static U32     channel,status,type,len;
static U8 const *ptr;
static U8 const *event;

//############################################################################
//##                                                                        ##
//## Locked code                                                            ##
//##                                                                        ##
//############################################################################

#define LOCK(x)   AIL_vmm_lock  (&(x),sizeof(x))
#define UNLOCK(x) AIL_vmm_unlock(&(x),sizeof(x))

static S32 locked = 0;

void AILXMIDI_end(void);

void AILXMIDI_start(void)
{
   if (!locked)
      {
      AIL_vmm_lock_range(AILXMIDI_start, AILXMIDI_end);

      LOCK (GTL_prefix   );
      LOCK (entry        );
      LOCK (S            );
      LOCK (i            );
      LOCK (j            );
      LOCK (n            );
      LOCK (sequence_done);
      LOCK (q            );
      LOCK (t            );
      LOCK (channel      );
      LOCK (status       );
      LOCK (type         );
      LOCK (len          );
      LOCK (ptr          );
      LOCK (event        );

      locked = 1;
      }
}


//############################################################################
//##                                                                        ##
//## Return size in bytes of MIDI channel voice message, based on type      ##
//##                                                                        ##
//############################################################################

static S32 AILCALL XMI_message_size(S32 status)
{
   switch (status & 0xf0)
      {
      case EV_NOTE_OFF  :
      case EV_NOTE_ON   :
      case EV_POLY_PRESS:
      case EV_CONTROL   :
      case EV_PITCH     : return 3;

      case EV_PROGRAM   :
      case EV_CHAN_PRESS: return 2;
      }

   return 0;
}

//############################################################################
//##                                                                        ##
//## Force transmission of any buffered MIDI traffic                        ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_flush_buffer(HMDIDRIVER mdi)
{
   VDI_CALL VDI;

   if (mdi->message_count > 0)
      {
      VDI.CX = (S16) mdi->message_count;

      AIL_call_driver(mdi->drvr, MDI_MIDI_XMIT, &VDI, NULL);

      mdi->message_count = 0;
      mdi->offset        = 0;
      }
}

//############################################################################
//##                                                                        ##
//## Write channel voice message to MIDI driver buffer                      ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_MIDI_message(HMDIDRIVER  mdi, //)
                                   S32        status,
                                   S32        d1,
                                   S32        d2)
{
   U32 size,count;

   count = 1;

   // 
   // If requested, send every MIDI Note Off message twice to help
   // low-end MIDI interfaces like Sound Blasters get the message
   //

   if (AIL_preference[MDI_DOUBLE_NOTE_OFF])
      {
      if (((status & 0xf0) == EV_NOTE_OFF)  ||
         (((status & 0xf0) == EV_NOTE_ON) && (d2 == 0)))
           {
           count = 2;
           }
      }

   while (count--)
      {
      size = XMI_message_size(status);

      if ((mdi->offset + size) > sizeof(mdi->DST->MIDI_data))
         {
         XMI_flush_buffer(mdi);
         }

      mdi->DST->MIDI_data[mdi->offset++]    = (S8) (status & 0xff);
      mdi->DST->MIDI_data[mdi->offset++]    = (S8) (d1     & 0xff);

      if (size == 3)
         {
         mdi->DST->MIDI_data[mdi->offset++] = (S8) (d2     & 0xff);
         }

      ++mdi->message_count;
      }
}

//############################################################################
//##                                                                        ##
//## Write system exclusive message to MIDI driver buffer                   ##
//##                                                                        ##
//############################################################################

void cdecl XMI_sysex_message(HMDIDRIVER  mdi, //)
                                    U8 const      *message,
                                    S32        size)
{
   U8 const *ptr;

   XMI_flush_buffer(mdi);

   AIL_memcpy(mdi->DST->MIDI_data,
           message,
           min(sizeof(mdi->DST->MIDI_data),size));

   ++mdi->message_count;

   XMI_flush_buffer(mdi);

   //
   // Get # of bytes in VLN length specifier
   //

   ptr = (U8 *) message + 1;

   XMI_read_VLN(&ptr);

   //
   // Get size of message less VLN length
   //

   size = (size - ((U32) ptr - (U32) message)) + 1;

   //
   // Send to channel-voice message trap as series of SYSEX_BYTE pseudo-
   // control events
   //

   if (mdi->event_trap != NULL)
      {
      for (i=0; i < size; i++)
         {
         U8 val;
         U8 ch  = 0;

         if (i == 0)
            {
            val = message[0];
            }
         else
            {
            val = ptr[i-1];
            }

         //
         // Send any data bytes > 0x80 on channel 2 with high bit masked,
         // all others on channel 1
         //

         if (val > 0x80)
            {
            val &= 0x7f;
            ch = 1;
            }

         mdi->event_trap(mdi,NULL,EV_CONTROL | ch,SYSEX_BYTE,val);
         }
      }
}

//############################################################################
//##                                                                        ##
//## Convert 4-byte integer from big-to-little-endian form, and vice versa  ##
//##                                                                        ##
//## Used to interpret IFF file chunk length words                          ##
//##                                                                        ##
//############################################################################

static U32 AILCALL XMI_swap(U32 n)
{
   return ((n & 0x000000ff) << 24) +
          ((n & 0x0000ff00) << 8 ) +
          ((n & 0x00ff0000) >> 8 ) +
          ((n & 0xff000000) >> 24);
}


//############################################################################
//##                                                                        ##
//## Read control log value                                                 ##
//##                                                                        ##
//############################################################################

static S32 cdecl XMI_read_log(CTRL_LOG *log, S32 status, S32 data_1)
{
   S32 st;
   S32 ch;

   st = status & 0xf0;
   ch = status & 0x0f;

   switch (st)
      {
      case EV_PROGRAM:
         return log->program[ch];

      case EV_PITCH:
         return (log->pitch_h[ch] << 7) | log->pitch_l[ch];

      case EV_CONTROL:

         switch (data_1)
            {
            case CHAN_LOCK:
               return log->c_lock[ch];

            case CHAN_PROTECT:
               return log->c_prot[ch];

            case CHAN_MUTE:
               return log->c_mute[ch];

            case VOICE_PROTECT:
               return log->c_v_prot[ch];

            case PATCH_BANK_SEL:
               return log->bank[ch];

            case GM_BANK_LSB:
               return log->gm_bank_l[ch];

            case GM_BANK_MSB:
               return log->gm_bank_m[ch];

            case INDIRECT_C_PFX:
               return log->indirect[ch];

            case CALLBACK_TRIG:
               return log->callback[ch];

            case MODULATION:
               return log->mod[ch];

            case PART_VOLUME:
               return log->vol[ch];

            case PANPOT:
               return log->pan[ch];

            case EXPRESSION:
               return log->exp[ch];

            case SUSTAIN:
               return log->sus[ch];

            case REVERB:
               return log->reverb[ch];

            case CHORUS:
               return log->chorus[ch];

            case RPN_LSB:
               return log->RPN_L[ch];

            case RPN_MSB:
               return log->RPN_M[ch];

            case PB_RANGE:
               return log->bend_range[ch];

            default:
               return -1;
            }

      default:
         return -1;
      }
}

//############################################################################
//##                                                                        ##
//## Write control log value                                                ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_write_log(CTRL_LOG *log, S32 status, S32 data_1,   //)
                                S32 data_2)
{
   S32 st;
   S32 ch;

   st = status & 0xf0;
   ch = status & 0x0f;

   switch (st)
      {
      case EV_PROGRAM:
         log->program[ch] = (S8) data_1;
         break;

      case EV_PITCH:
         log->pitch_l[ch] = (S8) data_1;
         log->pitch_h[ch] = (S8) data_2;
         break;

      case EV_CONTROL:

         switch (data_1)
            {
            case CHAN_LOCK:
               log->c_lock[ch]     = (S8) data_2;
               break;

            case CHAN_PROTECT:
               log->c_prot[ch]     = (S8) data_2;
               break;

            case CHAN_MUTE:
               log->c_mute[ch]     = (S8) data_2;
               break;

            case VOICE_PROTECT:
               log->c_v_prot[ch]   = (S8) data_2;
               break;

            case PATCH_BANK_SEL:
               log->bank[ch]       = (S8) data_2;
               break;

            case GM_BANK_LSB:
               log->gm_bank_l[ch]  = (S8) data_2;
               break;

            case GM_BANK_MSB:
               log->gm_bank_m[ch]  = (S8) data_2;
               break;

            case INDIRECT_C_PFX:
               log->indirect[ch]   = (S8) data_2;
               break;

            case CALLBACK_TRIG:
               log->callback[ch]   = (S8) data_2;
               break;

            case MODULATION:
               log->mod[ch]        = (S8) data_2;
               break;

            case PART_VOLUME:
               log->vol[ch]        = (S8) data_2;
               break;

            case PANPOT:
               log->pan[ch]        = (S8) data_2;
               break;

            case EXPRESSION:
               log->exp[ch]        = (S8) data_2;
               break;

            case SUSTAIN:
               log->sus[ch]        = (S8) data_2;
               break;

            case REVERB: 
               log->reverb[ch]     = (S8) data_2;
               break;

            case CHORUS: 
               log->chorus[ch]     = (S8) data_2;
               break;

            case RPN_LSB:
               log->RPN_L[ch]    = (S8) data_2;
               break;

            case RPN_MSB:
               log->RPN_M[ch]    = (S8) data_2;
               break;

            case DATA_MSB:

               //
               // If current RPN is 0 0 (bender range), fall through to
               // log this MSB setting under the PB_RANGE pseudo-control
               //
               // Otherwise ignore it (other RPNs not supported by XMIDI
               // standard, although synthesizers may recognize 
               // them)
               //

               if ((log->RPN_L[ch] != 0) || (log->RPN_M[ch] != 0))
                  {
                  break;
                  }

            case PB_RANGE:
               log->bend_range[ch] = (S8) data_2;
               break;
            }
      }
}

//############################################################################
//##                                                                        ##
//## Send MIDI channel voice message associated with a specific sequence    ##
//##                                                                        ##
//## Includes controller logging and XMIDI extensions                       ##
//##                                                                        ##
//## Warnings: ICA_enable should be 0 when calling outside XMIDI event loop ##
//##           May be recursively called by XMIDI controller handlers       ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_send_channel_voice_message(HSEQUENCE S, //)
                                                 S32 status, 
                                                 S32 data_1,
                                                 S32 data_2,
                                                 S32 ICA_enable)
{
   S32       st,i;
   S32       phys,log;
   HMDIDRIVER mdi;

   //
   // Get driver for sequence
   //

   mdi = S->driver;

   //
   // Translate logical to physical channel
   //

   st  = status & 0xf0;
   log = status & 0x0f;

   phys = S->chan_map[log];

   //
   // If indirect controller override active, substitute indirect 
   // controller value for data_2, and cancel indirect override
   //

   if ((st == EV_CONTROL) &&
       (ICA_enable)       &&
       (S->shadow.indirect[log] != -1))
      {
      data_2 = S->shadow.indirect[log];

      S->shadow.indirect[log] = -1;
      }

   //
   // Update local MIDI status log
   //

   if ((st == EV_CONTROL) ||
       (st == EV_PROGRAM) ||
       (st == EV_PITCH  ))
      {
      XMI_write_log(&S->shadow,st | log,data_1,data_2);
      }

   //
   // If this is a Control Change event, handle special XMIDI controllers
   // and extended features
   //
   // Controller handlers should 'break' to pass message on to driver, or
   // 'return' if message is not to be transmitted
   //

   if (st == EV_CONTROL)
      {
      switch (data_1)
         {
         //
         // INDIRECT_C_PFX: Override value of next controller event 
         //                 with value from nth index in 
         //                 application's Indirect Controller Array
         //

         case INDIRECT_C_PFX:

            if (S->ICA)
               {
               S->shadow.indirect[log] = S->ICA[data_2];
               }
            break;

         //
         // CALLBACK_PFX:   Override value of next controller event
         //                 with value from user callback function
         //

         case CALLBACK_PFX:

            if (S->prefix_callback != NULL)
               {
               S->shadow.indirect[log] = S->prefix_callback(S, log, data_2);
               }
            break;

         //
         // PB_RANGE:       Control bender range by first sending RPN 0 0
         //

         case PB_RANGE:

            XMI_send_channel_voice_message(S,
                                           EV_CONTROL | log,
                                           RPN_LSB,
                                           0,
                                           0);

            XMI_send_channel_voice_message(S,
                                           EV_CONTROL | log,
                                           RPN_MSB,
                                           0,
                                           0);

            XMI_send_channel_voice_message(S,
                                           EV_CONTROL | log,
                                           DATA_LSB,
                                           0,
                                           0);
            break;

         //
         // PART_VOLUME:    Scale volume according to sequence's current
         //                 volume setting and overall driver master volume
         //

         case PART_VOLUME:

            data_2 = (data_2    *
                      S->volume *
                      mdi->master_volume) / (127*127);

            if (data_2 > 127)
               {
               data_2 = 127;
               }

            if (data_2 < 0)
               {
               data_2 = 0;
               }

            break;

         //
         // CLEAR_BEAT_BAR: Reset beat/bar count to 0:0, clear fraction,
         //                 and predecrement to compensate for current
         //                 interval
         //

         case CLEAR_BEAT_BAR:

            S->beat_count     = 0;
            S->measure_count  = 0;

            S->beat_fraction  = 0;
            S->beat_fraction -= S->time_fraction;

            //
            // If beat/bar callback function active, trigger it
            //

            if (S->beat_callback != NULL)
               {
               S->beat_callback(mdi, S, 0, 0);
               }

            return;

         //
         // CALLBACK_TRIG:  Call XMIDI user function, passing sequence
         //                 handle, channel #, and callback controller value
         //

         case CALLBACK_TRIG:

            if (S->trigger_callback != NULL)
               {
               S->trigger_callback(S, log, data_2);
               }

            return;

         //
         // FOR_LOOP:       Mark the start of an XMIDI FOR...NEXT/BREAK loop
         //
         //                  1-127: Play n iterations
         //                      0: Play indefinitely
         //

         case FOR_LOOP:

            //
            // Find first available FOR loop entry
            //

            for (i=0; i < FOR_NEST; i++)
               {
               if (S->FOR_loop_count[i] == -1)
                  {
                  break;
                  }
               }

            //
            // If none available, ignore controller -- else set loop pointer
            // and count
            //

            if (i == FOR_NEST)
               {
               return;
               }

            S->FOR_loop_count [i] = data_2;
            S->FOR_ptrs       [i] = S->EVNT_ptr;

            return;

         //
         // NEXT_LOOP:      Mark the end of an XMIDI FOR...NEXT/BREAK loop
         //
         //                 64-127: Continue looping until FOR count reached
         //                   0-63: Break from current loop
         //

         case NEXT_LOOP:

            //
            // Otherwise, find innermost (most recent) FOR loop
            //

            for (i=FOR_NEST-1; i >= 0; i--)
               {
               if (S->FOR_loop_count[i] != -1)
                  {
                  break;
                  }
               }

            //
            // Break out of loop if value < 64
            //

            if (data_2 < 64)
               {
               S->FOR_loop_count[i] = -1;
               return;
               }

            //
            // If no FOR loops active, ignore controller
            //

            if (i == -1)
               {
               return;
               }

            //
            // If loop count == 0, loop indefinitely
            //

            if (S->FOR_loop_count[i] == 0)
               {
               S->EVNT_ptr = S->FOR_ptrs[i];
               return;
               }

            //
            // Otherwise, decrement loop count and, if the result is not
            // zero, loop back to FOR controller's location
            //
            // When loop finishes, set loop count to -1 to indicate
            // availability of FOR loop entry
            //

            if (--S->FOR_loop_count[i] != 0)
               {
               S->EVNT_ptr = S->FOR_ptrs[i];
               }
            else
               {
               S->FOR_loop_count[i] = -1;
               }

            return;

         //
         // SEQ_BRANCH:     Branch immediately to specified Sequence Branch
         //                 Index point
         //

         case SEQ_BRANCH:

            AIL_branch_index(S,data_2);
            return;

         //
         // CHAN_PROTECT:   Protect physical channel from being locked by 
         //                 API or another sequence          
         //
         //                 64-127: Enable lock protection
         //                   0-63: Disable lock protection
         //

         case CHAN_PROTECT:

            //
            // If channel is already locked, it's too late to protect it
            //

            if (mdi->lock[phys] == LOCKED)
               {
               return;
               }

            //
            // Otherwise, set UNLOCKED (by implication, UNPROTECTED)
            // or PROTECTED
            //

            if (data_2 < 64)
               {
               mdi->lock[phys] = UNLOCKED;
               }
            else
               {
               mdi->lock[phys] = PROTECTED;
               }

            return;

         //
         // CHAN_LOCK:      Lock/unlock physical channel for use by this
         //                 sequence's logical channel
         //
         //                 64-127: Search for and lock physical channel
         //                   0-63: Release physical channel to prior user
         //

         case CHAN_LOCK:

            if (data_2 >= 64)
               {
               //
               // Channel cannot be redundantly locked
               //

               if (mdi->lock[phys] == LOCKED)
                  {
                  return;
                  }

               //
               // Lock a physical channel (1-based), if possible
               //

               i = AIL_lock_channel(mdi);

               if (!i)
                  {
                  return;
                  }

               //
               // Map sequence channel (0-based) to locked physical 
               // channel (1-based)
               //

               AIL_map_sequence_channel(S,log+1,i);

               //
               // Keep track of which sequence locked the channel, so
               // other sequences can be inhibited from writing to it
               //

               mdi->locker[i-1] = S;
               }
            else
               {
               //
               // Channel must be locked in order to release it
               //

               if (mdi->lock[phys] != LOCKED)
                  {
                  return;
                  }

               //
               // Turn all notes off in channel
               //

               XMI_flush_channel_notes(S,log);

               //
               // Release locked physical channel (1-based)
               //

               AIL_release_channel(mdi,phys+1);

               //
               // Re-establish normal physical channel mapping 
               // for logical channel
               // 

               AIL_map_sequence_channel(S,log+1,log+1);
               }

            return;
         }
      }

   //
   // If this physical channel is locked by the API or by another 
   // sequence, return
   //

   if ((mdi->lock[phys] == LOCKED) && (mdi->locker[phys] != S))
      {
      return;
      }

   //
   // Keep track of overall physical channel note counts   
   //

   if (st == EV_NOTE_ON)
      {
      ++mdi->notes[phys];
      }
   else if (st == EV_NOTE_OFF)
      {
      --mdi->notes[phys];
      }

   //
   // Keep track of most recent sequence to use channel
   //

   mdi->user[phys] = S;

   //
   // If logical channel muted with XMIDI Channel Mute controller (107), 
   // return without transmitting note-on events
   //

   if ((st == EV_NOTE_ON)
         &&
       (S->shadow.c_mute[log] >= 64))
      {
      return;
      }

   //
   // Allow application a chance to process the event...
   //

   if (mdi->event_trap != NULL)
      {
      if (mdi->event_trap(mdi,S,st | phys,data_1,data_2))
         {
         return;
         }
      }

   //
   // ...otherwise, transmit message to driver
   //

   XMI_MIDI_message(mdi,st | phys,data_1,data_2);
}

//############################################################################
//##                                                                        ##
//## Flush sequence note queue                                              ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_flush_note_queue(HSEQUENCE S)
{
   S32 i,nmsgs;

   nmsgs = 0;

   for (i=0; i < MAX_NOTES; i++)
      {
      if (S->note_chan[i] == -1)
         {
         continue;
         }

      //
      // Send MIDI Note Off message
      //

      XMI_send_channel_voice_message(S,
                                     S->note_chan[i] | EV_NOTE_OFF,
                                     S->note_num [i],
                                     0,
                                     0);
      //
      // Release queue entry and increment "note off" count
      //

      S->note_chan[i] = -1;

      nmsgs++;
      }

   S->note_count = 0;

   XMI_flush_buffer(S->driver);

   //
   // If any messages were sent, delay before returning to give 
   // slower MPU-401 devices enough time to process MIDI data
   //

   if ((nmsgs) && (!AIL_background()))
      {
      AIL_delay(3);
      }
}

//############################################################################
//##                                                                        ##
//## Flush notes in one channel only                                        ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_flush_channel_notes(HSEQUENCE S, S32 channel)
{
   S32 i;

   for (i=0; i < MAX_NOTES; i++)
      {
      if (S->note_chan[i] != channel)
         {
         continue;
         }

      //
      // Send MIDI Note Off message
      //

      XMI_send_channel_voice_message(S,
                                     S->note_chan[i] | EV_NOTE_OFF,
                                     S->note_num [i],
                                     0,
                                     0);
      //
      // Release queue entry
      //

      S->note_chan[i] = -1;
      }

   XMI_flush_buffer(S->driver);
}

//############################################################################
//##                                                                        ##
//## Transmit logged channel controller values                              ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_refresh_channel(HSEQUENCE S, S32 ch)
{
   //
   // Set bank and patch values first ...
   //

   if (S->shadow.gm_bank_l[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     GM_BANK_LSB,
                                     S->shadow.gm_bank_l[ch],
                                     0);
      }

   if (S->shadow.gm_bank_m[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     GM_BANK_MSB,
                                     S->shadow.gm_bank_m[ch],
                                     0);
      }

   if (S->shadow.bank[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     PATCH_BANK_SEL,
                                     S->shadow.bank[ch],
                                     0);
      }

   if (S->shadow.program[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_PROGRAM | ch,
                                     S->shadow.program[ch],
                                     0,
                                     0);
      }

   //
   // ... followed by pitch bender ...
   //

   if (S->shadow.pitch_h[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_PITCH | ch,
                                     S->shadow.pitch_l[ch],
                                     S->shadow.pitch_h[ch],
                                     0);
      }

   //
   // ... followed by controller events
   //

   if (S->shadow.c_mute[ch] != -1)                           
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     CHAN_MUTE,
                                     S->shadow.c_mute[ch],
                                     0);
      }

   if (S->shadow.c_prot[ch] != -1)                           
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     CHAN_PROTECT,
                                     S->shadow.c_prot[ch],
                                     0);
      }

   if (S->shadow.c_v_prot[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     VOICE_PROTECT,
                                     S->shadow.c_v_prot[ch],
                                     0);
      }

   if (S->shadow.mod[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     MODULATION,
                                     S->shadow.mod[ch],
                                     0);
      }

   if (S->shadow.vol[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     PART_VOLUME,
                                     S->shadow.vol[ch],
                                     0);
      }

   if (S->shadow.pan[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     PANPOT,
                                     S->shadow.pan[ch],
                                     0);
      }

   if (S->shadow.exp[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     EXPRESSION,
                                     S->shadow.exp[ch],
                                     0);
      }

   if (S->shadow.sus[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     SUSTAIN,
                                     S->shadow.sus[ch],
                                     0);
      }

   if (S->shadow.reverb[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     REVERB,
                                     S->shadow.reverb[ch],
                                     0);
      }

   if (S->shadow.chorus[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     CHORUS,
                                     S->shadow.chorus[ch],
                                     0);
      }

   if (S->shadow.bend_range[ch] != -1)
      {
      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     PB_RANGE,
                                     S->shadow.bend_range[ch],
                                     0);
      }

   //
   // Disregard callback member -- present only for use with
   // AIL_controller_value()
   //
}

//############################################################################
//##                                                                        ##
//## Update all channels in sequence based on differences between two state ##
//## tables                                                                 ##
//##                                                                        ##
//## Controllers with a different value will be updated; upon exit, the     ##
//## contents of the original state table will be identical to those of the ##
//## updated version                                                        ##
//##                                                                        ##
//############################################################################

static void XMI_update_sequence(HSEQUENCE S, //)
                                CTRL_LOG *original,
                                CTRL_LOG *updated)
{
   S32 ch;

   for (ch = MIN_CHAN; ch <= MAX_CHAN; ch++)
      {
      //
      // Set bank and patch values first ...
      //

      if ((updated->gm_bank_l[ch] != original->gm_bank_l[ch]) &&
          (updated->gm_bank_l[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       GM_BANK_LSB,
                                       updated->gm_bank_l[ch],
                                       0);
         }

      if ((updated->gm_bank_m[ch] != original->gm_bank_m[ch]) &&
          (updated->gm_bank_m[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       GM_BANK_MSB,
                                       updated->gm_bank_m[ch],
                                       0);
         }

      if ((updated->bank[ch] != original->bank[ch]) &&
          (updated->bank[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       PATCH_BANK_SEL,
                                       updated->bank[ch],
                                       0);
         }

      if ((updated->program[ch] != original->program[ch]) &&
          (updated->program[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_PROGRAM | ch,
                                       updated->program[ch],
                                       0,
                                       0);
         }

      //
      // ... followed by pitch bender ...
      //

      if ((updated->pitch_h[ch] != original->pitch_h[ch]) &&
          (updated->pitch_h[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_PITCH | ch,
                                       updated->pitch_l[ch],
                                       updated->pitch_h[ch],
                                       0);
         }

      //
      // ... followed by controller events
      //

      if ((updated->c_mute[ch] != original->c_mute[ch]) &&
          (updated->c_mute[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       CHAN_MUTE,
                                       updated->c_mute[ch],
                                       0);
         }

      if ((updated->c_prot[ch] != original->c_prot[ch]) &&
          (updated->c_prot[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       CHAN_PROTECT,
                                       updated->c_prot[ch],
                                       0);
         }

      if ((updated->c_v_prot[ch] != original->c_v_prot[ch]) &&
          (updated->c_v_prot[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       VOICE_PROTECT,
                                       updated->c_v_prot[ch],
                                       0);
         }

      if ((updated->mod[ch] != original->mod[ch]) &&
          (updated->mod[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       MODULATION,
                                       updated->mod[ch],
                                       0);
         }

      if ((updated->vol[ch] != original->vol[ch]) &&
          (updated->vol[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       PART_VOLUME,
                                       updated->vol[ch],
                                       0);
         }

      if ((updated->pan[ch] != original->pan[ch]) &&
          (updated->pan[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       PANPOT,
                                       updated->pan[ch],
                                       0);
         }

      if ((updated->exp[ch] != original->exp[ch]) &&
          (updated->exp[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       EXPRESSION,
                                       updated->exp[ch],
                                       0);
         }

      if ((updated->sus[ch] != original->sus[ch]) &&
          (updated->sus[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       SUSTAIN,
                                       updated->sus[ch],
                                       0);
         }

      if ((updated->reverb[ch] != original->reverb[ch]) &&
          (updated->reverb[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       REVERB,
                                       updated->reverb[ch],
                                       0);
         }

      if ((updated->chorus[ch] != original->chorus[ch]) &&
          (updated->chorus[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       CHORUS,
                                       updated->chorus[ch],
                                       0);
         }

      if ((updated->bend_range[ch] != original->bend_range[ch]) &&
          (updated->bend_range[ch] != -1))
         {
         XMI_send_channel_voice_message(S,
                                       EV_CONTROL | ch,
                                       PB_RANGE,
                                       updated->bend_range[ch],
                                       0);
         }

      //
      // Create delay if not in background, to keep from overflowing hardware
      // FIFOs
      //

      if ((!(ch & 3)) && (!AIL_background()))
         {
         AIL_delay(1);
         }
      }
}

//############################################################################
//##                                                                        ##
//## Initialize state table entries                                         ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_init_sequence_state(HSEQUENCE S)
{
   S32 i;

   //
   // Initialize logical-physical channel map to identity set
   //

   for (i=0; i < NUM_CHANS; i++)
      {
      S->chan_map[i] = i;
      }

   //
   // Initialize all logged controllers to -1
   //

   AIL_memset(&S->shadow,-1,sizeof(CTRL_LOG));

   //
   // Initialize FOR loop counters
   //

   for (i=0; i < FOR_NEST; i++)
      {
      S->FOR_loop_count[i] = -1;
      }

   //
   // Initialize note queue
   // 

   for (i=0; i < MAX_NOTES; i++)
      {
      S->note_chan[i] = -1;
      }

   S->note_count = 0;

   //
   // Initialize timing variables
   //
   // Default to 4/4 time at 120 beats/minute
   //

   S->interval_count =  0;

   S->beat_count     =  0;
   S->measure_count  = -1;

   S->beat_fraction  =  0;
   S->time_fraction  =  0;

   S->time_numerator =  4;

   S->time_per_beat  =  500000*16;

   S->interval_num   =  0;
}

//############################################################################
//##                                                                        ##
//## Reset sequence pointers and initialize state table entries             ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_rewind_sequence(HSEQUENCE S)
{
   //
   // Initialize sequence state table
   //

   XMI_init_sequence_state(S);

   //
   // Initialize event pointer to start of XMIDI EVNT chunk data
   //

   S->EVNT_ptr = (U8 *) S->EVNT + 8;
}

//############################################################################
//##                                                                        ##
//## Send updated volume control messages to all channels in sequence       ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_update_volume(HSEQUENCE S)
{
   S32 ch;

   for (ch=0; ch < NUM_CHANS; ch++)
      {
      //
      // Skip channels with no volume controller history
      //

      if (S->shadow.vol[ch] == -1)
         {
         continue;
         }

      //
      // Retransmit volume values to permit volume scaling
      //

      XMI_send_channel_voice_message(S,
                                     EV_CONTROL | ch,
                                     PART_VOLUME,
                                     S->shadow.vol[ch],
                                     0);
      }
}

//############################################################################
//##                                                                        ##
//## Timer interrupt routine for XMIDI sequencing                           ##
//##                                                                        ##
//############################################################################

void AILCALLBACK XMI_serve(U32 mdival)
{

   HMDIDRIVER mdi=(HMDIDRIVER)mdival;

   //
   // Exit at once if service disabled
   //

   if (mdi->disable)
      {
      return;
      }

   //
   // Disallow re-entrant calls (but leave interrupts enabled so that
   // .DIG processing can run in a separate thread)
   //

   if (entry)
      {
      return;
      }

   entry = 1;

   //
   // Process all active sequences
   //

   for (n = mdi->n_sequences,S = &mdi->sequences[0]; n; --n,++S)
      {
      //
      // Skip sequence if stopped, finished, or not allocated
      //

      if (S->status != SEQ_PLAYING)
         {
         continue;
         }

      sequence_done = 0;

      //
      // Bump sequence interval number counter
      //

      ++S->interval_num;

      //
      // Add tempo percent to tempo overflow counter
      //

      S->tempo_error += S->tempo_percent;

      //
      // Execute interval zero, one, or more times, depending on tempo DDA
      // count
      //

      while (S->tempo_error >= 100)
         {
         S->tempo_error -= 100;

         //
         // Decrement note times and turn off any expired notes
         //

         if (S->note_count > 0)
            {
            for (i=0; i < MAX_NOTES; i++)
               {
               if (S->note_chan[i] == -1)
                  {
                  continue;
                  }

               if (--S->note_time[i] > 0)
                  {
                  continue;
                  }

               //
               // Note expired -- send MIDI Note Off message
               //

               XMI_send_channel_voice_message(S,
                                              S->note_chan[i] | EV_NOTE_OFF,
                                              S->note_num [i],
                                              0,
                                              0);
               //
               // Release queue entry, decrement sequence note count,
               // and exit loop if no active sequence notes left
               //

               S->note_chan[i] = -1;

               if (--S->note_count == 0)
                  {
                  break;
                  }
               }
            }

         //
         // Decrement interval delta-time count and process next interval if
         // ready
         //

         if (--S->interval_count <= 0)
            {
            //
            // Fetch events until next interval's delta-time byte (< 0x80)
            //

            while ((!sequence_done) && ((status = *S->EVNT_ptr) >= 0x80))
               {
               switch (status)
                  {
                  //
                  // Process MIDI meta-event
                  //

                  case EV_META:

                     type = *(S->EVNT_ptr+1);

                     S->EVNT_ptr += 2;
                     len = XMI_read_VLN(&S->EVNT_ptr);

                     switch (type)
                        {
                        //
                        // End-of-track event (XMIDI end-of-sequence)
                        //

                        case META_EOT:

                           //
                           // Set sequence_done to inhibit post-interval
                           // processing

                           sequence_done = 1;

                           //
                           // If loop count == 0, loop indefinitely
                           //
                           // Otherwise, decrement loop count and, if the
                           // result is not zero, return to beginning of
                           // sequence
                           //

                           if ((S->loop_count == 0)
                              ||
                              (--S->loop_count != 0))
                              {
                              S->EVNT_ptr = (U8 *) S->EVNT + 8;

                              S->beat_count    =  0;
                              S->measure_count = -1;
                              S->beat_fraction =  0;

                              //
                              // If beat/bar callback function active, 
                              // trigger it
                              //

                              if (S->beat_callback != NULL)
                                 {
                                 S->beat_callback(S->driver, S, 0, 0);
                                 }

                              break;
                              }

                           //
                           // Otherwise, stop sequence and set status 
                           // to SEQ_DONE
                           //
                           // Reset loop count to 1, to enable unlooped replay
                           //

                           S->loop_count = 1;

                           AIL_stop_sequence(S);
                           S->status = SEQ_DONE;

                           //
                           // Invoke end-of-sequence callback function, if any
                           //

                           if (S->EOS != NULL)
                              {
                              S->EOS(S);
                              }

                           break;

                        //
                        // Tempo event
                        //

                        case META_TEMPO:

                           //
                           // Calculate tempo as 1/16-uS per MIDI 
                           // quarter-note
                           //

                           t = ((S32) *(S->EVNT_ptr  ) << 16) +
                               ((S32) *(S->EVNT_ptr+1) << 8 ) +
                               ((S32) *(S->EVNT_ptr+2)      );

                           S->time_per_beat = t * 16;

                           break;

                        //
                        // Time signature event
                        //

                        case META_TIME_SIG:

                           //
                           // Fetch time numerator
                           //

                           S->time_numerator = *S->EVNT_ptr;

                           //
                           // Fetch time denominator: 0 = whole note, 
                           // 1 = half-note, 2 = quarter-note, 3 = eighth-note...
                           //

                           t = *(S->EVNT_ptr+1) - 2;

                           //
                           // Calculate beat period in terms of quantization
                           // rate
                           //

                           q = 16000000L / AIL_preference[MDI_SERVICE_RATE];

                           if (t < 0)
                              {
                              t = -t;

                              S->time_fraction = (q >> t);
                              }
                           else
                              {
                              S->time_fraction = (q << t);
                              }

                           //
                           // Predecrement beat fraction for this interval;
                           // signal beginning of new measure
                           //

                           S->beat_fraction  = 0;
                           S->beat_fraction -= S->time_fraction;

                           S->beat_count     = 0;
                           S->measure_count++;

                           //
                           // If beat/bar callback function active, 
                           // trigger it
                           //

                           if (S->beat_callback != NULL)
                              {
                              S->beat_callback(S->driver, S, S->beat_count, S->measure_count);
                              }

                           break;
                        }

                     S->EVNT_ptr += len;
                     break;

                  //
                  // Process MIDI System Exclusive message
                  //

                  case EV_SYSEX:
                  case EV_ESC:

                     //
                     // Read message length and copy data to buffer
                     //

                     ptr = (U8 *) S->EVNT_ptr + 1;

                     len = XMI_read_VLN(&ptr);

                     len += (S32) ((U32) ptr - (U32) S->EVNT_ptr);

                     XMI_sysex_message(mdi, S->EVNT_ptr, len);

                     S->EVNT_ptr += len;
                     break;

                  //
                  // Process MIDI channel voice message
                  //

                  default:

                     event   = S->EVNT_ptr;
                     channel = status & 0x0f;
                     status  = status & 0xf0;

                     //
                     // Transmit message with ICA override enabled
                     //

                     XMI_send_channel_voice_message(S,
                                                   *S->EVNT_ptr,
                                                  *(S->EVNT_ptr+1),
                                                  *(S->EVNT_ptr+2),
                                                    1);
                     //
                     // Index next event
                     //
                     // Allocate note queue entries for Note On messages
                     //

                     if (status != EV_NOTE_ON)
                        {
                        //
                        // If this was a control change event which caused
                        // a branch to take place -- either a FOR/NEXT
                        // controller or a user callback with an API branch
                        // call -- then don't skip the current event
                        //

                        if (event == S->EVNT_ptr)
                           {
                           S->EVNT_ptr += XMI_message_size(*S->EVNT_ptr);
                           }
                        }
                     else
                        {
                        //
                        // Find free note queue entry
                        //

                        for (i=0; i < MAX_NOTES; i++)
                           {
                           if (S->note_chan[i] == -1)
                              {
                              break;
                              }
                           }

                        //
                        // Shut down sequence if note queue overflows 
                        //
                        // Should never happen since excessive polyphony is 
                        // trapped by MIDIFORM
                        //

                        if (i == MAX_NOTES)
                           {
                           AIL_set_error("Internal note queue overflow.");

                           AIL_stop_sequence(S);
                           S->status = SEQ_DONE;

                           entry = 0;
                           return;
                           }

                        //
                        // Increment sequence-based note counter
                        //
                        // Record note's channel, number, and duration
                        //

                        ++S->note_count;

                        S->note_chan[i] = channel;
                        S->note_num [i] = *(S->EVNT_ptr+1);

                        S->EVNT_ptr    += 3;
                        S->note_time[i] = XMI_read_VLN(&S->EVNT_ptr);
                        }

                     break;
                  }
               }

            //
            // Terminate this interval and set delta-time count to skip 
            // next 0-127 intervals
            //

            if (!sequence_done)
               {
               S->interval_count = *S->EVNT_ptr++;
               }
            }

         if (!sequence_done)
            {
            //
            // Advance beat/bar counters
            //

            S->beat_fraction += S->time_fraction;

            if (S->beat_fraction >= S->time_per_beat)
               {
               S->beat_fraction -= S->time_per_beat;
               ++S->beat_count;

               if (S->beat_count >= S->time_numerator)
                  {
                  S->beat_count = 0;
                  ++S->measure_count;
                  }

               //
               // If beat/bar callback function active, trigger it
               //

               if (S->beat_callback != NULL)
                  {
                  AIL_sequence_position(S, &i, &j);
                  S->beat_callback(S->driver, S, i, j);
                  }
               }
            }
         }

      if (!sequence_done)
         {
         //
         // Update volume ramp, if any
         //

         if (S->volume != S->volume_target)
            {
            S->volume_accum += S->driver->interval_time;

            while (S->volume_accum >= S->volume_period)
               {
               S->volume_accum -= S->volume_period;

               if (S->volume_target > S->volume)
                  {
                  ++S->volume;
                  }
               else
                  {
                  --S->volume;
                  }

               if (S->volume == S->volume_target)
                  {
                  break;
                  }
               }

            //
            // Update volume controllers once every 8 intervals
            // to avoid generating excessive MIDI traffic
            //

            if (!(S->interval_num & 0x07))
               {
               XMI_update_volume(S);
               }
            }

         //
         // Update tempo ramp, if any
         //

         if (S->tempo_percent != S->tempo_target)
            {
            S->tempo_accum += S->driver->interval_time;

            while (S->tempo_accum >= S->tempo_period)
               {
               S->tempo_accum -= S->tempo_period;

               if (S->tempo_target > S->tempo_percent)
                  {
                  ++S->tempo_percent;
                  }
               else
                  {
                  --S->tempo_percent;
                  }

               if (S->tempo_percent == S->tempo_target)
                  {
                  break;
                  }
               }
            }
         }
      }

   //
   // Flush MIDI event buffer at least once every timer tick
   //

   XMI_flush_buffer(mdi);

   entry = 0;
}

//############################################################################
//##                                                                        ##
//## Call device I/O verification function using current detection policy   ##
//##                                                                        ##
//############################################################################

static S32 cdecl XMI_attempt_MDI_detection(HMDIDRIVER mdi, IO_PARMS *IO)    
{
   IO_PARMS *f;
   IO_PARMS  try;
   U32     i;

   //
   // Set up working IO_PARMS structure
   //

   try = *IO;

   //
   // If any needed parameters are not specified, use parameter
   // values from first factory-default IO structure
   // 

   if (mdi->drvr->VHDR->num_IO_configurations > 0)
      {
      f = (IO_PARMS *) (REALPTR(mdi->drvr->VHDR->common_IO_configurations));

      if ((S32) try.IO < 1)
         {
         try.IO = f->IO;
         }

      if ((S32) try.IRQ < 1)
         {
         try.IRQ = f->IRQ;
         }

      if ((S32) try.DMA_8_bit < 1)
         {
         try.DMA_8_bit = f->DMA_8_bit;
         }

      if ((S32) try.DMA_16_bit < 1)
         {
         try.DMA_16_bit = f->DMA_16_bit;
         }

      for (i=0; i < 4; i++)
         {
         if ((S32) try.IO_reserved[i] < 1)
            {
            try.IO_reserved[i] = f->IO_reserved[i];
            }
         }
      }

   //
   // Copy IO parameter block to driver
   //

   mdi->drvr->VHDR->IO = try;

   //
   // Call detection function
   // 

   return AIL_call_driver(mdi->drvr, DRV_VERIFY_IO, NULL, NULL);
}

//############################################################################
//##                                                                        ##
//## Uninstall XMIDI audio driver, freeing all allocated resources          ##
//##                                                                        ##
//## This function is called via the AIL_DRIVER.destructor vector only      ##
//##                                                                        ##
//############################################################################

static void cdecl XMI_destroy_MDI_driver(HMDIDRIVER mdi)
{
   S32 i;

   //
   // Stop all playing sequences to avoid hung notes
   //

   for (i=0; i < mdi->n_sequences; i++)
      {
      AIL_end_sequence(&mdi->sequences[i]);
      }

   //
   // Stop sequencer timer service
   // 

   AIL_release_timer_handle(mdi->timer);

   //
   // Release memory resources
   //

   AIL_mem_free_lock(mdi->sequences);
   AIL_mem_free_lock(mdi);
}

//############################################################################
//##                                                                        ##
//## Install and initialize XMIDI audio driver                              ##
//##                                                                        ##
//############################################################################

static HMDIDRIVER cdecl XMI_construct_MDI_driver(AIL_DRIVER *drvr, IO_PARMS *IO)
{
   IO_PARMS    use;
   S32        i;
   S32        detected;
   VDI_CALL    VDI;
   HMDIDRIVER  mdi;
   char     *envname;
   char     *envval;

   //
   // Ensure that all AILXMIDI code and data is locked into memory
   //

   AILXMIDI_start();

   //
   // Allocate memory for MDI_DRIVER structure
   //

   mdi = AIL_mem_alloc_lock(sizeof(MDI_DRIVER));

   if (mdi == NULL)
      {
      AIL_set_error("Could not allocate memory for driver.");

      return NULL;
      }

   AIL_memcpy(mdi->tag,"HMDI",4);

   mdi->drvr = drvr;

   //
   // Reject driver if not of type .MDI
   //

   if (mdi->drvr->type != AIL3MDI)
      {
      AIL_set_error(".MDI driver required.");

      AIL_mem_free_lock(mdi);

      return NULL;
      }

   //
   // Get DDT and DST addresses
   //

   AIL_call_driver(mdi->drvr, DRV_GET_INFO, NULL, &VDI);

   mdi->DDT = (void *) (((U32) ((U16) VDI.DX) << 4) + (U32) (U16) VDI.AX);
   mdi->DST = (void *) (((U32) ((U16) VDI.CX) << 4) + (U32) (U16) VDI.BX);

#ifdef _X32_H_INCLUDED

   mdi->DDT = (void *) ((U32) mdi->DDT + (U32) _x32_zero_base_ptr);
   mdi->DST = (void *) ((U32) mdi->DST + (U32) _x32_zero_base_ptr);

#else

#ifdef __BORLANDC__

   mdi->DDT = (void *) ((U32) mdi->DDT - AIL_sel_base(_DS));
   mdi->DST = (void *) ((U32) mdi->DST - AIL_sel_base(_DS));

#endif

#endif

   //
   // Copy library environment string to DST, if requested
   //

   envname = REALPTR(mdi->DDT->library_environment);

   if (!((envname == NULL) || (envname[0] == 0)))
      {
      envval = AIL_getenv(envname);
      
      if (!((envval == NULL) || (envval[0] == 0)))
         {
         i=AIL_strlen(envval);
         if (i>127)
           i=127;
         AIL_memcpy(mdi->DST->library_directory, envval, i);

         mdi->DST->library_directory[i]=0;
         }   
      }

   //
   // Copy AIL 2.X GTL filename to DST, if valid
   //
   // (By default, AIL applications use SAMPLE.XXX GTL filenames)
   //

   envname = REALPTR(mdi->DDT->GTL_suffix);

   if (!((envname == NULL) || (envname[0] == 0)))
      {
      AIL_strcpy(mdi->DST->GTL_filename,GTL_prefix);
      AIL_strcat(mdi->DST->GTL_filename,envname);
      }
   else
      {
      mdi->DST->GTL_filename[0] = 0;
      }

   //
   // Verify hardware I/O parameters
   //

   AIL_memset(&AIL_last_IO_attempt,-1,sizeof(IO_PARMS));

   //
   // If explicit IO_PARMS structure provided by application, try it
   // first
   //

   detected = 0;

   if (IO != NULL)
      {
      AIL_last_IO_attempt = *IO;

      if (XMI_attempt_MDI_detection(mdi,IO))
         {
         detected = 1;

         use = *IO;
         }
      }

   //
   // Next, try device-specific environment string (if applicable)
   //

   if (!detected)
      {
      IO = AIL_get_IO_environment(mdi->drvr);

      if (IO != NULL)
         {
         AIL_last_IO_attempt = *IO;

         if (XMI_attempt_MDI_detection(mdi,IO))
            {
            detected = 1;

            use = *IO;
            }
         }
      }

   //
   // Finally, try all common_IO_configurations[] entries in driver
   //

   if ((!detected) && (AIL_preference[AIL_SCAN_FOR_HARDWARE] == YES))
      {
      for (i=0; i < mdi->drvr->VHDR->num_IO_configurations; i++)
         {
         IO = &((IO_PARMS *)
                (REALPTR(mdi->drvr->VHDR->common_IO_configurations)))[i];

         if (i==0)
            {
            AIL_last_IO_attempt = *IO;
            }

         if (XMI_attempt_MDI_detection(mdi,IO))
            {
            detected = 1;

            use = *IO;

            break;
            }
         }
      }

   //
   // If all detection attempts failed, return NULL
   //

   if (detected)
      {
      AIL_last_IO_attempt = use;
      }
   else
      {
      AIL_set_error("XMIDI sound hardware not found.");

      AIL_mem_free_lock(mdi);

      return NULL;
      }

   //
   // Initialize device
   //

   AIL_call_driver(mdi->drvr, DRV_INIT_DEV, NULL, NULL);

   mdi->drvr->initialized = 1;

   //
   // Initialize instrument manager
   //

   AIL_call_driver(mdi->drvr, MDI_INIT_INS_MGR, NULL, &VDI);

   if (!VDI.AX)
      {
      AIL_set_error("Could not initialize instrument manager.");

      AIL_call_driver(mdi->drvr, DRV_SHUTDOWN_DEV, NULL, NULL);

      mdi->drvr->initialized = 0;

      AIL_mem_free_lock(mdi);

      return NULL;
      }

   //
   // Allocate SEQUENCE structures for driver
   //

   mdi->n_sequences = AIL_preference[MDI_SEQUENCES];

   mdi->sequences = AIL_mem_alloc_lock(sizeof(SEQUENCE) * mdi->n_sequences);

   if (mdi->sequences == NULL)
      {
      AIL_set_error("Could not allocate SEQUENCE structures.");

      AIL_call_driver(mdi->drvr, DRV_SHUTDOWN_DEV, NULL, NULL);

      mdi->drvr->initialized = 0;

      AIL_mem_free_lock(mdi);

      return NULL;
      }

   for (i=0; i < mdi->n_sequences; i++)
      {
      AIL_memcpy(mdi->sequences[i].tag,"HSEQ",4);

      mdi->sequences[i].status = SEQ_FREE;
      mdi->sequences[i].driver = mdi;
      }

   //
   // Initialize miscellaneous MDI_DRIVER variables
   //

   mdi->event_trap    = NULL;
   mdi->timbre_trap   = NULL;

   mdi->message_count = 0;
   mdi->offset        = 0;

   mdi->interval_time = 1000000L / AIL_preference[MDI_SERVICE_RATE];

   mdi->disable       = 0;

   mdi->master_volume = 127;

   //
   // Initialize channel lock table to NULL (all physical channels
   // available)
   //

   for (i=0; i < NUM_CHANS; i++)
      {
      mdi->lock  [i] = UNLOCKED;
      mdi->locker[i] = NULL;
      mdi->owner [i] = NULL;
      mdi->user  [i] = NULL;
      mdi->state [i] = UNLOCKED;
      mdi->notes [i] = 0;
      }

   //
   // Allocate timer for XMIDI sequencing
   //

   mdi->timer = AIL_register_timer(XMI_serve);

   if (mdi->timer == -1)
      {
      AIL_set_error("Out of timer handles.");

      AIL_call_driver(mdi->drvr, DRV_SHUTDOWN_DEV, NULL, NULL);

      mdi->drvr->initialized = 0;

      AIL_mem_free_lock(mdi->sequences);
      AIL_mem_free_lock(mdi);

      return NULL;
      }

   AIL_set_timer_user(mdi->timer,(U32) mdi);

   //
   // Set destructor handler and descriptor
   //

   mdi->drvr->destructor = (void *) XMI_destroy_MDI_driver;
   mdi->drvr->descriptor = mdi;

   //
   // Initialize synthesizer to General MIDI defaults   
   //

   for (i=0; i < NUM_CHANS; i++)
      {
      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        PATCH_BANK_SEL,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        GM_BANK_LSB,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        GM_BANK_MSB,
                                        0);

      XMI_MIDI_message(mdi,EV_PROGRAM | i,
                                        0,0);

      XMI_MIDI_message(mdi,EV_PITCH   | i,
                                        0x00,0x40);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        VOICE_PROTECT,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        MODULATION,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        PART_VOLUME,
                                        AIL_preference[MDI_DEFAULT_VOLUME]);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        PANPOT,
                                        64);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        EXPRESSION,
                                        127);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        SUSTAIN,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        REVERB,
                                        40);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        CHORUS,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        RPN_LSB,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        RPN_MSB,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        DATA_LSB,
                                        0);

      XMI_MIDI_message(mdi,EV_CONTROL | i,
                                        DATA_MSB,
                                        AIL_preference[MDI_DEFAULT_BEND_RANGE]);

      XMI_flush_buffer(mdi);

      if (!(i & 3))
         {
         AIL_delay(3);
         }
      }

   //
   // Start XMIDI timer service and return MDI_DRIVER descriptor
   //

   AIL_set_timer_frequency(mdi->timer,AIL_preference[MDI_SERVICE_RATE]);
   AIL_start_timer(mdi->timer);

   return mdi;
}

//############################################################################
//##                                                                        ##
//## Load, install, and initialize XMIDI audio driver                       ##
//##                                                                        ##
//############################################################################

HMDIDRIVER AILCALL AIL_API_install_MDI_driver_file(char const *filename, IO_PARMS *IO)
{
   AIL_DRIVER *drvr;
   S32        *driver_image;
   char fn[256];
   char prefix[128];
   HMDIDRIVER mdi=0;

   *prefix=0;

   if ((driver_image = AIL_file_read(filename,FILE_READ_WITH_SIZE)) == NULL)
      {
      if (*AIL_redist_directory==0)
         {
         goto error;
         }
      else
         {
         AIL_strcpy(fn, AIL_redist_directory);
         AIL_strcat(fn, filename);

         AIL_strcpy(prefix, GTL_prefix);
         AIL_strcpy(GTL_prefix, AIL_redist_directory);
         AIL_strcat(GTL_prefix, prefix);
         
         if ((driver_image = AIL_file_read(fn,FILE_READ_WITH_SIZE)) == NULL)
            {
            error:
            AIL_set_error("Driver file not found.");
            goto done;
            }
         }
      }

   drvr = AIL_install_driver((U8*) (driver_image+1), *driver_image );

   AIL_mem_free_lock(driver_image);

   if (drvr == NULL)
      {
      goto done;
      }

   mdi = XMI_construct_MDI_driver(drvr,IO);

   if (mdi == NULL)
      {
      AIL_uninstall_driver(drvr);
      }
  
  done:
   if (*prefix)
     AIL_strcpy(GTL_prefix, prefix);

   return mdi;
}

//############################################################################
//##                                                                        ##
//## Install and initialize XMIDI audio driver from file image              ##
//##                                                                        ##
//############################################################################

HMDIDRIVER AILCALL AIL_API_install_MDI_driver_image(void const *driver_image, //)
                                                  U32     size,
                                                  IO_PARMS *IO)
{
   AIL_DRIVER *drvr;
   HMDIDRIVER mdi;

   drvr = AIL_install_driver(driver_image,size);

   if (drvr == NULL)
      {
      return NULL;
      }

   mdi = XMI_construct_MDI_driver(drvr,IO);

   if (mdi == NULL)
      {
      AIL_uninstall_driver(drvr);
      }

   return mdi;
}

//############################################################################
//##                                                                        ##
//## Load, install, and initialize MIDI audio driver according to           ##
//## contents of MDI_INI file                                               ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_install_MDI_INI(HMDIDRIVER *mdi)
{
   AIL_INI ini;

   //
   // Attempt to read MDI_INI file
   //

   if (!AIL_read_INI(&ini, "MDI.INI"))
      {
      AIL_set_error("Unable to open file MDI.INI.");
      return AIL_NO_INI_FILE;
      }

   *mdi = AIL_install_MDI_driver_file(ini.driver_name,
                                     &ini.IO);

   if (*mdi == NULL)
      {
      return AIL_INIT_FAILURE;
      }

   return AIL_INIT_SUCCESS;
}

//############################################################################
//##                                                                        ##
//## Uninstall XMIDI audio driver                                           ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_uninstall_MDI_driver(HMDIDRIVER mdi)
{
   AIL_uninstall_driver(mdi->drvr);
}

//############################################################################
//##                                                                        ##
//## Return MIDI driver type (for selection of hardware-specific sequences, ##
//## etc.)                                                                  ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_MDI_driver_type(HMDIDRIVER mdi)
{  
   char *name;

   //
   // If driver name corresponds to Tandy or IBM speaker driver, return
   // speaker type
   //
   // Name field was added in VDI 1.12 spec, so don't check earlier drivers
   //

   if (mdi->drvr->VHDR->driver_version >= 0x112)
      {
      name = mdi->drvr->VHDR->dev_name;

      if (!AIL_stricmp(name,"Tandy 3-voice music"))
         {
         return MDIDRVRTYPE_SPKR;
         }

      if (!AIL_stricmp(name,"IBM internal speaker music"))
         {
         return MDIDRVRTYPE_SPKR;
         }
      }

   //
   // If no GTL suffix, assume it's a hardwired General MIDI device
   //

   name = REALPTR(mdi->DDT->GTL_suffix);

   if ((name == NULL) || (name[0] == 0))
      {
      return MDIDRVRTYPE_GM;
      }

   //
   // If GTL suffix = '.AD', it's an OPL-2
   //
   // Note: Creative AWE32 driver incorrectly declares '.AD' GTL prefix,
   // so provide workaround here -- if driver bigger than 20K, it's not one
   // of our FM drivers!
   //

   if (!AIL_stricmp(name,".AD"))
      {
      if (mdi->drvr->size > 20480)
         {
         return MDIDRVRTYPE_GM;
         }

      return MDIDRVRTYPE_FM_2;
      }

   //
   // If GTL suffix = '.OPL', it's an OPL-3
   //

   if (!AIL_stricmp(name,".OPL"))
      {
      return MDIDRVRTYPE_FM_4;
      }

   //
   // Otherwise, it's a currently-undefined GTL type -- assume it's a GM
   // device
   //

   return MDIDRVRTYPE_GM;
}

//############################################################################
//##                                                                        ##
//## Define a filename prefix for application's Global Timbre Library       ##
//## files                                                                  ##
//##                                                                        ##
//## Under MS-DOS, prefix may be up to 8 characters long and must not end   ##
//## in a period                                                            ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_set_GTL_filename_prefix(char const *prefix)
{
   S32 i;

   AIL_strcpy(GTL_prefix,prefix);

   //
   // Truncate prefix string at final '.' character, if it exists and
   // if it occurs after the last directory '\' character
   //

   for (i=AIL_strlen(GTL_prefix)-1; i; i--)
      {
      if (GTL_prefix[i] == '\\')
         {
         if (i == AIL_strlen(GTL_prefix)-1)
            {
            GTL_prefix[i] = 0;
            }

         break;
         }

      if (GTL_prefix[i] == '.')
         {
         GTL_prefix[i] = 0;
         break;
         }
      }
}

//############################################################################
//##                                                                        ##
//## Allocate a SEQUENCE structure for use with a given driver              ##
//##                                                                        ##
//############################################################################

HSEQUENCE AILCALL AIL_API_allocate_sequence_handle(HMDIDRIVER mdi)
{
   S32      i;
   HSEQUENCE S;

   //
   // Lock timer services to prevent reentry
   //

   AIL_lock();

   //
   // Look for an unallocated sequence structure
   // 

   for (i=0; i < mdi->n_sequences; i++)
      {
      if (mdi->sequences[i].status == SEQ_FREE)
         break;
      }

   //
   // If all structures in use, return NULL
   // 

   if (i == mdi->n_sequences)
      {
      AIL_set_error("Out of sequence handles.");

      AIL_unlock();
      return NULL;
      }

   S = &mdi->sequences[i];

   //
   // Initialize sequence to "done" status
   //

   S->status = SEQ_DONE;

   //
   // Initialize state table values
   // 

   XMI_init_sequence_state(S);

   //
   // Initialize end-of-sequence callback to NULL
   //

   S->EOS = NULL;

   //
   // Return sequence handle
   //

   AIL_unlock();
   return S;
}

//############################################################################
//##                                                                        ##
//## Free a SEQUENCE structure for later allocation                         ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_release_sequence_handle(HSEQUENCE S)
{
   if (S == NULL)
      {
      return;
      }

   //
   // Turn off all playing notes in sequence; release all channels
   //

   AIL_stop_sequence(S);

   //
   // Set 'free' flag
   //

   S->status = SEQ_FREE;
}


static void set_timbre_error(U32 bank, U32 patch)
{
  char err[128];
  U32 pars[2];
  pars[0]=bank;
  pars[1]=patch;
  AIL_sprintf(err,"Driver could not install timbre bank %d, patch %d\n",2,pars);
  AIL_set_error(err);
}


//############################################################################
//##                                                                        ##
//## Initialize a SEQUENCE structure to prepare for playback of desired     ##
//## XMIDI sequence file image                                              ##
//##                                                                        ##
//## Sequence is allocated (not free), done playing, and stopped            ##
//##                                                                        ##
//## Returns 0 if sequence initialization failed                            ##
//##        -1 if initialized OK but timbre was missing                     ##
//##         1 if initialization and timbre-loading successful              ##
//##                                                                        ##
//## Should not be called from callback function                            ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_init_sequence(HSEQUENCE S, void const *start, S32 sequence_num)
{
   U8 const    *image;
   U8 const    *end;
   U32        i,j,len;
   VDI_CALL     VDI;
   TIMB_chunk  *T;
   static U8 TIMB[sizeof(S->driver->DST->MIDI_data)];
   U32        bank,patch;

   if (S == NULL)
      {
      return 0;
      }

   //
   // Initialize status
   //

   S->status = SEQ_DONE;

   //
   // Find requested sequence in XMIDI image
   //

   image = XMI_find_sequence(start, sequence_num);

   if (image == NULL)
      {
      AIL_set_error("Invalid XMIDI sequence.");
      return 0;
      }

   //
   // Locate IFF chunks within FORM XMID:
   //
   // TIMB = list of bank/patch pairs needed to play sequence (optional)
   // RBRN = list of branch target points (optional)
   // EVNT = XMIDI event list (mandatory)
   //

   len = 8 + XMI_swap(*(U32 *) ((U8 *) (image+4)));
   end = image + len;

   image += 12;

   S->TIMB = NULL;
   S->RBRN = NULL;
   S->EVNT = NULL;

   while (image < end)
      {
      if (!AIL_memcmp(image,"TIMB",4))
         {
         S->TIMB = image;
         }

      if (!AIL_memcmp(image,"RBRN",4))
         {
         S->RBRN = image;
         }

      if (!AIL_memcmp(image,"EVNT",4))
         {
         S->EVNT = image;
         }

      image += 8 + XMI_swap(*(U32 *) ((U8 *) (image+4)));
      }

   //
   // Sequence must contain EVNT chunk
   //
   
   if (S->EVNT == NULL)
      {
      AIL_set_error("Invalid XMIDI sequence.");
      return 0;
      }

   //
   // Initialize sequence callback and state data
   //

   S->ICA              = NULL;
   S->prefix_callback  = NULL;
   S->trigger_callback = NULL;
   S->beat_callback    = NULL;
   S->EOS              = NULL;
   S->loop_count       = 1;

   XMI_rewind_sequence(S);

   //
   // Initialize volume and tempo
   //

   S->volume         =  AIL_preference[MDI_DEFAULT_VOLUME];
   S->volume_target  =  AIL_preference[MDI_DEFAULT_VOLUME];
   S->volume_period  =  0;
   S->volume_accum   =  0;

   S->tempo_percent  =  100;
   S->tempo_target   =  100;
   S->tempo_period   =  0;
   S->tempo_accum    =  0;
   S->tempo_error    =  0;

   //
   // If no TIMB chunk present, return success
   //

   if (S->TIMB == NULL)
      {
      return 1;
      }

   //
   // Make modifiable copy of TIMB chunk
   //

   AIL_memcpy(TIMB,
        S->TIMB,
           min(sizeof(S->driver->DST->MIDI_data),
               8 + XMI_swap(*(U32 *) ((S32 *) ((U8 *) S->TIMB+4)))));

   T = (TIMB_chunk *) TIMB;

   //
   // If timbre-request callback function registered, pass each bank/patch
   // pair to the function to see if it has to be requested from the driver
   //
   // Remove references to any timbres handled by the callback function
   //

   if (S->driver->timbre_trap != NULL)
      {
      i = 0;

      while (i < (U32) T->n_entries)
         {
         patch  =  ((U32) T->timbre[i]) & 0xff;
         bank   = (((U32) T->timbre[i]) & 0xff00) >> 8;
            
         if (!S->driver->timbre_trap(S->driver, (S32) bank, (S32) patch))
            {
            //
            // Timbre request was not handled by callback function --
            // check next timbre
            //

            ++i;
            }
         else
            {
            //
            // Timbre request was handled by callback function -- 
            // excise from TIMB chunk
            //
            // Scroll all subsequent entries down one slot, and test
            // the next timbre at the current slot
            //

            for (j = i+1; j < (U32) T->n_entries; j++)
               {
               T->timbre[j-1] = T->timbre[j];
               }

            --T->n_entries;

            if (T->lsb < 2)
               {
               T->lsb -= 2;
               T->msb--;
               }
            else
               T->lsb -= 2;
            }
         }
      }

   //
   // If all timbre requests have been handled, or the sequence contains no
   // timbre references, return success
   //
   // Otherwise, call driver to request timbre set installation
   //

   if (T->n_entries == 0)
      {
      return 1;
      }

   //
   // If called from background function (not recommended), return without
   // attempting to load timbres from disk
   //

   if (AIL_background())
      {
      AIL_set_error("No timbres loaded.");
      return -1;
      }

   //
   // Disable XMIDI service while accessing MIDI_data[] buffer
   //

   ++S->driver->disable;

   //
   // Copy TIMB chunk to driver's XMIDI buffer, and call driver
   // MDI_INSTALL_T_SET function
   // 
   // This must be done last, so the application can choose to ignore
   // timbre installation errors
   // 

   XMI_flush_buffer(S->driver);

   AIL_memcpy(S->driver->DST->MIDI_data,
           T,
           sizeof(S->driver->DST->MIDI_data));

   AIL_call_driver(S->driver->drvr, MDI_INSTALL_T_SET, NULL, &VDI);

   //
   // Re-enable XMIDI service and check for errors
   //

   --S->driver->disable;

   if (!VDI.AX)
      {
      set_timbre_error(VDI.BX >> 8, VDI.BX & 0xff);
      return -1;
      }

   return 1;
}

//############################################################################
//##                                                                        ##
//## Start playback of sequence from beginning                              ##
//##                                                                        ##
//## At a minimum, sequence must first have been initialized with a prior   ##
//## call to AIL_init_sequence()                                            ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_start_sequence(HSEQUENCE S)
{
   if (S == NULL)
      {
      return;
      }

   //
   // Make sure sequence has been allocated
   //

   if (S->status == SEQ_FREE)
      {
      return;
      }

   //
   // Stop sequence if playing
   //

   AIL_stop_sequence(S);

   //
   // Rewind sequence to beginning
   //

   XMI_rewind_sequence(S);

   //
   // Set 'playing' status
   //

   S->status = SEQ_PLAYING;
}

//############################################################################
//##                                                                        ##
//## Stop playback of sequence                                              ##
//##                                                                        ##
//## Sequence playback may be resumed with AIL_resume_sequence(), or        ##
//## restarted from the beginning with AIL_start_sequence()                 ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_stop_sequence(HSEQUENCE S)
{
   HMDIDRIVER mdi;
   S32        log,phys;

   if (S == NULL)
      {
      return;
      }

   //
   // Make sure sequence is currently playing
   //

   if (S->status != SEQ_PLAYING)
      {
      return;
      }

   //
   // Mask 'playing' status
   //

   S->status = SEQ_STOPPED;

   //
   // Turn off any active notes in sequence
   // 

   XMI_flush_note_queue(S);

   //
   // Prepare sequence's channels for use with other sequences, leaving 
   // shadow array intact for later recovery by AIL_resume_sequence()
   //

   mdi = S->driver;

   for (log=0; log < NUM_CHANS; log++)
      {
      phys = S->chan_map[log];

      //
      // If sustain pedal on, release it
      // 

      if (S->shadow.sus[log] >= 64)
         {
         XMI_MIDI_message(mdi,EV_CONTROL | phys,
                                           SUSTAIN,
                                           0);
         }

      //
      // If channel-lock protection active, cancel it
      // 

      if (S->shadow.c_prot[log] >= 64)
         {
         mdi->lock[phys] = UNLOCKED;
         }

      //
      // If voice-stealing protection active, cancel it
      // 

      if (S->shadow.c_v_prot[log] >= 64)
         {
         XMI_MIDI_message(mdi,EV_CONTROL | phys,
                                           VOICE_PROTECT,
                                           0);
         }

      //
      // Finally, if channel was locked, release it
      // 

      if (S->shadow.c_lock[log] >= 64)
         {
         AIL_release_channel(mdi, phys+1);
         }
      }
}

//############################################################################
//##                                                                        ##
//## Resume playback of previously stopped sequence                         ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_resume_sequence(HSEQUENCE S)
{
   HMDIDRIVER  mdi;
   S32        log;
   S32        ch;

   if (S == NULL)
      {
      return;
      }

   //
   // Make sure sequence has been previously stopped
   //

   if (S->status == SEQ_STOPPED) {

     //
     // Re-establish channel locks
     //

     mdi = S->driver;

     for (log=0; log < NUM_CHANS; log++)
        {
        if (S->shadow.c_lock[log] >= 64)
           {
           ch = AIL_lock_channel(mdi) - 1;

           S->chan_map[log] = (ch == -1) ? log : ch;
           }
        }

     //
     // Re-establish logged controller values (except Channel Lock, which
     // was done above)
     //

     for (log=0; log < NUM_CHANS; log++)
        {
        XMI_refresh_channel(S,log);
        }

     //
     // Set 'playing' status
     //

     S->status = SEQ_PLAYING;
   } else if (S->status==SEQ_DONE)
     S->status = SEQ_PLAYING;

}

//############################################################################
//##                                                                        ##
//## Terminate playback of sequence, setting sequence status to SEQ_DONE    ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_end_sequence(HSEQUENCE S)
{
   if (S == NULL)
      {
      return;
      }

   //
   // Make sure sequence is currently allocated
   //

   if ((S->status == SEQ_FREE) || (S->status == SEQ_DONE))
      {
      return;
      }

   //
   // Stop sequence and set 'done' status
   //

   AIL_stop_sequence(S);
   
   S->status = SEQ_DONE;

   //
   // Call EOS handler, if any
   //

   if (S->EOS != NULL)
      {
      S->EOS(S);
      }
}

//############################################################################
//##                                                                        ##
//## Set sequence loop count                                                ##
//##                                                                        ##
//##  1: Single iteration, no looping                                       ##
//##  0: Loop indefinitely                                                  ##
//##  n: Play sequence n times                                              ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_set_sequence_loop_count(HSEQUENCE S, S32 loop_count)
{
   if (S == NULL)
      {
      return;
      }

   S->loop_count = loop_count;
}

//############################################################################
//##                                                                        ##
//## Set relative tempo percentage for sequence, 0-100+ %                   ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_set_sequence_tempo(HSEQUENCE S, S32 tempo, S32 milliseconds)
{
   if (S == NULL)
      {
      return;
      }

   //
   // Disable XMIDI service while altering tempo control data
   //

   ++S->driver->disable;

   //
   // Set new tempo target; exit if no change
   //

   S->tempo_target = tempo;

   if (S->tempo_percent == S->tempo_target)
      {
      --S->driver->disable;

      return;
      }

   //
   // Otherwise, set up tempo ramp
   //

   if (milliseconds == 0)
      {
      S->tempo_percent = S->tempo_target;
      }
   else
      {
      S->tempo_period = (milliseconds * 1000) /
                    AIL_abs(S->tempo_percent - S->tempo_target);

      S->tempo_accum  = 0;
      }

   //
   // Restore XMIDI service and return
   //

   --S->driver->disable;
}

//############################################################################
//##                                                                        ##
//## Set volume scaling factor for all channels in sequence, 0-127          ##
//##                                                                        ##
//## Values above 127 cause "compression" effect                            ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_set_sequence_volume(HSEQUENCE S, S32 volume, S32 milliseconds)
{
   if (S == NULL)
      {
      return;
      }

   //
   // Disable XMIDI service while altering volume control data
   //

   ++S->driver->disable;

   //
   // Set new volume target; exit if no change
   //

   S->volume_target = volume;

   if (S->volume == S->volume_target)
      {
      --S->driver->disable;

      return;
      }

   //
   // Otherwise, set up volume ramp
   //

   if (milliseconds == 0)
      {
      S->volume = S->volume_target;
      }
   else
      {
      S->volume_period = (milliseconds * 1000) /
                    AIL_abs(S->volume - S->volume_target);

      S->volume_accum  = 0;
      }

   //
   // Restore interrupt state, update channel volume settings, and exit
   //

   XMI_update_volume(S);

   //
   // Restore XMIDI service and return
   //

   --S->driver->disable;
}

//############################################################################
//##                                                                        ##
//## Get status of sequence                                                 ##
//##                                                                        ##
//############################################################################

U32 AILCALL AIL_API_sequence_status(HSEQUENCE S)
{
   if (S == NULL)
      {
      return 0;
      }

   return S->status;
}

//############################################################################
//##                                                                        ##
//## Get number of sequence loops remaining                                 ##
//##                                                                        ##
//## 1 indicates that the sequence is on its last iteration                 ##
//## 0 indicates that the sequence is looping indefinitely                  ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_sequence_loop_count(HSEQUENCE S)
{
   if (S == NULL)
      {
      return -1;
      }

   return S->loop_count;
}

//############################################################################
//##                                                                        ##
//## Return relative tempo percentage for sequence, 0-100+ %                ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_sequence_tempo(HSEQUENCE S)
{
   if (S == NULL)
      {
      return 0;
      }

   return S->tempo_percent;
}

//############################################################################
//##                                                                        ##
//## Return volume scaling factor for sequence, 0-127                       ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_sequence_volume(HSEQUENCE S)
{
   if (S == NULL)
      {
      return 0;
      }

   return S->volume;
}

//############################################################################
//##                                                                        ##
//## Set master volume for all sequences                                    ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_set_XMIDI_master_volume(HMDIDRIVER mdi, S32 master_volume)
{
   HSEQUENCE S;
   S32      i;

   //
   // Set new volume; return if redundant setting
   //

   if (mdi->master_volume == master_volume)
      {
      return;
      }

   mdi->master_volume = master_volume;

   //
   // Force all sequences to update their volume controllers
   //

   ++mdi->disable;

   for (i = mdi->n_sequences,S = &mdi->sequences[0]; i; --i,++S)
      {
      if (S->status != SEQ_PLAYING)
         {
         continue;
         }

      XMI_update_volume(S);
      }

   --mdi->disable;
}

//############################################################################
//##                                                                        ##
//## Return current master MIDI volume setting                              ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_XMIDI_master_volume(HMDIDRIVER mdi)
{
   return mdi->master_volume;
}

//############################################################################
//##                                                                        ##
//## Return status of timbre: 1 if timbre is present in synthesizer memory  ##
//##                          or local driver cache, else 0                 ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_timbre_status(HMDIDRIVER mdi, S32 bank, S32 patch)
{
   VDI_CALL VDI;

   VDI.CX = (S16) ((bank << 8) | (patch));

   return AIL_call_driver(mdi->drvr, MDI_GET_T_STATUS, &VDI, NULL);
}

//############################################################################
//##                                                                        ##
//## Install a single instrument from proprietary library or GTL file       ##
//## associated with specified driver                                       ##
//##                                                                        ##
//## Returns 1 if timbre successfully installed or already present          ##
//##         0 if timbre installation failed for any reason (lack of        ##
//##         memory, file I/O error, timbre not found in library file...)   ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_install_timbre(HMDIDRIVER mdi, S32 bank, S32 patch)
{
   VDI_CALL VDI;

   static U8 dummy_TIMB[] =
      {
      'T','I','M','B',     // [0-3] TIMB chunk tag
      0,0,0,4,             // [4-7] Length in Motorola 68XXX format
      1,0,                 // [8-9] # of timbre list entries (1)
     -1,                   // [10]  Patch # to request
     -1                    // [11]  Bank # to request
      };

   //
   // If timbre callback function active, give it a chance to intercept
   // the request
   //

   if (mdi->timbre_trap != NULL)
      {
      if (mdi->timbre_trap(mdi, bank, patch))
         {
         return 1;
         }
      }

   //
   // Write bank and patch values to dummy TIMB chunk
   //

   dummy_TIMB[10] = (S8) patch;
   dummy_TIMB[11] = (S8) bank;

   //
   // Disable XMIDI service while accessing MIDI_data[] buffer
   //

   ++mdi->disable;

   //
   // Flush MIDI traffic out of buffer so it can be used to pass TIMB  
   // chunk to driver
   //

   XMI_flush_buffer(mdi);

   AIL_memcpy(mdi->DST->MIDI_data,
           dummy_TIMB,
           sizeof(dummy_TIMB));

   AIL_call_driver(mdi->drvr, MDI_INSTALL_T_SET, NULL, &VDI);

   //
   // Re-enable XMIDI service
   //

   --mdi->disable;

   if (!VDI.AX)
      {
      set_timbre_error(VDI.BX >> 8,VDI.BX & 0xff);
      }

   return VDI.AX;
}

//############################################################################
//##                                                                        ##
//## Protect timbre from being discarded from synthesizer memory or local   ##
//## driver cache                                                           ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_protect_timbre(HMDIDRIVER mdi, S32 bank, S32 patch)
{
   VDI_CALL VDI;

   VDI.CX = (S16) ((bank << 8) | (patch));
   VDI.DX = 1;

   AIL_call_driver(mdi->drvr, MDI_PROT_UNPROT_T, &VDI, NULL);
}

//############################################################################
//##                                                                        ##
//## Allow timbre to be discarded from synthesizer memory or local          ##
//## driver cache                                                           ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_unprotect_timbre(HMDIDRIVER mdi, S32 bank, S32 patch)
{
   VDI_CALL VDI;

   VDI.CX = (S16) ((bank << 8) | (patch));
   VDI.DX = 0;

   AIL_call_driver(mdi->drvr, MDI_PROT_UNPROT_T, &VDI, NULL);
}

//############################################################################
//##                                                                        ##
//## Return number of actively playing sequences for given driver           ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_active_sequence_count(HMDIDRIVER mdi)
{
   S32 i,n;

   n = 0;

   for (i=0; i < mdi->n_sequences; i++)
      {
      if (mdi->sequences[i].status == SEQ_PLAYING)
         {
         ++n;
         }
      }

   return n;
}

//############################################################################
//##                                                                        ##
//## Return current value of desired MIDI controller in sequence            ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_controller_value(HSEQUENCE S, S32 channel, S32 controller_num)
{
   if (S == NULL)
      {
      return -1;
      }

   return XMI_read_log(&S->shadow,
                       EV_CONTROL | (channel-1),
                       controller_num);
}

//############################################################################
//##                                                                        ##
//## Return number of 'on' notes in given channel                           ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_channel_notes(HSEQUENCE S, S32 channel)
{
   S32 i,n;

   if (S == NULL)
      {
      return 0;
      }

   //
   // Count number of notes with desired channel
   //

   n = 0;

   for (i=0; i < NUM_CHANS; i++)
      {
      if (S->note_chan[i] == (channel-1))
         {
         ++n;
         }
      }

   return n;
}

//############################################################################
//##                                                                        ##
//## Report relative beat and measure count for current XMIDI sequence      ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_sequence_position(HSEQUENCE S, S32 *beat, S32 *measure)
{
   S32 b,m,i,f;

   if (S == NULL)
      {
      return;
      }

   //
   // Disable XMIDI service to prevent errors
   //

   ++S->driver->disable;

   b = S->beat_count;
   m = S->measure_count;

   //
   // Advance beat/measure count by AIL_preference[MDI_QUANT_ADVANCE]
   // intervals
   //

   f = S->beat_fraction;

   for (i=0; i < AIL_preference[MDI_QUANT_ADVANCE]; i++)
      {
      f += S->time_fraction;

      if (f >= S->time_per_beat)
         {
         f -= S->time_per_beat;
         ++b;

         if (b >= S->time_numerator)
            {
            b = 0;
            ++m;
            }
         }
      }

   //
   // Avoid negative measure counts prior to sequence start
   //

   if (m < 0)
      {
      m = 0;
      }

   //
   // Return beat and/or measure count, as desired
   //

   if (measure != NULL)
      {
      *measure = m;
      }
   
   if (beat != NULL)
      {
      *beat = b;
      }

   --S->driver->disable;

   return;
}

//############################################################################
//##                                                                        ##
//## Branch immediately to specified Sequence Branch Index point            ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_branch_index(HSEQUENCE S, U32 marker)
{
   S32        n,i;
   RBRN_entry *R;

   if (S == NULL)
      {
      return;
      }

   //
   // Make sure RBRN block is present
   //

   if (S->RBRN == NULL)
      {
      return;
      }

   //
   // Get number of RBRN entries
   //

   n = *(S16 *) ((U8 *) S->RBRN + 8);

   //
   // Get pointer to RBRN entry list
   // 

   R = (RBRN_entry *) ((U8 *) S->RBRN + 10);

   //
   // Search RBRN list for specified index
   // 
   // If not found, return
   //

   for (i=0; i < n; i++)
      {
      if ((U32) (U16) R[i].bnum == marker)
         {
         break;
         }
      }

   if (i == n)
      {
      return;
      }

   //
   // Move sequence pointer to branch point
   //
   // The first event fetched at the new location will be the Sequence Branch
   // Index controller, so this routine may safely be called from within 
   // an XMIDI Callback Trigger, Force Branch, or other 3-byte event
   // handler
   //
   // Zero interval count, so next event will be fetched immediately
   //

   S->EVNT_ptr = (U8 *) S->EVNT + 8 + R[i].offset;

   S->interval_count = 0;

   //
   // Cancel all FOR...NEXT loops unless application specifies otherwise
   //

   if (AIL_preference[MDI_ALLOW_LOOP_BRANCHING] == NO)
      {
      for (n=0; n < FOR_NEST; n++)
         {
         S->FOR_loop_count[n] = -1;
         }
      }
}

//############################################################################
//##                                                                        ##
//## Install function handler for XMIDI Callback Prefix events              ##
//##                                                                        ##
//############################################################################

AILPREFIXCB AILCALL AIL_API_register_prefix_callback(HSEQUENCE S, AILPREFIXCB callback)
{
   AILPREFIXCB old;

   if (S == NULL)
      {
      return NULL;
      }

   old = S->prefix_callback;

   S->prefix_callback = callback;

   return old;
}

//############################################################################
//##                                                                        ##
//## Install function handler for XMIDI Callback Trigger events             ##
//##                                                                        ##
//############################################################################

AILTRIGGERCB AILCALL AIL_API_register_trigger_callback(HSEQUENCE S, AILTRIGGERCB callback)
{
   AILTRIGGERCB old;

   if (S == NULL)
      {
      return NULL;
      }

   old = S->trigger_callback;

   S->trigger_callback = callback;

   return old;
}

//############################################################################
//##                                                                        ##
//## Install function handler for end-of-sequence callbacks                 ##
//##                                                                        ##
//############################################################################

AILSEQUENCECB AILCALL AIL_API_register_sequence_callback(HSEQUENCE S, AILSEQUENCECB callback)
{
   AILSEQUENCECB old;

   if (S == NULL)
      {
      return NULL;
      }

   old = S->EOS;

   S->EOS = callback;

   return old;
}

//############################################################################
//##                                                                        ##
//## Install callback function handler for XMIDI beat/bar change events     ##
//##                                                                        ##
//############################################################################

AILBEATCB AILCALL AIL_API_register_beat_callback(HSEQUENCE S, AILBEATCB callback)
{
   AILBEATCB old;

   if (S == NULL)
      {
      return NULL;
      }

   old = S->beat_callback;

   S->beat_callback = callback;

   return old;
}

//############################################################################
//##                                                                        ##
//## Install callback function handler for MIDI/XMIDI event trap            ##
//##                                                                        ##
//############################################################################

AILEVENTCB AILCALL AIL_API_register_event_callback(HMDIDRIVER mdi, AILEVENTCB callback)
{
   AILEVENTCB old;

   old = mdi->event_trap;

   mdi->event_trap = callback;

   return old;
}

//############################################################################
//##                                                                        ##
//## Install callback function handler for MIDI/XMIDI timbre installation   ##
//##                                                                        ##
//############################################################################

AILTIMBRECB AILCALL AIL_API_register_timbre_callback(HMDIDRIVER mdi, AILTIMBRECB callback)
{
   AILTIMBRECB old;

   old = mdi->timbre_trap;

   mdi->timbre_trap = callback;

   return old;
}

//############################################################################
//##                                                                        ##
//## Set sequence user data value at specified index                        ##
//##                                                                        ##
//## Any desired 32-bit value may be stored at one of eight user data words ##
//## associated with a given SEQUENCE                                       ##
//##                                                                        ##
//## Callback functions may access the user data array at interrupt time    ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_set_sequence_user_data(HSEQUENCE S, U32 index, S32 value)
{
   if (S == NULL)
      {
      return;
      }

   S->user_data[index] = value;
}

//############################################################################
//##                                                                        ##
//## Get sequence user data value at specified index                        ##
//##                                                                        ##
//## Any desired 32-bit value may be stored at one of eight user data words ##
//## associated with a given SEQUENCE                                       ##
//##                                                                        ##
//## Callback functions may access the user data array at interrupt time    ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_sequence_user_data(HSEQUENCE S, U32 index)
{
   if (S == NULL)
      {
      return 0;
      }

   return S->user_data[index];
}

//############################################################################
//##                                                                        ##
//## Register an Indirect Controller Array for use with specified sequence  ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_register_ICA_array(HSEQUENCE S, U8 *array)
{
   if (S == NULL)
      {
      return;
      }

   S->ICA = (U8 *) array;
}

//############################################################################
//##                                                                        ##
//## Lock an unprotected physical channel                                   ##
//##                                                                        ##
//## Returns 0 if lock attempt failed                                       ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_lock_channel(HMDIDRIVER mdi)
{
   S32      i,j;
   S32      ch,best;
   HSEQUENCE S;

   //
   // Disable XMIDI service while locking channel
   //
   
   ++mdi->disable;

   //
   // Search for highest channel # with lowest note activity,
   // skipping already-locked and protected physical channels
   //

   ch   = -1;
   best = LONG_MAX;

   for (i=MAX_LOCK_CHAN; i >= MIN_LOCK_CHAN; i--)
      {
      if (i == PERCUSS_CHAN)
         {
         continue;
         }

      if ((mdi->lock[i] == LOCKED) ||
          (mdi->lock[i] == PROTECTED))
         {
         continue;
         }

      if (mdi->notes[i] < best)
         {
         best = mdi->notes[i];
         ch   = i;
         }
      }

   //
   // If no unprotected channels available, ignore lock protection and
   // try again
   //

   if (ch == -1)
      {
      for (i=MAX_LOCK_CHAN; i >= MIN_LOCK_CHAN; i--)
         {
         if (i == PERCUSS_CHAN)
            {
            continue;
            }

         if (mdi->lock[i] == LOCKED)
            {
            continue;
            }

         if (mdi->notes[i] < best)
            {
            best = mdi->notes[i];
            ch   = i;
            }
         }
      }

   //
   // If no unlocked channels available, return failure
   //

   if (ch == -1)
      {
      --mdi->disable;

      return 0;
      }

   //
   // Otherwise, release sustain pedal and turn off all active notes in 
   // physical channel, regardless of sequence
   //

   XMI_MIDI_message(mdi,EV_CONTROL | ch,
                        SUSTAIN,
                        0);

   for (i = mdi->n_sequences,S = &mdi->sequences[0]; i; --i,++S)
      {
      if (S->status == SEQ_FREE)
         {
         continue;
         }

      for (j=0; j < MAX_NOTES; j++)
         {
         if (S->note_chan[j] == -1)
            {
            continue;
            }

         if (S->chan_map[S->note_chan[j]] != ch)
            {
            continue;
            }

         XMI_send_channel_voice_message(S,
                                        S->note_chan[j] | EV_NOTE_OFF,
                                        S->note_num [j],
                                        0,
                                        0);
         S->note_chan[j] = -1;
         }
      }

   //
   // Lock channel
   //
   // By default, API asserts ownership of channel (locker=NULL), and
   // last sequence to use channel is recorded as its original owner
   //

   mdi->state [ch] = mdi->lock[ch];
   mdi->lock  [ch] = LOCKED;
   mdi->locker[ch] = NULL;
   mdi->owner [ch] = mdi->user[ch];

   //
   // Return 1-based channel number to caller  
   //

   --mdi->disable;

   return ch+1;
}

//############################################################################
//##                                                                        ##
//## Release (unlock) a locked physical channel                             ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_release_channel(HMDIDRIVER mdi, S32 channel)
{
   S32      i,j,ch;
   HSEQUENCE S;

   //
   // Convert channel # to 0-based internal notation
   //

   ch = channel-1;

   //
   // If channel is not locked, return
   //

   if (mdi->lock[ch] != LOCKED)
      {
      return;
      }

   //
   // Disable XMIDI service while unlocking channel
   //
   
   ++mdi->disable;

   //
   // Restore channel's original state and ownership  
   //

   mdi->lock[ch] = mdi->state[ch];
   mdi->user[ch] = mdi->owner[ch];

   //
   // Release sustain pedal and turn all notes off in channel, 
   // regardless of sequence
   // 

   XMI_MIDI_message(mdi,EV_CONTROL | ch,
                        SUSTAIN,
                        0);

   for (i = mdi->n_sequences,S = &mdi->sequences[0]; i; --i,++S)
      {
      if (S->status == SEQ_FREE)
         {
         continue;
         }

      for (j=0; j < MAX_NOTES; j++)
         {
         if (S->note_chan[j] == -1)
            {
            continue;
            }

         if (S->chan_map[S->note_chan[j]] != ch)
            {
            continue;
            }

         XMI_send_channel_voice_message(S,
                                        S->note_chan[j] | EV_NOTE_OFF,
                                        S->note_num [j],
                                        0,
                                        0);
         S->note_chan[j] = -1;
         }
      }

   //
   // Bring channel up to date with owner's current controller values, if
   // owner is valid sequence
   //

   if (mdi->owner[ch] != NULL)
      {
      if (mdi->owner[ch]->status != SEQ_FREE)
         {
         XMI_refresh_channel(mdi->owner[ch],ch);
         }
      }

   --mdi->disable;
}

//############################################################################
//##                                                                        ##
//## Map sequence's logical channel to desired physical output channel      ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_map_sequence_channel(HSEQUENCE S, S32 seq_channel, S32 new_channel)
{
   if (S == NULL)
      {
      return;
      }

   //
   // Redirect output on this sequence's channel to new channel
   // 

   S->chan_map[seq_channel-1] = new_channel-1;

   //
   // If channel is locked by API or other sequence, reassign it to
   // this sequence so it's not inhibited from playing
   //

   if ((S->driver->lock  [new_channel-1] == LOCKED) &&
       (S->driver->locker[new_channel-1] != S))
      {
      S->driver->locker[new_channel-1] = S;
      }
}

//############################################################################
//##                                                                        ##
//## Return physical channel to which sequence's logical channel is mapped  ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_true_sequence_channel(HSEQUENCE S, S32 seq_channel)
{
   if (S == NULL)
      {
      return 0;
      }

   return S->chan_map[seq_channel-1] + 1;
}

//############################################################################
//##                                                                        ##
//## Transmit MIDI channel voice message via desired physical channel       ##
//##                                                                        ##
//## This function disregards channel locking and other XMIDI features      ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_send_channel_voice_message(HMDIDRIVER mdi, HSEQUENCE S, //)
                                              S32       status,
                                              S32       data_1,
                                              S32       data_2)
{
   HMDIDRIVER drvr;

   //
   // Get driver handle to use
   //

   drvr=mdi;
   if (drvr==NULL)
      {
      if (S==NULL)
        {
        return;
        }
      drvr=S->driver;
      }

   //
   // Disable XMIDI service while accessing MIDI_data[] buffer
   //

   ++drvr->disable;

   if (S == NULL)
      {
      //
      // If this is a Part Volume (7) controller, scale its value by the
      // driver's master volume setting
      //

      if (((status & 0xf0) == EV_CONTROL) &&
           (data_1         == PART_VOLUME))
         {
         data_2 = (data_2 * drvr->master_volume) / 127;

         if (data_2 > 127)
            {
            data_2 = 127;
            }

         if (data_2 < 0)
            {
            data_2 = 0;
            }
         }   

      //
      // If no sequence handle given, transmit message on physical channel
      // without XMIDI logging
      //

      if ((drvr->event_trap == NULL)
           || (!drvr->event_trap(drvr,NULL,status,data_1,data_2)))
         {
         XMI_MIDI_message(drvr,status,data_1,data_2);
         }
      }
   else
      {
      //
      // Otherwise, perform logical-to-physical translation and XMIDI     
      // interpretation based on sequence handle, when transmitting
      // message
      //

      XMI_send_channel_voice_message(S,
                                     status,
                                     data_1,
                                     data_2,
                                     0);
      }

   XMI_flush_buffer(drvr);

   //
   // Reenable XMIDI service
   //

   --drvr->disable;
}

//############################################################################
//##                                                                        ##
//## Transmit MIDI System Exclusive message                                 ##
//##                                                                        ##
//## System Exclusive message must be passed in Standard MIDI Files 1.0     ##
//## format, may be of type F0 or F7 (although some drivers may not support ##
//## F7 messages), and must not exceed 512 bytes                            ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_send_sysex_message(HMDIDRIVER mdi, void const *buffer)
{
   U8 const *ptr;
   S32     len;

   //
   // Disable XMIDI service while accessing MIDI_data[] buffer
   //

   ++mdi->disable;

   //
   // Read message length and copy data to buffer
   //

   ptr = (U8 *) buffer + 1;

   len = XMI_read_VLN(&ptr);

   len += (S32) ((U32) ptr - (U32) buffer);

   XMI_sysex_message(mdi,buffer,len);

   //
   // Reenable XMIDI service
   //

   --mdi->disable;
}

//############################################################################
//##                                                                        ##
//## Get size and current position of sequence in milliseconds              ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_sequence_ms_position(HSEQUENCE S, //)
                                          S32 FAR    *total_milliseconds,
                                          S32 FAR   *current_milliseconds)
{
   //
   // Init total interval count = 0
   //

   S32 int_cnt = 0;
   S32 current = -1;

   //
   // Walk through all events in EVNT chunk, keeping track of
   // instrument regions used
   //

   U8 const FAR *ptr = (U8 FAR *) S->EVNT + 8;

   S32 done = 0;
   S32 last_int_stat = 0;

   while (!done)
      {
      S32 status = *ptr;
      S32 channel,type,len;

      //
      // If scan pointer reaches event pointer, mark current location
      // (Current event location is not valid until S->interval_count
      // reaches 0)
      //

      if ((AIL_ptr_dif(ptr,S->EVNT_ptr)>=0) && (current == -1))
         {
         current = int_cnt;

         if (S->interval_count >= 0)
            {
            current -= S->interval_count;
            }
         }

      //
      // Process interval byte
      //

      if (status < 0x80)
         {
         //
         // Accumulate interval count
         //

         last_int_stat = status;
         int_cnt += status;

         //
         // Skip delta time interval byte
         //

         ptr=AIL_ptr_add(ptr,1);
         continue;
         }

      switch (status)
         {
         //
         // Process MIDI meta-event, checking for end of sequence
         //

         case EV_META:

            ptr=AIL_ptr_add(ptr,1);

            type = *ptr;

            ptr=AIL_ptr_add(ptr,1);
            len = XMI_read_VLN(&ptr);

            if (type == META_EOT)
               {
               done = 1;
               }
            else
               {
               ptr=AIL_ptr_add(ptr,len);
               }
            break;

         //
         // Skip MIDI System Exclusive message
         //

         case EV_SYSEX:
         case EV_ESC:

            ptr=AIL_ptr_add(ptr,1);

            len = XMI_read_VLN(&ptr);

            ptr=AIL_ptr_add(ptr,len);
            break;

         //
         // Process MIDI channel voice message
         //

         default:

            channel = status & 0x0f;
            status  = status & 0xf0;

            //
            // Advance past channel-voice message
            //

            ptr=AIL_ptr_add(ptr,XMI_message_size(status));

            //
            // If this was EV_NOTE_ON, advance past duration word
            //

            if (status == EV_NOTE_ON)
               {
               XMI_read_VLN(&ptr);
               }

            break;
         }
      }

   //
   // Return requested values
   //

   if (total_milliseconds != NULL)
      {
      *total_milliseconds = (1000*int_cnt)/AIL_get_preference(MDI_SERVICE_RATE);
      }

   if (current_milliseconds != NULL)
      {
      *current_milliseconds = (1000*current)/AIL_get_preference(MDI_SERVICE_RATE);
      }
}

//############################################################################
//##                                                                        ##
//## Seek to a specified millisecond within a sequence                      ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_set_sequence_ms_position  (HSEQUENCE S, //)
                                                S32       milliseconds)
{
   HMDIDRIVER mdi;
   S32        log,phys;

   //
   // Get pointer to sequence events
   //

   U8 const FAR *ptr = (U8 FAR *) S->EVNT + 8;

   S32 target_interval=(milliseconds*AIL_get_preference(MDI_SERVICE_RATE))/1000;
   S32 done = 0;

   //
   // Init total interval count = 0
   //

   S32 int_cnt = 0;

   //
   // Make copy of control log which reflects current sequence state
   //

   CTRL_LOG original = S->shadow;

   //
   // Turn off any active notes in sequence
   //

   XMI_flush_note_queue(S);

   mdi = S->driver;

   for (log=0; log < NUM_CHANS; log++)
      {
      phys = S->chan_map[log];

      //
      // If sustain pedal on, release it
      //

      if (S->shadow.sus[log] >= 64)
         {
         XMI_MIDI_message(mdi,EV_CONTROL | phys,
                                           SUSTAIN,
                                           0);
         }
      }

   //
   // Walk through all events in EVNT chunk, keeping track of
   // instrument regions used
   //

   while (!done)
      {
      S32 status = *ptr;
      S32 type,len;

      //
      // If target interval reached, set new position and break out of loop
      //

      if (int_cnt >= target_interval)
         {
         S->EVNT_ptr = ptr;
         break;
         }

      //
      // Process interval byte
      //

      if (status < 0x80)
         {
         //
         // Accumulate interval count
         //

         int_cnt += status;

         //
         // Skip delta time interval byte
         //

         ptr=AIL_ptr_add(ptr,1);
         continue;
         }

      switch (status)
         {
         //
         // Process MIDI meta-event, checking for end of sequence
         //

         case EV_META:

            ptr=AIL_ptr_add(ptr,1);

            type = *ptr;

            ptr=AIL_ptr_add(ptr,1);
            len = XMI_read_VLN(&ptr);

            if (type == META_EOT)
               {
               done = 1;
               }
            else
               {
               ptr=AIL_ptr_add(ptr,len);
               }
            break;

         //
         // Skip MIDI System Exclusive message
         //

         case EV_SYSEX:
         case EV_ESC:

            ptr=AIL_ptr_add(ptr,1);

            len = XMI_read_VLN(&ptr);

            ptr=AIL_ptr_add(ptr,len);
            break;

         //
         // Process MIDI channel voice message
         //

         default:

            //
            // Update sequence state table
            //

            XMI_write_log(&S->shadow, status, *((U8 FAR*)AIL_ptr_add(ptr,1)), *((U8 FAR*)AIL_ptr_add(ptr,2)));

            //
            // Advance past channel-voice message
            //

            ptr=AIL_ptr_add(ptr,XMI_message_size(status));

            //
            // If this was EV_NOTE_ON, advance past duration word
            //

            if ((status&0xf0) == EV_NOTE_ON)
               {
               XMI_read_VLN(&ptr);
               }

            break;
         }
      }

   //
   // Send MIDI traffic as necessary to bring synthesizer up to date with
   // changes made to sequence state table
   //

   XMI_update_sequence(S, &original, &S->shadow);
}

//############################################################################
//##                                                                        ##
//## End of locked code                                                     ##
//##                                                                        ##
//############################################################################

void AILXMIDI_end(void)
{
   if (locked)
      {
      AIL_vmm_unlock_range(AILXMIDI_start, AILXMIDI_end);

      UNLOCK (GTL_prefix   );
      UNLOCK (entry        );
      UNLOCK (S            );
      UNLOCK (i            );
      UNLOCK (n            );
      UNLOCK (sequence_done);
      UNLOCK (q            );
      UNLOCK (t            );
      UNLOCK (channel      );
      UNLOCK (status       );
      UNLOCK (type         );
      UNLOCK (len          );
      UNLOCK (ptr          );
      UNLOCK (event        );

      locked = 0;
      }
}



