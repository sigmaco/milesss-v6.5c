//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  mssdig.C: Digital Sound module for SDL audio output                   ##
//##                                                                        ##
//##  16-bit protected-mode source compatible with MSC 7.0                  ##
//##  32-bit protected-mode source compatible with MSC 9.0                  ##
//##                                                                        ##
//##  Version 1.00 of 15-Feb-95: Derived from AILSS.C V1.03                 ##
//##          1.01 of 19-Jun-95: Stereo tracks panned for mono output       ##
//##                             Use multiply/shift for 16-bit scaling      ##
//##                             Digital master volume added                ##
//##                             AIL_resume_sample() restarts driver        ##
//##          1.02 of 16-Jul-95: Win95 thread synchronization added         ##
//##          1.03 of 21-Nov-95: API brought up to DOS 3.03C level          ##
//##                             Changed synchronization methods            ##
//##          1.04 of 15-Feb-96: Fixes for optimization and multiple        ##
//##                             16 bit loads (JKR)                         ##
//##          1.05 of 11-Apr-96: Added background thread checking (JKR)     ##
//##          1.06 of 11-May-97: Added IMA ADPCM support (Serge Plagnol)    ##
//##          1.10 of 10-Jun-98: Adapted for use with new mixer, many       ##
//##                             changes (JM)                               ##
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

#include "mss.h"
#include "imssapi.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include "SDL.h"
#include "SDL_audio.h"

//
// Assembly-language functions
//

#define SETTAG(var,tag) AIL_memcpy(var,tag,4);

//
// Flag to disable background and foreground callbacks
//

static volatile S32 disable_callbacks = 0;


//
// Flag to keep SS_serve from reentering
//

static volatile S32 SS_servicing = 0;

//
// Primary digital driver (the one used with 3D providers and other
// future single-driver extensions)
//

extern "C"
{
HDIGDRIVER primary_digital_driver = NULL;
}

// ------------------------------------------------------------------
// DIG_closeAudio
// ------------------------------------------------------------------

void DIG_closeAudio(HDIGDRIVER dig)
{
   MSSLockedIncrement(disable_callbacks);

   dig->playing=0;
   dig->released=1;

   SDL_CloseAudio();
   MSSLockedDecrement(disable_callbacks);
}

//############################################################################
//##                                                                        ##
//## Start driver-based DMA buffer playback                                 ##
//##                                                                        ##
//############################################################################

void AILCALL SS_start_DIG_driver_playback(HDIGDRIVER dig)
{
   //
   // Return if playback already active
   //

   if (dig->playing)
      {
      return;
      }

   //
   // Set playing flag
   //

   dig->playing = 1;

   SDL_PauseAudio(0);
}

//############################################################################
//##                                                                        ##
//## Stop driver-based DMA buffer playback                                  ##
//##                                                                        ##
//############################################################################

static void SS_stop_DIG_driver_playback(HDIGDRIVER dig)
{
   if (!dig->playing)
      {
      return;
      }

   //
   // Stop playback ASAP and return all buffers
   //

   dig->playing = 0;

   SDL_PauseAudio(1);
}

//############################################################################
//##                                                                        ##
//## Fill output buffer with mixed data and/or silence                      ##
//##                                                                        ##
//############################################################################

