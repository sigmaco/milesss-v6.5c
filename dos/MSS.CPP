//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  MSS.C: C API module and support routines                              ##
//##                                                                        ##
//##  Flat-model source compatible with IBM 32-bit ANSI C/C++               ##
//##                                                                        ##
//##  Version 1.00 of 3-Apr-94: Initial version for MSS 3.0                 ##
//##          1.01 of 9-Jun-95: Borland/Zortech support added               ##
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

#include "mss.h"
#include "imssapi.h"

#include <stdio.h>
#include <string.h>

//
// Private AILA.ASM prototypes
//

extern "C"
{
extern void cdecl AILA_startup(void);
extern void cdecl AILA_shutdown(void);
}

//
// External hook to register MPEG support
//

extern "C" S32 AILCALL MPEG_register(S32 fdwReason, HPROVIDER provider_handle);

//############################################################################
//##                                                                        ##
//## Locked global data                                                     ##
//##                                                                        ##
//############################################################################

//
// HPROVIDER handle for internal RIB interface registration
//

HPROVIDER MSS_INTERNAL;

//
// List of installed AIL drivers
//

AIL_DRIVER * AIL_driver[MAX_DRVRS];

//
// AIL "preferences" array
//

S32 AIL_preference[N_PREFS];

//
// ASCII error type string
//

C8 AIL_error[256];

//
// Last IO_PARMS structure used to attempt device detection
//

IO_PARMS AIL_last_IO_attempt;

//############################################################################
//##                                                                        ##
//## Locked static data                                                     ##
//##                                                                        ##
//############################################################################

//
// AIL_driver_server()
//

static U32 entry = 0;
static S32  flags;

//############################################################################
//##                                                                        ##
//## Locked code                                                            ##
//##                                                                        ##
//############################################################################

#define LOCK(x)   AIL_vmm_lock  (&(x),sizeof(x))
#define UNLOCK(x) AIL_vmm_unlock(&(x),sizeof(x))

static S32 locked = 0;

void AIL_end(void);

void AIL_start(void)
{
   if (!locked)
      {
      AIL_vmm_lock_range(AIL_start, AIL_end);

      LOCK (AIL_driver[0]      );
      LOCK (AIL_preference[0]  );
      LOCK (AIL_error[0]       );
      LOCK (AIL_last_IO_attempt);
      LOCK (entry              );
      LOCK (flags              );
      LOCK (MSS_INTERNAL       );

      locked = 1;
      }
}

//############################################################################
//##                                                                        ##
//## Timer interrupt routine to provide periodic service to driver, if      ##
//## requested                                                              ##
//##                                                                        ##
//############################################################################

static void AILCALLBACK AIL_driver_server(U32 drvr)
{
   //
   // Disallow re-entrant calls and interrupts, switch to local stack,
   // call driver, restore stack and interrupt status, and return
   //

   if (entry) return;
   entry = 1;

   flags = AIL_disable_interrupts();

   AIL_call_driver((AIL_DRIVER*)drvr, DRV_SERVE, NULL, NULL);

   AIL_restore_interrupts(flags);

   entry = 0;
}

//############################################################################
//##                                                                        ##
//## Evaluate ASCII string in specified numeric base, optionally writing    ##
//## end-of-value pointer to **end                                          ##
//##                                                                        ##
//## Disregards whitespace                                                  ##
//##                                                                        ##
//############################################################################

static S32 cdecl ASC_val(char *str, S32 base, char **end)
{
   S32 i,j;
   S32 total;
   S32 sign;
   char c;
   
   total = 0;
   sign  = 1;

   for (i=0; i < AIL_strlen(str); i++)
      {

      c=str[i];
      if ((c>='a') && (c<='z'))
        c-=('a'-'A');

      if (c == '-')
         {
         sign = -sign;
         continue;
         }

      if (c==' ')
         {
         continue;
         }

      for (j=0; j < base; j++)
         {
         if (c == "0123456789ABCDEF"[j])
            {
            total = (total * base) + j;
            break;
            }
         }

      if (j == base)
         {
         break;
         }
      }

   if (end != NULL)
      {
      *end = &str[i];
      }

   return total * sign;
}

//############################################################################
//##                                                                        ##
//##  Read a line from a binary file                                        ##
//##                                                                        ##
//############################################################################

