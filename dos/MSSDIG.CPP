//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  MSSDIG.C: Digital Sound C API module and support routines             ##
//##                                                                        ##
//##  Flat-model source compatible with IBM 32-bit ANSI C/C++               ##
//##                                                                        ##
//##  Version 1.00 of 20-Apr-94: Initial                                    ##
//##          1.01 of 21-Oct-94: No repetition after buffer starvation      ##
//##                             Watcom/FlashTek support implemented        ##
//##          1.02 of 27-Nov-94: Allow use of DMA channel 0                 ##
//##          1.03 of 28-Nov-94: Initialize sample handle upon allocation   ##
//##          1.04 of 29-Mar-95: AIL_resume_sample() restarts DMA           ##
//##          1.05 of 18-Apr-95: Enable IRQs during SS_serve() by default   ##
//##          1.06 of  9-Jun-95: Stereo tracks panned for mono output       ##
//##                             Use multiply/shift for 16-bit scaling      ##
//##                             Digital master volume added                ##
//##                             Improved error reporting for INI loading   ##
//##                             Convert driver handles to HDIGDRIVER       ##
//##                             Borland/Zortech support added              ##
//##          1.07 of 20-Feb-96: Added new 3.5 functionality                ##
//##          1.20 of 10-May-02: Massive changes for new reverb, many       ##
//##                             functions moved to wavefile.cpp (JKR)      ##
//##                                                                        ##
//##  Author: John Miles and Jeff Roberts                                   ##
//##                                                                        ##
//############################################################################
//##                                                                        ##
//##  Copyright (C) RAD Game Tools, Inc.                                    ##
//##                                                                        ##
//##  Contact RAD Game Tools at 425-893-4300 for technical support.         ##
//##                                                                        ##
//############################################################################

#include <limits.h>
#include <stdio.h>
#include <dos.h>
#include <stdlib.h>

#include "mss.h"
#include "imssapi.h"

//
// Assembly-language functions
//

extern "C"
{
extern void cdecl MSSA32_VMM_lock(void);
extern void cdecl AILSSA_VMM_lock(void);
}

//############################################################################
//##                                                                        ##
//## Locked static data                                                     ##
//##                                                                        ##
//############################################################################

//
// SS_serve
//

static U32       entry = 0;
static HDIGDRIVER  dig;
static VDI_CALL    VDI;
static S32        flags;
static S32        cnt,n;
static S32        current;
static HSAMPLE     S;

//############################################################################
//##                                                                        ##
//## Locked code                                                            ##
//##                                                                        ##
//############################################################################

#define LOCK(x)   AIL_vmm_lock  (&(x),sizeof(x))
#define UNLOCK(x) AIL_vmm_unlock(&(x),sizeof(x))

static S32 locked = 0;

void AILSS_end(void);

void AILSS_start(void)
{
   if (!locked)
      {
      AIL_vmm_lock_range(AILSS_start, AILSS_end);

      LOCK (entry    );
      LOCK (dig      );
      LOCK (VDI      );
      LOCK (flags    );
      LOCK (cnt      );
      LOCK (n        );
      LOCK (current  );
      LOCK (S        );

      MSSA32_VMM_lock();
      AILSSA_VMM_lock();

      locked = 1;
      }
}


//############################################################################
//##                                                                        ##
//## Start driver-based DMA buffer playback                                 ##
//##                                                                        ##
//############################################################################

void AILCALL SS_start_DIG_driver_playback(HDIGDRIVER dig)
{
   VDI_CALL VDI;

   if (!dig->playing)
      {
      VDI.DX = (U16) dig->hw_format;
      VDI.CX = (U16) dig->DMA_rate;

      AIL_call_driver(dig->drvr, DIG_START_P_CMD, &VDI, NULL);

      dig->playing = 1;
      }
}

//############################################################################
//##                                                                        ##
//## Stop driver-based DMA buffer playback                                  ##
//##                                                                        ##
//############################################################################

static void AILCALL SS_stop_DIG_driver_playback(HDIGDRIVER dig)
{
   if (dig->playing)
      {
      AIL_call_driver(dig->drvr, DIG_STOP_P_REQ, NULL, NULL);

      dig->playing = 0;
      }
}

//############################################################################
//##                                                                        ##
//## Timer interrupt routine to poll DMA buffer flags and process samples   ##
//##                                                                        ##
//############################################################################