static void SS_fill(HDIGDRIVER dig, void FAR *lpData)
{
   static S32        cnt,n;
   static HSAMPLE    S;

   U32 start_us = AIL_us_count();
   S32 current_ms = start_us/1000;

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
      // If ASI codec is in use, buffer maintenance can take place within
      // either SS_stream_to_buffer() or the ASI fetch callback
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

   SS_copy(dig, lpData);

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
//## Timer callback function to mix data into output buffers                ##
//##                                                                        ##
//############################################################################

static void SS_serve(HDIGDRIVER dig, void FAR *lpData)
{
   //
   // Return immediately if callbacks disabled or driver not actively playing
   //

   if (disable_callbacks)
      {
      return;
      }

   if (!dig->playing)
      {
      return;
      }

   //
   // Increment background count so callback functions will run in
   // background
   //

   MSSLockedIncrement(SS_servicing);

   if (SS_servicing==1)
   {
      //
      // Mix data into audio stream
      //

      SS_fill(dig, lpData);
   }

   MSSLockedDecrement(SS_servicing);
}

//############################################################################
//##                                                                        ##
//## SetTimer timer to periodically call the foreground servicing routine   ##
//##                                                                        ##
//############################################################################

extern "C" void stream_background(void); // background service for streaming

void SS_SDLAudioMix(void *userdata, Uint8 *stream, int len)
{
   HDIGDRIVER dig=primary_digital_driver;

   // Check the size of the audio stream
   if(!dig || len != dig->buffer_size)
     return;

   stream_background();

   // check all the buffer status's
   SS_serve(dig, stream);
}


//############################################################################
//##                                                                        ##
//## Initialize Windows waveOut driver and allocate output buffers          ##
//##                                                                        ##
//############################################################################

HDIGDRIVER AILCALL AIL_API_open_digital_driver(U32 frequency, S32 bits, S32 channels, U32 flags)
{
   S32           i;
   HDIGDRIVER    dig;
   SDL_AudioSpec spec;

   //
   // Allocate memory for DIG_DRIVER structure
   //

   dig = (HDIGDRIVER) AIL_mem_alloc_lock(sizeof(struct _DIG_DRIVER));

   if (dig == NULL)
      {
      AIL_set_error("Could not allocate memory for driver descriptor.");

      return NULL;
      }

   //
   // Explicitly initialize all DIG_DRIVER fields to NULL/0
   //

   AIL_memset(dig,
          0,
          sizeof(*dig));

   SETTAG(dig->tag,"HDIG");

   //
   // Check for MMX support if enabled
   //

   dig->use_MMX = AIL_MMX_available();

   //
   // Attempt to open wave output device
   //

   spec.freq = frequency;
   spec.format = bits == 8 ? AUDIO_U8 : AUDIO_S16;
   spec.channels = channels;
   spec.samples = 1024; // FIXME: make this a configuration parameter
   spec.callback = SS_SDLAudioMix;

   if ( SDL_OpenAudio(&spec, NULL) < 0 ) {
      AIL_mem_free_lock(dig);

      AIL_set_error(SDL_GetError());
      return NULL;
   }
   dig->buffer_size = spec.size;

   //
   // Init miscellaneous buffer variables
   //

   dig->playing          = 0;
   dig->released         = 0;
   dig->quiet            = 0;

   //
   // Set sample rate and size values and calling params
   //

   dig->DMA_rate            = frequency;
   dig->channels_per_sample = channels;
   dig->bytes_per_channel   = bits / 8;

   if (dig->bytes_per_channel == 1)
      {
      dig->hw_mode_flags = 0;

      if (dig->channels_per_sample == 1)
         {
         dig->hw_format = DIG_F_MONO_8;
         }
      else
         {
         dig->hw_format = DIG_F_STEREO_8;
         }
      }
   else
      {
      dig->hw_mode_flags = DIG_PCM_SIGN;

      if (dig->channels_per_sample == 1)
         {
         dig->hw_format = DIG_F_MONO_16;
         }
      else
         {
         dig->hw_format = DIG_F_STEREO_16;
         }
      }

   //
   // Exact buffer size is already known
   //

   dig->channels_per_buffer = dig->buffer_size / dig->bytes_per_channel;

   dig->samples_per_buffer = dig->channels_per_buffer / dig->channels_per_sample;

   //
   // Allocate build buffer
   //

   dig->build_size = sizeof(S32) * dig->channels_per_buffer;

   dig->build_buffer = (S32 FAR *) AIL_mem_alloc_lock(dig->build_size);

   if (dig->build_buffer == NULL)
      {
      AIL_set_error("Could not allocate build buffer.");

      DIG_closeAudio(dig);
      AIL_mem_free_lock(dig);

      return NULL;
      }

   if (AIL_allocate_reverb_buffers( dig ) == 0 )
   {
      AIL_set_error("Could not allocate reverb build buffer.");

      DIG_closeAudio(dig);
      AIL_mem_free_lock(dig->build_buffer);
      AIL_mem_free_lock(dig);

      return NULL;
   }

   //
   // Allocate physical SAMPLE structures for driver
   //

   dig->n_samples        = AIL_get_preference(DIG_MIXER_CHANNELS);
   dig->n_active_samples = 0;

   dig->master_volume    = 1.0F;
   dig->master_dry       = 1.0F;
   dig->master_wet       = 1.0F;

   dig->samples = (HSAMPLE) AIL_mem_alloc_lock(sizeof(struct _SAMPLE) * dig->n_samples);

   if (dig->samples == NULL)
      {
      AIL_set_error("Could not allocate SAMPLE structures.");

      AIL_mem_free_lock(dig->build_buffer);
      AIL_mem_free_lock(dig->reverb_build_buffer);
      DIG_closeAudio(dig);
      AIL_mem_free_lock(dig);

      return NULL;
      }

   for (i=0; i < dig->n_samples; i++)
      {
      AIL_memset(&dig->samples[i],
                  0,
                  sizeof(struct _SAMPLE));

      SETTAG(dig->samples[i].tag,"HSAM");

      dig->samples[i].status = SMP_FREE;
      dig->samples[i].driver = dig;
      }

   //
   // Link HDIGDRIVER into chain used by timers
   //

   AIL_primary_digital_driver(dig);

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
                                    DP_DEFAULT_FILTER,
                                    0);

   AIL_set_digital_driver_processor(dig,
                                    DP_DEFAULT_MERGE,
                                    HP);

   AIL_set_digital_driver_processor(dig,
                                    DP_FLUSH,
                                    HP);

   AIL_set_digital_driver_processor(dig,
                                    DP_COPY,
                                    HP);

   //
   // Return normally
   //

   return dig;
}

