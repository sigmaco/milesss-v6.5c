//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  MSS.C: C API module and support routines                              ##
//##                                                                        ##
//##  16-bit protected-mode source compatible with MSC 7.0                  ##
//##  32-bit protected-mode source compatible with MSC 9.0                  ##
//##                                                                        ##
//##  Version 1.00 of 15-Feb-95: Derived from AIL.C V1.00                   ##
//##          1.01 of  1-May-95: Moved WAILA functions here for portability ##
//##          1.02 of 15-Fed-96: Additions for multiple 16 bit loads (JKR)  ##
//##          1.03 of 11-Apr-96: Win32s updates (JKR)                       ##
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

#include "SDL.h"
#include "SDL_thread.h"

//
// HPROVIDER handle for internal RIB interface registration
//

HPROVIDER MSS_INTERNAL;

char MSS_Directory[260] = ".";

//
// AIL "preferences" array
//

S32 AIL_preference[N_PREFS];

//
// ASCII error type string
//

C8 AIL_error[256];

//
// The main thread
//
Uint32 AIL_foreground_thread = 0;

//
// DIG_DRIVER list
//

HDIGDRIVER DIG_first = NULL;

//
// MDI_DRIVER list
//

HMDIDRIVER MDI_first = NULL;

//
// Timer array
//

static struct _AILTIMER FAR *timers = NULL;
static S32                   n_timers;

//
// Period of base multimedia timer in uS
//

static S32                   timer_period;

//
// Locking count
//

static S32                   lock_count=0;


//############################################################################
//##                                                                        ##
//## Set AIL operational preferences and policies                           ##
//##                                                                        ##
//## May be called by applications which need to alter the default          ##
//## behavior of the AIL system                                             ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_set_preference(U32 number, S32 value)
{
   S32 old;

   old = AIL_preference[number];

#ifdef IS_WIN16
   if (number != DIG_USE_WAVEOUT)
#endif

   AIL_preference[number] = value;

   return old;
}

//############################################################################
//##                                                                        ##
//## Get AIL operational preferences and policies                           ##
//##                                                                        ##
//############################################################################

DXDEF
S32 AILEXPORT AIL_get_preference(U32 number)
{
   return AIL_preference[number];
}

static void init_preferences()
{
   AIL_preference[DIG_MIXER_CHANNELS]=        DEFAULT_DMC;
   AIL_preference[DIG_MAX_PREDELAY_MS]=       DEFAULT_MPDMS;
   AIL_preference[DIG_RESAMPLING_TOLERANCE]=  DEFAULT_DRT;
   AIL_preference[DIG_OUTPUT_BUFFER_SIZE]=    DEFAULT_DOBS;
   AIL_preference[DIG_ENABLE_RESAMPLE_FILTER]=DEFAULT_DERF;

   AIL_preference[MDI_SERVICE_RATE]=          DEFAULT_MSR;
   AIL_preference[MDI_SEQUENCES]=             DEFAULT_MS;
   AIL_preference[MDI_DEFAULT_VOLUME]=        DEFAULT_MDV;
   AIL_preference[MDI_QUANT_ADVANCE]=         DEFAULT_MQA;
   AIL_preference[MDI_ALLOW_LOOP_BRANCHING]=  DEFAULT_ALB;
   AIL_preference[MDI_DEFAULT_BEND_RANGE]=    DEFAULT_MDBR;
   AIL_preference[MDI_SYSEX_BUFFER_SIZE]=     DEFAULT_MSBS;
   AIL_preference[MDI_DOUBLE_NOTE_OFF]=       DEFAULT_MDNO;

   AIL_preference[AIL_MM_PERIOD]=             DEFAULT_AMP;
   AIL_preference[AIL_TIMERS]=                DEFAULT_AT;

   AIL_preference[AIL_MUTEX_PROTECTION]=      DEFAULT_AMPR;

   AIL_preference[DLS_TIMEBASE]=            DEFAULT_DTB;
   AIL_preference[DLS_VOICE_LIMIT]=         DEFAULT_DVL;
   AIL_preference[DLS_BANK_SELECT_ALIAS]=   DEFAULT_DBSA;
   AIL_preference[DLS_STREAM_BOOTSTRAP]=    DEFAULT_DSB;
   AIL_preference[DLS_VOLUME_BOOST]=        DEFAULT_DVB;
   AIL_preference[DLS_ENABLE_FILTERING]=    DEFAULT_DEF;
   AIL_preference[AIL_ENABLE_MMX_SUPPORT]=  DEFAULT_AEMS;
   AIL_preference[DLS_GM_PASSTHROUGH]=      DEFAULT_DGP;

#ifdef OLD_DLS_REVERB_PREFERENCES
   AIL_preference[DLS_ENABLE_GLOBAL_REVERB]=DEFAULT_DEGR;
   AIL_preference[DLS_GLOBAL_REVERB_LEVEL]= DEFAULT_GRL;
   AIL_preference[DLS_GLOBAL_REVERB_TIME]=  DEFAULT_GRT;
#endif

   AIL_preference[DLS_ADPCM_TO_ASI_THRESHOLD]=  DEFAULT_DATAT;

   AIL_preference[DIG_INPUT_LATENCY]=DEFAULT_DIL;

   AIL_error[0]   = 0;
}