static U32 AIL_fgets(U32 file, U32* ibuf, char* buf, U32 size)
{
  U32 n=0;
    
  for(;;) {
    if (n>=size) 
      break;
    if (ibuf[1]==0) {
      ibuf[2]=(U32)&ibuf[3];
      ibuf[1]=AIL_fread(file,&ibuf[3],ibuf[0]-12);
      if (ibuf[1]==0) 
        break;
    }
    ibuf[1]--;
    if (*((char*)ibuf[2])!=13) {
      n++;
      if (*((char*)ibuf[2])==10) {
        ibuf[2]++;
        break;
      }
      *buf++=*((char*)ibuf[2]);
    }
    ibuf[2]++;
  }
  *buf=0;
  return(n);
}

//############################################################################
//##                                                                        ##
//##  Read .INI file                                                        ##
//##                                                                        ##
//##  Return 0 and update AIL_error[] if error occurred                     ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_read_INI(AIL_INI *INI, char*filename)
{
   S32 in;
   AIL_INI work;
   S8    buffer[80];
   U32    iobuf[67];  // three initial, 64 for buffering
   S32    i;
   char *value;
   char *parm,*end_parm;

   //
   // Init workspace
   //

   AIL_memset(&work   ,  0, sizeof(work));
   AIL_memset(&work.IO, -1, sizeof(work.IO));

   //
   // Open .INI file
   //

   in = AIL_lowfopen(filename,0);

   if (in == -1)
      {
      if (*AIL_redist_directory==0)
         {
         return 0;
         }
      else
         {
         char fn[256];
         AIL_strcpy(fn,AIL_redist_directory);
         AIL_strcat(fn,filename);

         in=AIL_lowfopen(fn,0);
         if (in == -1)
            {
            return 0;
            }
         }
      }
   iobuf[0]=sizeof(iobuf);
   iobuf[1]=0;
   while (AIL_fgets((U32) in, iobuf, (char *) buffer, sizeof(buffer)) != NULL)
      {

      //
      // Parse line
      //

      for (i = AIL_strlen(buffer)-1; i >= 0; i--)
         {
         //
         // Remove trailing whitespace
         //

         if (buffer[i]==' ')
            {
            buffer[i] = 0;
            }
         else
            {
            break;
            }
         }

      //
      // Find "parm" (1st word on line)
      //

      for (i=0; i < AIL_strlen(buffer); i++)
         {
         if (buffer[i]!=' ')
            {
            break;
            }
         }

      parm = (char *) &buffer[i];

      //
      // Find "value" (2nd word on line)
      //

      for (; i < AIL_strlen(buffer); i++)
         {
         if (buffer[i]==' ')
            {
            break;
            }
         }

      end_parm = (char *) &buffer[i];

      for (; i < AIL_strlen(buffer); i++)
         {
         if (buffer[i]!=' ')
            {
            break;
            }
         }

      value = (char *) &buffer[i];

      //
      // Reject unparsable lines
      //

      if (i >= AIL_strlen(buffer))
         {
         continue;
         }

      *end_parm = 0;

      //
      // Reject comments explicitly
      //

      if (parm[0] == ';')
         {
         continue;
         }

      //
      // Interpret line parameters 
      //

      if      (!AIL_strnicmp((char*)parm, "DRIVER",     sizeof("DRIVER")))
         {
         AIL_strcpy(work.driver_name, value);
         }
      else if (!AIL_strnicmp((char*)parm, "DEVICE",     sizeof("DEVICE")))
         {
         AIL_strcpy(work.device_name, value);
         }
      else if (!AIL_strnicmp((char*)parm, "IO_ADDR",    sizeof("IO_ADDR")))
         {
         work.IO.IO =         (S16) (ASC_val(value, 16, NULL) & 0xffff);
         }
      else if (!AIL_strnicmp((char*)parm, "IRQ",        sizeof("IRQ")))
         {
         work.IO.IRQ =        (S16) (ASC_val(value, 10, NULL) & 0xffff);
         }
      else if (!AIL_strnicmp((char*)parm, "DMA_8_bit",  sizeof("DMA_8_bit")))
         {
         work.IO.DMA_8_bit =  (S16) (ASC_val(value, 10, NULL) & 0xffff);
         }
      else if (!AIL_strnicmp((char*)parm, "DMA_16_bit", sizeof("DMA_16_bit")))
         {
         work.IO.DMA_16_bit = (S16) (ASC_val(value, 10, NULL) & 0xffff);
         }
      }

   AIL_lowfclose(in);

   //
   // Fail call if neither driver nor device valid
   //

   if ((!AIL_strlen(work.driver_name)) || (!AIL_strlen(work.device_name)))
      {
      AIL_set_error("Corrupted .INI file.");

      return 0;
      }

   *INI = work;

   return 1;
}

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

   AIL_preference[number] = value;

   return old;
}