static void AILCALLBACK SS_serve(U32 clientval)
{
   HDIGDRIVER client=(HDIGDRIVER)clientval;

   U32 start_us = AIL_us_count();
   S32 current_ms = start_us/1000;

   //
   // Disallow re-entrant calls
   //

   if (entry)
      {
      return;
      }

   entry = 1;

   //
   // Set up stack-independent DIG_DRIVER pointer
   //

   dig = client;

   //
   // If desired, disable interrupts while manipulating DMA buffers
   //
   // Set DIG_SS_LOCK to NO for use with high-speed serial lines -- may
   // cause audio glitches, however, if ISR takes too long
   //

   if (AIL_preference[DIG_SS_LOCK])
      {
      flags = AIL_disable_interrupts();
      }

   //
   // If driver not transmitting buffer data, return
   //

   if (!dig->playing)
      {
      if (AIL_preference[DIG_SS_LOCK])
         {
         AIL_restore_interrupts(flags);
         }

      entry = 0;
      return;
      }

   //
   // Return immediately if buffer has not switched
   // 
                                                     
   current = (S32) *dig->buffer_flag;

   if ((current == -1) || (current == dig->last_buffer))
      {
      if (AIL_preference[DIG_SS_LOCK])
         {
         AIL_restore_interrupts(flags);
         }

      entry = 0;
      return;
      }

   dig->last_buffer = current;

   //
   // Flush build buffer with silence
   //

   SS_flush(dig);

   //
   // Merge active samples (if any) into build buffer
   //

   cnt = 0;

   for (n = dig->n_samples,S = &dig->samples[0]; n; --n,++S)
      {
      //
      // Skip sample if stopped, finished, or not allocated
      //

      if (S->status != SMP_PLAYING)
         {
         continue;
         }

      ++cnt;

      //
      // Convert sample to 16-bit signed format and mix with
      // contents of build buffer
      //
      // Note that SS_stream_to_buffer() may invoke user callback functions
      // which may free or otherwise alter the sample being merged
      //

      SS_stream_to_buffer(S);
      }

   //
   // Set number of active samples
   //

   dig->n_active_samples = cnt;

   //
   // Copy build buffer contents to DMA buffer
   //

   SS_copy(dig, dig->DMA[ current ^ 1 ]);

   //
   // Send buffer-service VSE callback to driver, if required
   //

   if (dig->hw_mode_flags & DIG_BUFFER_SERVICE)
      {
      VDI.CX = 0;
      VDI.DX = (S16) (current ^ 1);

      AIL_call_driver(dig->drvr, DIG_VSE, &VDI, NULL);
      }

   //
   // If no samples active for two consecutive interrupts,
   // request DMA halt
   //

   if (dig->n_active_samples)
      {
      dig->quiet = 0;
      }
   else
      {
      if (dig->quiet++ == 2)
         {
         SS_stop_DIG_driver_playback(dig);
         }
      }

   //
   // Restore interrupts and return
   //

   if (AIL_preference[DIG_SS_LOCK])
      {
      AIL_restore_interrupts(flags);
      }

   entry = 0;
   
   //
   // keep the profiling information
   //

   U32 end_us=AIL_us_count();

   start_us=(end_us<start_us)?(end_us+(0xffffffff-start_us)):(end_us-start_us);

   dig->us_count+=start_us;
   if (dig->us_count>10000000) {
     dig->ms_count+=(dig->us_count/1000);
     dig->us_count=dig->us_count%1000;
   }
}

//############################################################################
//##                                                                        ##
//## Configure DMA half-buffers                                             ##
//##                                                                        ##
//## Returns 1 if configuration valid, else 0                               ##
//##                                                                        ##
//############################################################################