//############################################################################
//##                                                                        ##
//## Initialize AIL API modules and resources                               ##
//##                                                                        ##
//## Must be called prior to any other AIL_...() calls!                     ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_startup(void)
{
   init_preferences();

   //
   // Get handle for registration of internal RIBs
   //

   MSS_INTERNAL = RIB_alloc_provider_handle(0);

   //
   // Initialize default internal mixer RIB
   //

   const RIB_INTERFACE_ENTRY MIXER[] =
      {
      { RIB_FUNCTION, "MIXER_startup",  (U32) &MSS_mixer_startup,  RIB_NONE },
      { RIB_FUNCTION, "MIXER_shutdown", (U32) &MSS_mixer_shutdown, RIB_NONE },
      { RIB_FUNCTION, "MIXER_flush",    (U32) &MSS_mixer_flush,    RIB_NONE },
      { RIB_FUNCTION, "MIXER_merge",    (U32) &MSS_mixer_merge,    RIB_NONE },
      { RIB_FUNCTION, "MIXER_copy",     (U32) &MSS_mixer_copy,     RIB_NONE }
      };

   RIB_register(MSS_INTERNAL,
               "MSS mixer services",
                MIXER);

   //
   // Load and initialize external MSS RIBs
   //

   HPROVIDER PROVIDER;
   HPROENUM  next;

   //
   // Load and start all available mixer providers
   //

   RIB_load_application_providers("*.mix");

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("MSS mixer services",
                                  &next,
                                  &PROVIDER))
      {
      MIXER_STARTUP MIXER_startup;

      if (RIB_request_interface_entry(PROVIDER,
                                     "MSS mixer services",
                                      RIB_FUNCTION,
                                     "MIXER_startup",
                         (U32 FAR *) &MIXER_startup) == RIB_NOERR)
         {
         MIXER_startup();
         }
      }

   //
   // Load and start all available ASI providers
   //

   RIB_load_application_providers("*.asi");

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("ASI codec",
                                  &next,
                                  &PROVIDER))
      {
      ASI_STARTUP ASI_startup;

      if (RIB_request_interface_entry(PROVIDER,
                                     "ASI codec",
                                      RIB_FUNCTION,
                                     "ASI_startup",
                         (U32 FAR *) &ASI_startup) == RIB_NOERR)
         {
         ASI_startup();
         }
      }

   //
   // Load and start all available M3D providers
   //

   RIB_load_application_providers("*.m3d");

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("MSS 3D audio services",
                                  &next,
                                  &PROVIDER))
      {
      M3D_STARTUP M3D_startup;

      if (RIB_request_interface_entry(PROVIDER,
                                     "MSS 3D audio services",
                                      RIB_FUNCTION,
                                     "M3D_startup",
                         (U32 FAR *) &M3D_startup) == RIB_NOERR)
         {
         M3D_startup();
         }
      }

   //
   // Load and start all available FLT providers
   //

   FLT_init_list();    // Initialize driver-association list so we can clean up at shutdown time

   RIB_load_application_providers("*.flt");

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("MSS pipeline filter",
                                  &next,
                                  &PROVIDER))
      {
      FLT_STARTUP FLT_startup;

      if (RIB_request_interface_entry(PROVIDER,
                                     "MSS pipeline filter",
                                      RIB_FUNCTION,
                                     "FLT_startup",
                         (U32 FAR *) &FLT_startup) == RIB_NOERR)
         {
         FLT_startup();
         }
      }
}