//############################################################################
//##                                                                        ##
//## Register VDI driver with API, returning AIL_DRIVER descriptor          ##
//##                                                                        ##
//############################################################################

AIL_DRIVER * AILCALL AIL_API_install_driver(U8 const *driver_image, U32 n_bytes)
{
   AIL_DRIVER *drvr;
   U32       i;

#ifdef __HIGHC__
   U8      *buffer;
   FARPTR      DRVfar;
#endif

   //
   // Allocate AIL_DRIVER structure
   //

   drvr = (AIL_DRIVER *) AIL_mem_alloc_lock(sizeof(AIL_DRIVER));

   if (drvr==NULL)
      {
      AIL_set_error("Out of memory.");

      return NULL;
      }

   //
   // Allocate a buffer for the VDI driver in real-mode (lower 1MB) memory
   //

   drvr->size = n_bytes;

   if (!AIL_mem_alloc_DOS((n_bytes + 15) / 16, &drvr->buf, &drvr->seg, &drvr->sel))
      {
      AIL_set_error("Out of DOS memory.");

      AIL_mem_free_lock(drvr);

      return NULL;
      }

   //
   // Copy the driver image into low memory
   //

#ifdef __HIGHC__

   DRVfar.part.seg = 0x34;
   DRVfar.part.off = (U32) drvr->buf;
   
   for (i = n_bytes; i; i--)
      {
      ((_Far U8 *) DRVfar.ptr)[i-1] = driver_image[i-1];
      }

#else

   AIL_memcpy(drvr->buf,driver_image,n_bytes);

#endif

   //
   // Set up pointer to driver's VDI header
   //  

   drvr->VHDR = (VDI_HDR *) drvr->buf;

   //
   // Identify driver type
   //

   if (!AIL_strnicmp((char*)drvr->VHDR->ID,"AIL3DIG",7))
      {
      drvr->type = AIL3DIG;
      }
   else if (!AIL_strnicmp((char*)drvr->VHDR->ID,"AIL3MDI",7))
      {
      drvr->type = AIL3MDI;
      }
   else
      {
      AIL_set_error("Invalid driver type.");

      AIL_mem_free_DOS  (drvr->buf, drvr->seg, drvr->sel);
      AIL_mem_free_lock (drvr);

      return NULL;
      }

   //
   // Add driver to list
   //

   for (i=0; i < MAX_DRVRS; i++)
      {
      if (AIL_driver[i] == NULL)
         {
         AIL_driver[i] = drvr;

         drvr->VHDR->driver_num = (S16) i;

         break;
         }
      }

   if (i==MAX_DRVRS)
      {
      AIL_set_error("Out of driver handles.");

      AIL_mem_free_DOS  (drvr->buf, drvr->seg, drvr->sel);
      AIL_mem_free_lock (drvr);

      return NULL;
      }

   //
   // Initialize driver variables
   //

   drvr->VHDR->busy = 0;

   drvr->initialized = 0;

   drvr->PM_ISR = -1;

   //
   // Link driver into INT 66H chain after current ISR
   //

   drvr->VHDR->prev_ISR = AIL_get_real_vect(0x66);

   AIL_set_real_vect(0x66, drvr->seg + drvr->VHDR->this_ISR);

   //
   // Initialize destructor vector and high-level descriptor to NULL
   //

   drvr->destructor = NULL;
   drvr->descriptor = NULL;

   //
   // Call initialization function
   //

   AIL_call_driver(drvr, DRV_INIT, NULL, NULL);

   //
   // Allocate periodic service timer, if requested
   //

   if (drvr->VHDR->service_rate <= 0)
      {
      drvr->server = -1;
      }
   else
      {
      drvr->server = AIL_register_timer(AIL_driver_server);

      if (drvr->server == -1)
         {
         AIL_set_error("Out of timer handles.");

         AIL_set_real_vect(0x66, drvr->VHDR->prev_ISR);

         AIL_mem_free_DOS  (drvr->buf, drvr->seg, drvr->sel);
         AIL_mem_free_lock (drvr);

         return NULL;
         }

      AIL_set_timer_user(drvr->server,(U32) drvr);

      AIL_set_timer_frequency(drvr->server,drvr->VHDR->service_rate);
      AIL_start_timer(drvr->server);
      }

   //
   // Return AIL_DRIVER descriptor
   //

   return drvr;
}