static S32 AILCALL SS_configure_buffers(HDIGDRIVER dig)
{
   S32  i,min,max,hb_samples;
   S32  n,match,delta;
   S32  pref[4];
   U32 linear;
   S32  DMA_status;

   DMA_status = dig->playing;

   if (DMA_status)
      {
      //
      // Pause all playing samples
      //
      // (Implemented for future use with drivers which can be
      //  restarted with new DMA buffers)
      //

      for (i=0; i < dig->n_samples; i++)
         {
         dig->samples[i].system_data[SSD_TEMP] = dig->samples[i].status;

         if (dig->samples[i].status == SMP_PLAYING)
            {
            dig->samples[i].status = SMP_STOPPED;
            }
         }

      //
      // Halt DMA playback and flush DMA buffers with silence
      //

      SS_stop_DIG_driver_playback(dig);

      SS_flush(dig);

      SS_copy(dig, dig->DMA[ 0 ]);
      SS_copy(dig, dig->DMA[ 1 ]);
      }

   //
   // Select hardware output format based on application's preference
   //
   // If desired mode is unavailable, downgrade/upgrade to other modes
   //

   switch ((AIL_preference[DIG_USE_STEREO] << 1) |
           (AIL_preference[DIG_USE_16_BITS]))
      {
      case DIG_F_MONO_8:

         pref[0] = DIG_F_MONO_8;
         pref[1] = DIG_F_MONO_16;
         pref[2] = DIG_F_STEREO_8;
         pref[3] = DIG_F_STEREO_16;
         break;

      case DIG_F_MONO_16:

         pref[0] = DIG_F_MONO_16;
         pref[1] = DIG_F_MONO_8;
         pref[2] = DIG_F_STEREO_16;
         pref[3] = DIG_F_STEREO_8;
         break;

      case DIG_F_STEREO_8:

         pref[0] = DIG_F_STEREO_8;
         pref[1] = DIG_F_STEREO_16;
         pref[2] = DIG_F_MONO_8;
         pref[3] = DIG_F_MONO_16;
         break;

      case DIG_F_STEREO_16:

         pref[0] = DIG_F_STEREO_16;
         pref[1] = DIG_F_STEREO_8;
         pref[2] = DIG_F_MONO_16;
         pref[3] = DIG_F_MONO_8;
         break;
      }

   for (i=0; i<4; i++)
      {
      if (dig->DDT->format_supported[pref[i]])
         {
         dig->hw_format = pref[i];
         break;
         }
      }

   dig->hw_mode_flags = dig->DDT->format_data[dig->hw_format].flags;

   //
   // Select physical output rate based on application's preference
   //

   switch (AIL_preference[DIG_HARDWARE_SAMPLE_RATE])
      {
      case MIN_VAL:

         dig->DMA_rate =
            dig->DDT->format_data[dig->hw_format].
               minimum_physical_sample_rate;
         break;

      case NOM_VAL:

         dig->DMA_rate =
            dig->DDT->format_data[dig->hw_format].
               nominal_physical_sample_rate;
         break;

      case MAX_VAL:

         dig->DMA_rate =
            dig->DDT->format_data[dig->hw_format].
               maximum_physical_sample_rate;
         break;

      default:

         //
         // If preference set to explicit frequency, use best match with
         // current driver
         //

         n = AIL_preference[DIG_HARDWARE_SAMPLE_RATE];

         pref[0] = dig->DDT->format_data[dig->hw_format].
                      minimum_physical_sample_rate;

         pref[1] = dig->DDT->format_data[dig->hw_format].
                      nominal_physical_sample_rate;

         pref[2] = dig->DDT->format_data[dig->hw_format].
                      maximum_physical_sample_rate;

         delta = LONG_MAX;

         for (i = 0; i < 3; i++)
            {
            if (AIL_abs(n - pref[i]) <= delta)
               {
               delta = AIL_abs(n - pref[i]);
               match = i;
               }
            }

         dig->DMA_rate = pref[match];
         break;
      }

   //
   // Set sample size values
   //

   switch (dig->hw_format)
      {
      case DIG_F_MONO_8:
         dig->channels_per_sample = 1;
         dig->bytes_per_channel   = 1;
         break;

      case DIG_F_STEREO_8:
         dig->channels_per_sample = 2;
         dig->bytes_per_channel   = 1;
         break;

      case DIG_F_MONO_16:
         dig->channels_per_sample = 1;
         dig->bytes_per_channel   = 2;
         break;

      case DIG_F_STEREO_16:
         dig->channels_per_sample = 2;
         dig->bytes_per_channel   = 2;
         break;
      }

   //
   // Get # of samples per half-buffer, assuming desired latency
   // 
   
   hb_samples = (dig->DMA_rate * AIL_preference[DIG_LATENCY]) / 1000;

   //
   // Calculate half-buffer size in bytes
   //

   dig->buffer_size = hb_samples *
                           dig->bytes_per_channel *
                           dig->channels_per_sample;

   //
   // Make sure half-buffer size is legal with this driver
   //

   min = dig->DDT->format_data[dig->hw_format].minimum_DMA_half_buffer_size;
   max = dig->DDT->format_data[dig->hw_format].maximum_DMA_half_buffer_size;

   //
   // If Windows DOS session active, maximum half-buffer size is 2048
   // under all drivers
   //

   if (AIL_preference[AIL_ALLOW_VDM_EXECUTION])
      {
      if (max > 2048)
         {
         max = 2048;
         }

      if (min > 2048)
         {
         AIL_set_error("Minimum DMA buffer size too large for VDM.");

         return 0;
         }
      }

   //
   // Round half-buffer size to nearest binary power between 8 and
   // DIG_DMA_RESERVE / 2; ensure result within driver limits
   //

   delta = LONG_MAX;

   for (n = 8; n <= (AIL_preference[DIG_DMA_RESERVE] / 2); n <<= 1)
      {
      if (AIL_abs(n - dig->buffer_size) <= delta)
         {
         delta = AIL_abs(n - dig->buffer_size);
         match = n;
         }
      }

   dig->buffer_size = match;

   if (dig->buffer_size < min)
      {
      dig->buffer_size = min;
      }

   if (dig->buffer_size > max)
      {
      dig->buffer_size = max;
      }

   //
   // Set DMA half-buffer segments in driver DST, and store equivalent 
   // protected-mode pointers in driver descriptor structure
   //
   // If Windows VDM compatibility requested, set buffer address
   // to beginning of first contiguous 4K page in DMA buffer
   //

   linear = (U32) dig->DMA_seg;

   if (AIL_preference[AIL_ALLOW_VDM_EXECUTION])
      {
      linear >>= 12;

      linear += 4095;
      linear &= 0xff000;

      linear <<= 12;
      }

   dig->DST->DMA_buffer_A = (REALFAR)  linear;
   dig->DST->DMA_buffer_B = (REALFAR) (linear + dig->buffer_size);

   dig->DMA[0] = REALPTR(dig->DST->DMA_buffer_A);
   dig->DMA[1] = REALPTR(dig->DST->DMA_buffer_B);

   if (DMA_status)
      {
      //
      // Resume any interrupted samples
      //
      // (Implemented for future use with drivers which can be
      //  restarted with new DMA buffers)
      //

      for (i=0; i < dig->n_samples; i++)
         {
         dig->samples[i].status = dig->samples[i].system_data[SSD_TEMP];
         }

      //
      // Flush new DMA buffers with silence and resume DMA playback
      //

      SS_flush(dig);

      SS_copy(dig, dig->DMA[ 0 ]);
      SS_copy(dig, dig->DMA[ 1 ]);

      SS_start_DIG_driver_playback(dig);
      }

   return 1;
}