//############################################################################
//##                                                                        ##
//## Background timer server                                                ##
//##                                                                        ##
//############################################################################

U32 lastapitimerms=0;
static S32 timer_busy = 0;
static U32 highest_timer_delay=0;

void API_timer ()
{
   static S32 i;
   U32 tme,diff;

   tme=AIL_ms_count();

   //
   // If timer services uninitialized or locked, or reentry attempted, exit
   //

   MSSLockedIncrement(timer_busy);


   if ((timers == NULL) || (lock_count > 0) || (timer_busy!=1))
      {
      goto resumethreadandexit;
      }

   //
   // Advance all running timers
   //

   if (lastapitimerms==0) {
     diff=timer_period;
     lastapitimerms=tme;
   } else {
     diff=tme-lastapitimerms;

     if (diff>highest_timer_delay)
       highest_timer_delay=diff;

     if (diff>100)
       diff=100;
     diff*=1000;
   }
   lastapitimerms=tme;

   for (i=0; i < n_timers; i++)
      {
      //
      // Skip timer if not active
      //

      if (timers[i].status != AILT_RUNNING)
         {
         continue;
         }

      //
      // Add base MME timer period to timer's accumulator
      //

      timers[i].elapsed += diff;

      //
      // If elapsed time >= timer's period, reset timer and
      // trigger callback function
      //

      while (timers[i].elapsed >= timers[i].value)
         {
         timers[i].elapsed -= timers[i].value;

         //
         // Invoke timer callback function with specified user value
         //

         MSS_do_cb1( (AILTIMERCB), timers[i].callback, timers[i].callingDS, timers[i].user);

         // check again, in case they canceled the time in the background
         if (timers[i].status != AILT_RUNNING)
         {
           break;
         }

         }
      }

 resumethreadandexit:

   //
   // Enable future timer calls
   //

   MSSLockedDecrement(timer_busy);
}

//############################################################################
//##                                                                        ##
//## Thread to call timer services                                          ##
//##                                                                        ##
//############################################################################

static SDL_Thread *thread_hand=0;
static volatile int thread_exit=0;

int TMR_thread(void *user)
{
  while (!thread_exit) {

    SDL_Delay(1); /* FIXME: we should probably delay the minimum period */

    if (AIL_preference[AIL_MUTEX_PROTECTION]==0)
    {
      API_timer();
    }
    else
    {
      AIL_lock_mutex();
      API_timer();
      AIL_unlock_mutex();
    }

  }

  return(0);
}

//############################################################################
//##                                                                        ##
//## Shut down AIL API modules and resources                                ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_shutdown(void)
{
   HDIGDRIVER dig,d;
   HMDIDRIVER mdi,m;
   S32 i;

   //
   // Shut down any active MIDI drivers
   //

#ifdef IS_WIN32

   mdi = MDI_first;

   while (mdi != NULL)
      {
      m = mdi->next;

        AIL_midiOutClose(mdi);

      mdi = m;
      }
#endif

   //
   // Shut down all M3D providers
   //

   HPROVIDER PROVIDER;
   HPROENUM next;

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("MSS 3D audio services",
                                  &next,
                                  &PROVIDER))
      {
      M3D_SHUTDOWN M3D_shutdown;

      if (RIB_request_interface_entry(PROVIDER,
                                     "MSS 3D audio services",
                                      RIB_FUNCTION,
                                     "M3D_shutdown",
                         (U32 FAR *) &M3D_shutdown) == RIB_NOERR)
         {
         M3D_shutdown();
         }
      }

   //
   // Shut down any active wave drivers
   //

   AIL_lock();

#ifdef IS_WIN32

   dig = DIG_first;

   while (dig != NULL)
      {
      d = dig->next;

        AIL_waveOutClose(dig);

      dig = d;
      }