//############################################################################
//##                                                                        ##
//## Shut down and unload driver from memory; free driver descriptor        ##
//##                                                                        ##
//## Note: Not all DPMI hosts reclaim low DOS memory properly when freed.   ##
//## This function never needs to be called by most applications.           ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_uninstall_driver(AIL_DRIVER *drvr)
{
   S32 i;

   //
   // Call high-level destructor to free any allocated resources
   //

   if (drvr->destructor != NULL)
      {
      drvr->destructor((HDIGDRIVER) drvr->descriptor);
      }

   //
   // Stop periodic timer service, if enabled
   // 

   if (drvr->server != -1)
      {
      AIL_release_timer_handle(drvr->server);
      }

   //
   // If device successfully initialized, shut it down
   // 

   if (drvr->initialized)
      {
      if (drvr->PM_ISR != -1)
         {
         AIL_restore_USE16_ISR(drvr->PM_ISR);
         }

      AIL_call_driver(drvr, DRV_SHUTDOWN_DEV, NULL, NULL);
      }

   //
   // Unlink driver from INT 66H chain
   //

   AIL_set_real_vect(0x66, drvr->VHDR->prev_ISR);

   //
   // Free low-memory buffer and AIL_DRIVER descriptor
   //

   AIL_mem_free_DOS(drvr->buf,drvr->seg,drvr->sel);
   AIL_mem_free_lock(drvr);

   for (i = 0; i < MAX_DRVRS; i++)
      {
      if (AIL_driver[i] == drvr)
         {
         AIL_driver[i] = NULL;
         }
      }
}

//############################################################################
//##                                                                        ##
//## Retrieve I/O settings for driver by means of embedded environment      ##
//## variable, if present                                                   ##
//##                                                                        ##
//############################################################################