//############################################################################
//##                                                                        ##
//## Call device I/O verification function using current detection policy   ##
//##                                                                        ##
//############################################################################

static S32 AILCALL SS_attempt_DIG_detection(HDIGDRIVER dig, IO_PARMS *IO)
{
   VDI_CALL  VDI;
   IO_PARMS *f;
   IO_PARMS  attempt;
   U32     i;

   //
   // Set up working IO_PARMS structure
   //

   attempt = *IO;

   //
   // If any needed parameters are not specified, use parameter
   // values from first factory-default IO structure
   // 

   if (dig->drvr->VHDR->num_IO_configurations > 0)
      {
      f = (IO_PARMS *) (REALPTR(dig->drvr->VHDR->common_IO_configurations));

      if ((S32) attempt.IO < 0)
         {
         attempt.IO = f->IO;
         }

      if ((S32) attempt.IRQ < 0)
         {
         attempt.IRQ = f->IRQ;
         }

      if ((S32) attempt.DMA_8_bit < 0)
         {
         attempt.DMA_8_bit = f->DMA_8_bit;
         }

      if ((S32) attempt.DMA_16_bit < 0)
         {
         attempt.DMA_16_bit = f->DMA_16_bit;
         }

      for (i=0; i < 4; i++)
         {
         if ((S32) attempt.IO_reserved[i] < 0)
            {
            attempt.IO_reserved[i] = f->IO_reserved[i];
            }
         }
      }

   //
   // Copy IO parameter block to driver
   //

   dig->drvr->VHDR->IO = attempt;

   //
   // If application wishes to avoid 16-bit DMA channel usage,
   // attempt detection on 8-bit channels only
   //
   // Otherwise, try either/both channels
   //

   if (AIL_preference[DIG_ALLOW_16_BIT_DMA] == NO)
      {
      VDI.DX = DIG_DETECT_8_BIT_ONLY;

      return AIL_call_driver(dig->drvr, DRV_VERIFY_IO, &VDI, NULL);
      }
   else
      {
      VDI.DX = DIG_DETECT_8_AND_16_BITS;

      return AIL_call_driver(dig->drvr, DRV_VERIFY_IO, &VDI, NULL);
      }
}