#endif

   //
   // Shut down all FLT providers
   //

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("MSS pipeline filter",
                                  &next,
                                  &PROVIDER))
      {
      FLT_SHUTDOWN FLT_shutdown;

      if (RIB_request_interface_entry(PROVIDER,
                                     "MSS pipeline filter",
                                      RIB_FUNCTION,
                                     "FLT_shutdown",
                         (U32 FAR *) &FLT_shutdown) == RIB_NOERR)
         {
         FLT_shutdown();
         }
      }

   //
   // Shut down all ASI providers
   //

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("ASI codec",
                                  &next,
                                  &PROVIDER))
      {
      ASI_SHUTDOWN ASI_shutdown;

      if (RIB_request_interface_entry(PROVIDER,
                                     "ASI codec",
                                      RIB_FUNCTION,
                                     "ASI_shutdown",
                         (U32 FAR *) &ASI_shutdown) == RIB_NOERR)
         {
         ASI_shutdown();
         }
      }

   //
   // Shut down all MIXER providers
   //

   next = HPROENUM_FIRST;

   while (RIB_enumerate_providers("MSS mixer services",
                                  &next,
                                  &PROVIDER))
      {
      MIXER_SHUTDOWN MIXER_shutdown;

      if (RIB_request_interface_entry(PROVIDER,
                                     "MSS mixer services",
                                      RIB_FUNCTION,
                                     "MIXER_shutdown",
                         (U32 FAR *) &MIXER_shutdown) == RIB_NOERR)
         {
         MIXER_shutdown();
         }
      }

   //
   // Unregister all internal RIBs
   //

   RIB_unregister_all(MSS_INTERNAL);

   //
   // Release all loaded libraries
   //

   RIB_free_libraries();

   //
   // Shut down timer services
   //

   if (timers != NULL)
      {
      AIL_release_all_timers();

      for (i=0; i < n_timers; i++)           // don't free the timer system
        if (timers[i].status != AILT_FREE)   // if any timers remain
          goto done;

      //
      // Stop background thread
      //

      if (thread_hand) {
        thread_exit = 1;
        SDL_WaitThread(thread_hand, NULL);
        thread_hand=0;
      }

      //
      // Free timer array
      //

      AIL_mem_free_lock(timers);

      timers = NULL;

      }

  done:
   AIL_unlock();
}

//###########################################################################
//##                                                                        ##
//## Lock/unlock AIL timer service (to enable atomic operations)            ##
//##                                                                        ##
//############################################################################

DXDEF
void    AILEXPORT AIL_lock                      (void)
{
   MSSLockedIncrement(lock_count);
}

DXDEF
void    AILEXPORT AIL_unlock                    (void)
{
   MSSLockedDecrement(lock_count);
}

DXDEF
void    AILEXPORT AIL_lock_mutex                (void)
{
  InMilesMutex();
}

DXDEF
void    AILEXPORT AIL_unlock_mutex              (void)
{
  OutMilesMutex();
}

//############################################################################
//##                                                                        ##
//## System-independent delay in 1/60 second intervals                      ##
//##                                                                        ##
//## Returns at once if called from background                              ##
//##                                                                        ##
//############################################################################

void    AILCALL AIL_API_delay                     (S32         intervals)
{
   if (AIL_API_background())
      {
      return;
      }

   SDL_Delay(16*intervals);
}

//############################################################################
//##                                                                        ##
//## Returns TRUE if called from within timer handler or callback function  ##
//##                                                                        ##
//############################################################################

S32     AILCALL AIL_API_background                (void)
{
   return SDL_ThreadID() != AIL_foreground_thread;
}


//############################################################################
//##                                                                        ##
//## Register a timer                                                       ##
//##                                                                        ##
//############################################################################

HTIMER  AILCALL AIL_API_register_timer            (AILTIMERCB    fn)
{
   S32 i;

   //
   // If timer array has not yet been allocated, allocate and initialize it
   //

   if (timers == NULL)
      {
      n_timers = AIL_preference[AIL_TIMERS];

      timers = (struct _AILTIMER FAR *) AIL_mem_alloc_lock(n_timers * sizeof(struct _AILTIMER));

      if (timers == NULL)
         {
         return -1;
         }

      //
      // Mark all timers free
      //

      for (i=0; i < n_timers; i++)
         {
         timers[i].status = AILT_FREE;

         }

      //
      // Start background thread to check for foreground service
      //
      thread_exit=0;
      thread_hand=SDL_CreateThread(TMR_thread,(void *)&i);
      }

   //
   // Find a free timer, if possible, and return its handle
   //

   for (i=0; i < n_timers; i++)
      {
      if (timers[i].status == AILT_FREE)
         {
         break;
         }
      }

   //
   // If no free timers, return -1
   //

   if (i == n_timers)
      {
      return -1;
      }

   //
   // Otherwise, mark timer "stopped" and record callback address
   //

   timers[i].status = AILT_STOPPED;

   timers[i].callback = fn;

   //
   // Set default rate of 100 Hz
   //

   timers[i].value   = 10000;
   timers[i].elapsed = 0;

   return i;
}