//############################################################################
//##                                                                        ##
//## Shut down Windows waveOut driver and free output buffers               ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_close_digital_driver(HDIGDRIVER dig)
{
   S32        i;

   if (dig == NULL)
      {
      return;
      }

   AILSTRM_shutdown(dig);

   MSSLockedIncrement( SS_servicing );

   while ( SS_servicing != 1 )
   {
     SDL_Delay( 1 );
   }

   //
   // Disable callback processing
   //

   MSSLockedIncrement(disable_callbacks);

   //
   // Stop playback
   //

   if (!dig->released)
     SS_stop_DIG_driver_playback(dig);

   //
   // Release any open sample handles (to ensure that pipeline resources
   // are deallocated properly)
   //

   for (i=0; i < dig->n_samples; i++)
      {
      if (dig->samples[i].status != SMP_FREE)
         {
         AIL_release_sample_handle(&dig->samples[i]);
         }
      }


   //
   // Release any filters associated with this driver
   //

   FLT_disconnect_driver(dig);

   //
   // Unlink from foreground service chain
   //

   AIL_primary_digital_driver(NULL);

   //
   // Close driver and free resources
   //

   if (!dig->released)
     DIG_closeAudio(dig);

   AIL_mem_free_lock((void FAR *) dig->samples);
   AIL_mem_free_lock((void FAR *) dig->build_buffer);
   AIL_mem_free_lock((void FAR *) dig->reverb_build_buffer);

   MSSLockedDecrement( SS_servicing );

   MSSLockedDecrement(disable_callbacks);

   AIL_mem_free_lock(dig);
}

//############################################################################
//##                                                                        ##
//## Temporarily release the Windows HWAVEOUT device                        ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_digital_handle_release(HDIGDRIVER dig)
{
   if (dig == NULL)
      {
      dig = primary_digital_driver;
      }

   if (dig == NULL)
      {
      return(0);
      }

   if (dig->released)
     return(1);

   //
   // Disable callback processing
   //

   MSSLockedIncrement(disable_callbacks);

   //
   // Stop playback
   //

   SS_stop_DIG_driver_playback(dig);

   //
   // Close driver
   //

   dig->playing=0;

   dig->released=1;

   MSSLockedDecrement(disable_callbacks);

   return(1);
}

//############################################################################
//##                                                                        ##
//## Reacquire the Windows HWAVEOUT device                                  ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_digital_handle_reacquire(HDIGDRIVER dig)
{
   S32 i;
   S32 playing=0;

   if (dig == NULL)
      {
      dig = primary_digital_driver;
      }

   if (dig == NULL)
      {
      return(0);
      }

   if (!dig->released)
     return(1);

   //
   // Disable callback processing
   // 

   MSSLockedIncrement(disable_callbacks);

   dig->released=0;

   if (playing)
     SS_start_DIG_driver_playback(dig);

   dig->released=0;

   MSSLockedDecrement(disable_callbacks);

   return(1);
}

//############################################################################
//##                                                                        ##
//## Externally-callable service function for foreground timer              ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_serve()
{
}


//############################################################################
//##                                                                        ##
//## Get digital driver configuration                                       ##
//##                                                                        ##
//############################################################################

void     AILCALL AIL_API_digital_configuration     (HDIGDRIVER dig, //)
                                            S32    FAR*rate,
                                            S32    FAR*format,
                                            char   FAR*config)
{
   if (dig==NULL)
     return;

   if (rate != NULL)
      {
      *rate = dig->DMA_rate;
      }

   if (format != NULL)
      {
      *format = dig->hw_format;
      }
   if (config != NULL)
      {
      SDL_AudioDriverName(config, 128);
      }
}

S32 AILCALL AIL_API_digital_latency(HDIGDRIVER dig)
{
printf("FIXME: AIL_API_digital_latency\n");
  return(0);
}