IO_PARMS * AILCALL AIL_API_get_IO_environment(AIL_DRIVER *drvr)
{
   static IO_PARMS IO;
   char         *envname;
   char         *envval;
   S32 len;

   envname = (char *) REALPTR(drvr->VHDR->environment_string);

   if (envname == NULL)
      {
      return NULL;
      }

   envval = AIL_getenv(envname);

   if (envval == NULL)
      {
      return NULL;
      }

   len=AIL_strlen(envval);
   if (len>127)
     len=127;

   AIL_memcpy(drvr->VHDR->scratch,envval,len);

   drvr->VHDR->scratch[len]=0;

   if (AIL_call_driver(drvr, DRV_PARSE_ENV, NULL, NULL) == -1)
      {
      return NULL;
      }

#ifdef __HIGHC__

#else

   IO = drvr->VHDR->IO;

#endif

   return &IO;
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
   S32 i;

   //
   // If Borland Power Pack in use, we need to immediately set DS's limit
   // to 0xFFFFFFFF, in order to allow access to lower memory via 4GB near
   // pointer wraparound (a la FlashTek X32)
   //

   #ifdef __BORLANDC__

   AIL_sel_set_limit(_DS, 0xffffffff);

   #endif

   //
   // Initialize AIL
   //

   AIL_start();

   AIL_set_preference(DIG_SERVICE_RATE,         DEFAULT_DSR);
   AIL_set_preference(DIG_HARDWARE_SAMPLE_RATE, DEFAULT_DHSR);
   AIL_set_preference(DIG_DMA_RESERVE,          DEFAULT_DDR);
   AIL_set_preference(DIG_LATENCY,              DEFAULT_DL);
   AIL_set_preference(DIG_MIXER_CHANNELS,       DEFAULT_DMC);
   AIL_set_preference(DIG_RESAMPLING_TOLERANCE, DEFAULT_DRT);
   AIL_set_preference(DIG_USE_STEREO,           DEFAULT_DUS);
   AIL_set_preference(DIG_USE_16_BITS,          DEFAULT_DU16);
   AIL_set_preference(DIG_ALLOW_16_BIT_DMA,     DEFAULT_DA16DMA);
   AIL_set_preference(DIG_SS_LOCK,              DEFAULT_DSL);
   AIL_set_preference(MDI_SERVICE_RATE,         DEFAULT_MSR);
   AIL_set_preference(MDI_SEQUENCES,            DEFAULT_MS);
   AIL_set_preference(MDI_DEFAULT_VOLUME,       DEFAULT_MDV);
   AIL_set_preference(MDI_QUANT_ADVANCE,        DEFAULT_MQA);
   AIL_set_preference(MDI_ALLOW_LOOP_BRANCHING, DEFAULT_ALB);
   AIL_set_preference(MDI_DEFAULT_BEND_RANGE,   DEFAULT_MDBR);
   AIL_set_preference(DIG_ENABLE_RESAMPLE_FILTER, DEFAULT_DERF);
   AIL_set_preference(DIG_MAX_PREDELAY_MS,      DEFAULT_MPDMS);

   AIL_set_preference(AIL_SCAN_FOR_HARDWARE,    DEFAULT_ASH);
   AIL_set_preference(AIL_ALLOW_VDM_EXECUTION,  DEFAULT_AVE);
   AIL_set_preference(MDI_DOUBLE_NOTE_OFF,      DEFAULT_MDNO);

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

   for (i=0; i < MAX_DRVRS; i++)
      {
      AIL_driver[i] = NULL;
      }

   AIL_error[0] = 0;

   AILA_startup();

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

   //
   // Remove following line to disable MPEG support in DOS version
   //

   MPEG_register(TRUE, RIB_alloc_provider_handle(0));

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
}

//############################################################################
//##                                                                        ##
//## Shut down AIL API modules and resources, unloading all installed       ##
//## drivers from memory                                                    ##
//##                                                                        ##
//## No further AIL_...() calls other than AIL_startup() are permissible!   ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_shutdown(void)
{
   AIL_DRIVER *drvr;
   S32        i;

   //
   // Shut down and unload all registered drivers
   //

   for (i = MAX_DRVRS-1; i >= 0; i--)
      {
      if ((drvr = AIL_driver[i]) != NULL)
         {
         AIL_uninstall_driver(drvr);
         }
      }

   //
   // Release all application timers
   //

   AIL_release_all_timers();

   //
   // Shut down all ASI providers
   //

   HPROVIDER PROVIDER;

   HPROENUM next = HPROENUM_FIRST;

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
   // Remove following line to disable MPEG support
   //

   MPEG_register(FALSE,0);

   //
   // Shut down assembly API 
   //

   AILA_shutdown();
}

//############################################################################
//##                                                                        ##
//## Delay in 1/60ths of a second                                           ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_delay(S32 intervals)
{
  intervals= AIL_ms_count() +(intervals*16);  // 16 = 62.5 intervals per second
  while (AIL_ms_count()<intervals) ;          // do the delay
}


//############################################################################
//##                                                                        ##
//## Error text handling routines                                           ##
//##                                                                        ##
//############################################################################


void AILCALL AIL_API_set_error(char const FAR* error_msg)
{
  AIL_strcpy(AIL_error,error_msg);
}


char FAR* AILCALL AIL_API_last_error               (void)
{
   return (char FAR *) (AIL_error);
}

//############################################################################
//##                                                                        ##
//## End of locked code                                                     ##
//##                                                                        ##
//############################################################################

void AIL_end(void)
{
   if (locked)
      {
      AIL_vmm_unlock_range(AIL_start, AIL_end);

      UNLOCK (AIL_driver[0]      );
      UNLOCK (AIL_preference[0]  );
      UNLOCK (AIL_error[0]       );
      UNLOCK (AIL_last_IO_attempt);
      UNLOCK (entry              );
      UNLOCK (flags              );
      UNLOCK (MSS_INTERNAL       );

      locked = 0;
      }
}