//############################################################################
//##                                                                        ##
//## Uninstall digital audio driver, freeing all allocated resources        ##
//##                                                                        ##
//## This function is called via the AIL_DRIVER.destructor vector only      ##
//##                                                                        ##
//############################################################################

static void AILCALL SS_destroy_DIG_driver(HDIGDRIVER dig)
{
   //
   // Halt DMA traffic and flush buffer with silence
   // 

   if (dig->playing)
      {
      SS_stop_DIG_driver_playback(dig);

      SS_flush(dig);

      SS_copy(dig, dig->DMA[ 0 ] );
      SS_copy(dig, dig->DMA[ 1 ] );

      AIL_delay(10);
      }

   //
   // Stop buffer timer service
   // 

   AIL_release_timer_handle(dig->backgroundtimer);

   //
   // Release any open sample handles (to ensure that pipeline resources
   // are deallocated properly)
   //

   S32 i;

   for (i=0; i < dig->n_samples; i++)
      {
      if (dig->samples[i].status != SMP_FREE)
         {
         AIL_release_sample_handle(&dig->samples[i]);
         }
      }

   //
   // Release memory resources
   //

   AIL_mem_free_DOS  (dig->DMA_buf, dig->DMA_seg, dig->DMA_sel);
   AIL_mem_free_lock (dig->samples);
   AIL_mem_free_lock (dig->build_buffer);
   AIL_mem_free_lock((void FAR *) dig->reverb_build_buffer);
   AIL_mem_free_lock (dig);
}

//############################################################################
//##                                                                        ##
//## Install and initialize digital audio driver                            ##
//##                                                                        ##
//############################################################################

