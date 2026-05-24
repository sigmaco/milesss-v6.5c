//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  MSSSR.C: Stack vs. register mapping file                              ##
//##                                                                        ##
//##  Flat-model source compatible with IBM 32-bit ANSI C/C++               ##
//##                                                                        ##
//##  Version 1.00 of 22-Jun-96: Initial                                    ##
//##                                                                        ##
//##  Author: Jeff Roberts                                                  ##
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dos.h>

static S32 locked = 0;

void AILSR_end(void);

void __pascal AILDEBUG_start(void);

static void AILSR_start(void)
{
   if (!locked)
      {
      AIL_vmm_lock_range(AILSR_start, AILSR_end);

      locked = 1;
      AILDEBUG_start();
      }
}

//############################################################################
//##                                                                        ##
//## Stack vs. Register mapping functions                                   ##
//##                                                                        ##
//############################################################################

static void* AILCALLBACK ail_malloc(U32 size)
{
  void* val=malloc(size);
  MarkEBXAsChanged();
  return(val);
}

static void AILCALLBACK ail_free(void* ptr)
{
  free(ptr);
  MarkEBXAsChanged();
}

static char* __pascal ail_getenv(char* vari)
{
  char* val=getenv(vari);
  MarkEBXAsChanged();
  return(val);
}

static void __pascal ail_int386(U32 intnum,void* inr, void* outr)
{
  int386(intnum,(union REGS*)inr,(union REGS*)outr);
  MarkEBXAsChanged();
}

static S32 _cdecl ail_sprintf (char FAR *dest, char const FAR *fmt, ...)
{
  S32 len;

  va_list ap;

  va_start(ap,
           fmt);
  MarkEBXAsChanged();

  len=vsprintf(dest,
               fmt,
               ap);
  MarkEBXAsChanged();

  va_end  (ap);
  MarkEBXAsChanged();

  return( len );
}

static S32 _cdecl ail_fprintf (U32 hand, char const FAR *fmt, ...)
{
  char buf[512];

  S32 len;

  va_list ap;

  va_start(ap,
           fmt);
  MarkEBXAsChanged();

  len=vsprintf(buf,
               fmt,
               ap);
  MarkEBXAsChanged();

  va_end  (ap);
  MarkEBXAsChanged();

  return( AIL_fwrite(hand,buf,len) );
}

/****************************************************************************/

extern U16 AIL_debug;
extern U16 AIL_sys_debug;
extern U32 AIL_debugfile;
extern U32 AIL_indent;
extern U32 AIL_starttime;
extern int AIL_didaninit;

extern void cdecl AIL_API_startup(void);

#ifdef __WATCOMC__
#pragma aux AIL_mem_alloc "_*";
#pragma aux AIL_mem_free "_*";
#pragma aux AIL_getenv "_*";
#pragma aux AIL_int386 "_*";
#pragma aux AIL_sprintf "_*";
#pragma aux AIL_fprintf "_*";
#pragma aux AIL_debug "_*";
#pragma aux AIL_sys_debug "_*";
#pragma aux AIL_indent "_*";
#pragma aux AIL_starttime "_*";
#pragma aux AIL_debugfile "_*";
#pragma aux AIL_didaninit "_*";
#endif

#ifdef __SW_3R
S32    cdecl  AIL_startup_reg               (void)
#else
S32    cdecl  AIL_startup_stack             (void)
#endif
{
   char*      filename;
   S32        elapstime;
   struct tm  *adjtime;
   static char loctime[32];
   char* asc;

   //
   // Bail out if already started
   //

   if (AIL_didaninit!=NO)
      {
      return(0);
      }

    AIL_didaninit=1;

   AIL_mem_alloc=ail_malloc;
   AIL_mem_free=ail_free;
   AIL_getenv=ail_getenv;
   AIL_int386=ail_int386;
   AIL_sprintf=ail_sprintf;
   AIL_fprintf=ail_fprintf;

   AILSR_start();

   //
   // Get environment variable for debug script, and enable debug mode
   // if script filename valid
   //

   AIL_debug     = 0;
   AIL_sys_debug = 0;

   filename = ail_getenv("MSS_DEBUG");

   if (filename == NULL)
      {
      AIL_API_startup();
      return(1);
      }

   if (ail_getenv("MSS_SYS_DEBUG") != NULL)
      {
      AIL_sys_debug = 1;
      }

   //
   // Open script file and set "debug" flag
   //

   AIL_debugfile = AIL_fappend(filename);

   if (AIL_debugfile == (U32)-1)
      {
      AIL_API_startup();
      return(1);
      }

   //
   // Write header to script file
   //

   time((void *) &elapstime);
   MarkEBXAsChanged();
   adjtime = localtime((void *) &elapstime);
   MarkEBXAsChanged();
   asc=asctime(adjtime);
   MarkEBXAsChanged();
   AIL_strcpy(loctime,asc);
   loctime[24] = 0;

   AIL_fprintf(AIL_debugfile,
               "-------------------------------------------------------------------------------\r\n"
               "Miles Sound System usage script generated by MSS V"MSS_VERSION"\r\n"
               "Start time: %s"
               "\r\n-------------------------------------------------------------------------------\r\n\r\n",
               loctime);
   //
   // Initialize API
   //

   AIL_API_startup();

   AIL_starttime=AIL_ms_count();
   AIL_debug  = 1;

   AIL_indent = 1;
   AIL_time_write();
   AIL_indent = 0;

   AIL_fprintf(AIL_debugfile,"AIL_startup()\r\n");

   return(1);
}

//############################################################################
//##                                                                        ##
//## End of locked code                                                     ##
//##                                                                        ##
//############################################################################

void AILSR_end(void)
{
   if (locked)
      {
      AIL_vmm_unlock_range(AILSR_start, AILSR_end);

      locked = 0;
      }
}