//############################################################################
//##                                                                        ##
//## Set timer user word                                                    ##
//##                                                                        ##
//############################################################################

U32     AILCALL AIL_API_set_timer_user            (HTIMER      timer,      //)
                                                   U32         user)
{
   U32 temp;

   if (timer == -1)
      {
      return 0;
      }

   temp = timers[timer].user;

   timers[timer].user = user;

   return temp;
}

//############################################################################
//##                                                                        ##
//## Set timer period in microseconds, frequency in hertz, or equivalent    ##
//## interrupt divisor value                                                ##
//##                                                                        ##
//############################################################################

void    AILCALL AIL_API_set_timer_period          (HTIMER      timer,       //)
                                                   U32         microseconds)
{
   if (timer == -1)
      {
      return;
      }

   //
   // Begin atomic operation
   //

   AIL_lock();

   //
   // Reset timer and set new period in microseconds
   //

   timers[timer].elapsed = 0;
   timers[timer].value   = microseconds;

   //
   // End atomic operation
   //

   AIL_unlock();
}


void    AILCALL AIL_API_set_timer_frequency       (HTIMER      timer,      //)
                                                   U32         hertz)
{
   if (timer == -1)
      {
      return;
      }

   AIL_set_timer_period(timer,
                        1000000 / hertz);
}


void    AILCALL AIL_API_set_timer_divisor         (HTIMER      timer,      //)
                                                   U32         PIT_divisor)
{
   if (timer == -1)
      {
      return;
      }

   //
   // Ensure 100% precision with zero case
   //

   if (PIT_divisor == 0)
      {
      AIL_set_timer_period(timer,
                           54925);
      }
   else
      {
      AIL_set_timer_period(timer,
                           (PIT_divisor * 10000) / 11932);
      }
}

//############################################################################
//##                                                                        ##
//## Start timer(s)                                                         ##
//##                                                                        ##
//############################################################################

void    AILCALL AIL_API_start_timer               (HTIMER      timer)
{
   if (timer == -1)
      {
      return;
      }

   if (timers[timer].status == AILT_STOPPED)
      {
      timers[timer].status = AILT_RUNNING;
      }
}

void    AILCALL AIL_API_start_all_timers          (void)
{
   S32 i;

   for (i=0; i < n_timers; i++)
      {

      AIL_start_timer(i);
      }
}


//############################################################################
//##                                                                        ##
//## Stop timer(s)                                                          ##
//##                                                                        ##
//############################################################################

void    AILCALL AIL_API_stop_timer                (HTIMER      timer)
{
   if (timer == -1)
      {
      return;
      }

   if (timers[timer].status == AILT_RUNNING)
      {
      timers[timer].status = AILT_STOPPED;
      }

}

void    AILCALL AIL_API_stop_all_timers           (void)
{
   S32 i;

   for (i=0; i < n_timers; i++)
      {

      AIL_stop_timer(i);
      }
}


//############################################################################
//##                                                                        ##
//## Release timer handle(s)                                                ##
//##                                                                        ##
//############################################################################

void    AILCALL AIL_API_release_timer_handle      (HTIMER      timer)
{
   if (timer == -1)
      {
      return;
      }

   timers[timer].status = AILT_FREE;
}

void    AILCALL AIL_API_release_all_timers        (void)
{
   S32 i;

   for (i=0; i < n_timers; i++)
      {

      AIL_release_timer_handle(i);
      }
}


DXDEF U32 AILEXPORT AIL_get_timer_highest_delay   (void)
{
  if (highest_timer_delay<(U32)AIL_preference[AIL_MM_PERIOD])
    return(0);
  else
  {
    U32 ret=highest_timer_delay-AIL_preference[AIL_MM_PERIOD];
    highest_timer_delay=0;
    return( ret );
  }
}


//############################################################################
//##                                                                        ##
//## Error text handling routines                                           ##
//##                                                                        ##
//############################################################################


void AILCALL AIL_API_set_error(C8 const FAR * error_msg)
{
  AIL_strcpy(AIL_error,error_msg);
}


C8 FAR * AILCALL AIL_API_last_error(void)
{
   return(AIL_error);
}