static HDIGDRIVER AILCALL SS_construct_DIG_driver(AIL_DRIVER *drvr, IO_PARMS *IO)
{
   IO_PARMS    use;
   S32        i;
   S32        detected;
   VDI_CALL    VDI;
   HDIGDRIVER  dig;
   S32        DMA_bytes;
   U32       start_pg,end_pg;
   void       *buf;
   REALFAR     seg;
   U32       sel;
   U32       linear;

   //
   // Ensure that all AILSS code and data is locked into memory
   //

   AILSS_start();

   //
   // Allocate memory for DIG_DRIVER structure
   //

   dig = (HDIGDRIVER) AIL_mem_alloc_lock(sizeof(DIG_DRIVER));

   if (dig == NULL)
      {
      AIL_set_error("Out of memory.");

      return NULL;
      }

   //
   // Explicitly initialize all DIG_DRIVER fields to NULL/0
   //

   AIL_memset(dig,
              0,
              sizeof(*dig));

   AIL_memcpy(dig->tag,"HDIG",4);

   dig->drvr = drvr;

   //
   // Check for MMX support if enabled
   //

   dig->use_MMX = AIL_MMX_available();

   //
   // Reject driver if not of type .DIG
   //

   if (dig->drvr->type != AIL3DIG)
      {
      AIL_set_error(".DIG driver required.");

      AIL_mem_free_lock(dig);

      return NULL;
      }

   //
   // Get DDT and DST addresses
   //

   AIL_call_driver(dig->drvr, DRV_GET_INFO, NULL, &VDI);

   dig->DDT = (DIG_DDT *) (((U32) ((U16) VDI.DX) << 4) + (U32) (U16) VDI.AX);
   dig->DST = (DIG_DST *) (((U32) ((U16) VDI.CX) << 4) + (U32) (U16) VDI.BX);

#ifdef _X32_H_INCLUDED

   dig->DDT = (DIG_DDT *) ((U32) dig->DDT + (U32) _x32_zero_base_ptr);
   dig->DST = (DIG_DST *) ((U32) dig->DST + (U32) _x32_zero_base_ptr);

#else

#ifdef __BORLANDC__

   dig->DDT = (DIG_DDT *) ((U32) dig->DDT - AIL_sel_base(_DS));
   dig->DST = (DIG_DST *) ((U32) dig->DST - AIL_sel_base(_DS));

#endif

#endif

   //
   // Initialize miscellaneous DIG_DRIVER members
   //

   dig->buffer_flag      = &(dig->DST->active_buffer);
   dig->last_buffer      = -1;

   dig->playing          = 0;
   dig->quiet            = 0;

   dig->n_active_samples = 0;

   dig->master_volume    = 1.0F;
   dig->master_dry       = 1.0F;
   dig->master_wet       = 1.0F;

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

      if (SS_attempt_DIG_detection(dig,IO))
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
      IO = AIL_get_IO_environment(dig->drvr);

      if (IO != NULL)
         {
         AIL_last_IO_attempt = *IO;

         if (SS_attempt_DIG_detection(dig,IO))
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
      for (i=0; i < dig->drvr->VHDR->num_IO_configurations; i++)
         {
         IO = &((IO_PARMS *)
                (REALPTR(dig->drvr->VHDR->common_IO_configurations)))[i];

         if (i==0)
            {
            AIL_last_IO_attempt = *IO;
            }

         if (SS_attempt_DIG_detection(dig,IO))
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
      AIL_set_error("Digital sound hardware not found.");

      AIL_mem_free_lock(dig);

      return NULL;
      }

   //
   // Allocate pair of DMA half-buffers within a 64K physical page
   // 
   // If VDM execution desired, two 4K pages are allocated to ensure the
   // availability of at least one monolithic 4K physical page for the
   // two half-buffers
   //
   
   if (AIL_preference[AIL_ALLOW_VDM_EXECUTION])
      {
      DMA_bytes = 2 * 4096;
      }
   else
      {
      DMA_bytes = AIL_preference[DIG_DMA_RESERVE];
      }

   dig->DMA_buf = NULL;

   do
      {
      if (!AIL_mem_alloc_DOS((DMA_bytes + 15) / 16, &buf, &seg, &sel))
         {
         AIL_set_error("Could not allocate DMA buffers.");

         AIL_mem_free_lock(dig);
      
         return NULL;
         }

       if (dig->DMA_buf != NULL)
         {
         AIL_mem_free_DOS(dig->DMA_buf, dig->DMA_seg, dig->DMA_sel);
         }

       dig->DMA_buf = buf;
       dig->DMA_seg = seg;
       dig->DMA_sel = sel;

       linear = (U32) seg >> 12;

       start_pg = ((U32)           linear                 ) & 0xf0000L;
       end_pg   = ((U32) ((U8 *) linear + DMA_bytes - 1)) & 0xf0000L;
       }
   while (start_pg != end_pg);

   //
   // Configure working DMA half-buffers
   //
   // If Windows VDM compatibility requested, limit half-buffer size to 2,048
   // bytes (i.e., one-half of 4K page)
   //

   if (!SS_configure_buffers(dig))
      {
      AIL_mem_free_DOS  (dig->DMA_buf, dig->DMA_seg, dig->DMA_sel);
      AIL_mem_free_lock (dig);

      return NULL;
      }

   //
   // Initialize device
   //

   AIL_call_driver(dig->drvr, DRV_INIT_DEV, NULL, NULL);

   dig->drvr->initialized = 1;

   //
   // If driver supplies a 16-bit protected-mode device service routine (DSR),
   // install it as a protected-mode ISR to help reduce interrupt overhead
   //
   // Disabled by default as of V3.00 release
   //

#if 0

   VDI.CX =  2;
   VDI.AX = -1;

   AIL_call_driver(dig->drvr, DIG_VSE, &VDI, &VDI);

   if (VDI.AX == 1)
      {
      dig->drvr->PM_ISR = (S32) VDI.BX;

      AIL_set_USE16_ISR(VDI.BX,
                        dig->drvr->seg,
                        VDI.CX);
      }

#endif

   //
   // Allocate build buffer
   //

   dig->samples_per_buffer = dig->buffer_size /
      (dig->channels_per_sample * dig->bytes_per_channel);

   dig->channels_per_buffer = dig->buffer_size /
       dig->bytes_per_channel;

   dig->build_size = sizeof(S32) * dig->channels_per_buffer;

   dig->build_buffer = (S32 *) AIL_mem_alloc_lock(dig->build_size);

   if (dig->build_buffer == NULL)
      {
      AIL_set_error("Could not allocate build buffer.");

      if (drvr->PM_ISR != -1)
         {
         AIL_restore_USE16_ISR(drvr->PM_ISR);
         }

      AIL_call_driver(dig->drvr, DRV_SHUTDOWN_DEV, NULL, NULL);

      dig->drvr->initialized = 0;

      AIL_mem_free_DOS  (dig->DMA_buf, dig->DMA_seg, dig->DMA_sel);
      AIL_mem_free_lock (dig);

      return NULL;
      }

   //
   // Allocate reverb buffer
   //

   if (AIL_allocate_reverb_buffers( dig ) == 0 )
   {
      AIL_set_error("Could not allocate decode buffer.");

      if (drvr->PM_ISR != -1)
         {
         AIL_restore_USE16_ISR(drvr->PM_ISR);
         }

      AIL_call_driver(dig->drvr, DRV_SHUTDOWN_DEV, NULL, NULL);

      dig->drvr->initialized = 0;

      AIL_mem_free_DOS  (dig->DMA_buf, dig->DMA_seg, dig->DMA_sel);
      AIL_mem_free_lock (dig->build_buffer);
      AIL_mem_free_lock((void FAR *) dig->reverb_build_buffer);
      AIL_mem_free_lock (dig);

      return NULL;
   }

   //
   // Allocate physical SAMPLE structures for driver
   //

   dig->n_samples = AIL_preference[DIG_MIXER_CHANNELS];

   dig->samples = (HSAMPLE) AIL_mem_alloc_lock(sizeof(SAMPLE) * dig->n_samples);

   if (dig->samples == NULL)
      {
      AIL_set_error("Could not allocate SAMPLE structures.");

      if (drvr->PM_ISR != -1)
         {
         AIL_restore_USE16_ISR(drvr->PM_ISR);
         }

      AIL_call_driver(dig->drvr, DRV_SHUTDOWN_DEV, NULL, NULL);

      dig->drvr->initialized = 0;

      AIL_mem_free_DOS  (dig->DMA_buf, dig->DMA_seg, dig->DMA_sel);
      AIL_mem_free_lock (dig->build_buffer);
      AIL_mem_free_lock((void FAR *) dig->reverb_build_buffer);
      AIL_mem_free_lock (dig);

      return NULL;
      }

   for (i=0; i < dig->n_samples; i++)
      {
      AIL_memset(&dig->samples[i],
                  0,
                  sizeof(SAMPLE));

      AIL_memcpy(dig->samples[i].tag,"HSAM",4);

      dig->samples[i].status = SMP_FREE;
      dig->samples[i].driver = dig;
      }

   //
   // Init driver pipeline stages
   //

   for (i=0; i < N_DIGDRV_STAGES; i++)
      {
      dig->pipeline[i].active = 0;
      }

   //
   // Select default mixer flush/copy providers
   //

   HPROVIDER HP;
   
   RIB_enumerate_providers("MSS mixer services",
                            NULL,
                           &HP);

   AIL_set_digital_driver_processor(dig,
                                    DP_FLUSH,
                                    HP);

   AIL_set_digital_driver_processor(dig,
                                    DP_COPY,
                                    HP);

   //
   // Allocate timer for DMA buffer service
   //

   dig->backgroundtimer = AIL_register_timer(SS_serve);

   if (dig->backgroundtimer == -1)
      {
      AIL_set_error("Out of timer handles.");

      if (drvr->PM_ISR != -1)
         {
         AIL_restore_USE16_ISR(drvr->PM_ISR);
         }

      AIL_call_driver(dig->drvr, DRV_SHUTDOWN_DEV, NULL, NULL);

      dig->drvr->initialized = 0;

      AIL_mem_free_DOS  (dig->DMA_buf, dig->DMA_seg, dig->DMA_sel);
      AIL_mem_free_lock (dig->samples);
      AIL_mem_free_lock (dig->build_buffer);
      AIL_mem_free_lock((void FAR *) dig->reverb_build_buffer);
      AIL_mem_free_lock (dig);

      return NULL;
      }

   AIL_set_timer_user(dig->backgroundtimer,(U32) dig);

   AIL_set_timer_frequency(dig->backgroundtimer,AIL_preference[DIG_SERVICE_RATE]);
   AIL_start_timer(dig->backgroundtimer);

   //
   // Set destructor handler and descriptor
   //

   dig->drvr->destructor = SS_destroy_DIG_driver;
   dig->drvr->descriptor = dig;

   //
   // Flush DMA buffers with silence
   //

   SS_flush(dig);

   SS_copy(dig, dig->DMA[ 0 ] );
   SS_copy(dig, dig->DMA[ 1 ] );

   //
   // Return DIG_DRIVER descriptor
   //

   return dig;
}

//############################################################################
//##                                                                        ##
//## Load, install, and initialize digital audio driver                     ##
//##                                                                        ##
//############################################################################

HDIGDRIVER AILCALL AIL_API_install_DIG_driver_file(char const *filename,      //)
                                                 IO_PARMS *IO)
{
   AIL_DRIVER *drvr;
   HDIGDRIVER  dig;
   S32        *driver_image;

   if ((driver_image = (S32 *) AIL_file_read(filename,FILE_READ_WITH_SIZE)) == NULL)
      {
      if (*AIL_redist_directory==0)
         {
         error:
         AIL_set_error("Driver file not found.");

         return NULL;
         }
       else
         {
          char fn[256];
          AIL_strcpy(fn, AIL_redist_directory);
          AIL_strcat(fn, filename);

          if ((driver_image = (S32 *) AIL_file_read(fn,FILE_READ_WITH_SIZE)) == NULL)
             goto error;
          }
      }

   drvr = AIL_install_driver( (U8*) (driver_image+1) ,*driver_image);

   AIL_mem_free_lock(driver_image);

   if (drvr == NULL)
      {
      return NULL;
      }

   dig = SS_construct_DIG_driver(drvr,IO);

   if (dig == NULL)
      {
      AIL_uninstall_driver(drvr);
      }

   return dig;
}

//############################################################################
//##                                                                        ##
//## Install and initialize digital audio driver from file image            ##
//##                                                                        ##
//############################################################################

HDIGDRIVER AILCALL AIL_API_install_DIG_driver_image(void const *driver_image, //)
                                                  U32     size,
                                                  IO_PARMS *IO)
{
   AIL_DRIVER *drvr;
   HDIGDRIVER dig;

   drvr = (AIL_DRIVER *) AIL_install_driver((U8 *) driver_image,size);

   if (drvr == NULL)
      {
      return NULL;
      }

   dig = SS_construct_DIG_driver(drvr,IO);

   if (dig == NULL)
      {
      AIL_uninstall_driver(drvr);
      }

   return dig;
}

//############################################################################
//##                                                                        ##
//## Load, install, and initialize digital audio driver according to        ##
//## contents of DIG_INI file                                               ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_install_DIG_INI(HDIGDRIVER *dig)
{
   AIL_INI ini;

   //
   // Attempt to read DIG_INI file
   //

   if (!AIL_read_INI(&ini, "DIG.INI"))
      {
      AIL_set_error("Unable to open file DIG.INI.");
      return AIL_NO_INI_FILE;
      }

   *dig = AIL_install_DIG_driver_file(ini.driver_name,
                                     &ini.IO);

   if (*dig == NULL)
      {
      return AIL_INIT_FAILURE;
      }

   return AIL_INIT_SUCCESS;
}

//############################################################################
//##                                                                        ##
//## Uninstall digital audio driver                                         ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_uninstall_DIG_driver(HDIGDRIVER dig)
{
   AIL_uninstall_driver(dig->drvr);
}

//############################################################################
//##                                                                        ##
//## Get digital driver configuration                                       ##
//##                                                                        ##
//############################################################################

void  AILCALL AIL_API_digital_configuration     (HDIGDRIVER dig, //)
                                               S32      *rate,
                                               S32      *format,
                                               char     *name)
{
   if (rate != NULL)
      {
      *rate = dig->DMA_rate;
      }

   if (format != NULL)
      {
      *format = dig->hw_format;
      }

   if (name != NULL)
      {
      AIL_strcpy(name, dig->drvr->VHDR->dev_name);
      }
}

S32 AILCALL AIL_API_digital_latency(HDIGDRIVER dig)
{
  if (dig)
  {
    return( mult64anddiv(dig->buffer_size,1000,dig->DMA_rate*
                         dig->channels_per_sample*dig->bytes_per_channel));
  }
  return(0);
}

//############################################################################
//##                                                                        ##
//## End of locked code                                                     ##
//##                                                                        ##
//############################################################################

void AILSS_end(void)
{
   if (locked)
      {
      AIL_vmm_unlock_range(AILSS_start, AILSS_end);

      UNLOCK (entry    );
      UNLOCK (dig      );
      UNLOCK (VDI      );
      UNLOCK (flags    );
      UNLOCK (cnt      );
      UNLOCK (n        );
      UNLOCK (current  );
      UNLOCK (S        );

      locked = 0;
      }
}

