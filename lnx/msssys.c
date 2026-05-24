//############################################################################
//##                                                                        ##
//##  MSSSYS.C                                                              ##
//##                                                                        ##
//##  Windows MSS support routines                                          ##
//##                                                                        ##
//##  16-bit protected-mode source compatible with MSC 7.0                  ##
//##  32-bit protected-mode source compatible with MSC 9.0                  ##
//##                                                                        ##
//##  Version 1.00 of 15-Feb-95: Derived from DLLLOAD.C V1.12               ##
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
#include <stdio.h>
#include <stdarg.h>

#include "mss.h"
#include "imssapi.h"


void * AILCALLBACK lnx_alloc(U32 size)
{
   return malloc(size);
}

void AILCALLBACK lnx_free( void * ptr )
{
   return free(ptr);
}

static AILMEMALLOCCB mss_alloc = lnx_alloc;
static AILMEMFREECB mss_free = lnx_free;

DXDEC void * AILCALL AIL_mem_use_malloc(AILMEMALLOCCB fn)
{
  void * ret = mss_alloc;
  mss_alloc = (fn)?fn:lnx_alloc;
  return( ret );
}

DXDEC void * AILCALL AIL_mem_use_free  (AILMEMFREECB fn)
{
  void * ret = mss_free;
  mss_free = (fn)?fn:lnx_free;
  return( ret );
}

//############################################################################
//##                                                                        ##
//## Allocate and free page-locked global memory for AIL resources          ##
//##                                                                        ##
//## These routines should not be used for allocation of numerous small     ##
//## objects, due to limited LDT handle space                               ##
//##                                                                        ##
//## Memory allocated is owned by DLL, and is allocated with                ##
//## GMEM_SHARE and GMEM_ZEROINIT attributes (MOVEABLE attribute is         ##
//## disabled by GlobalPageLock())                                          ##
//##                                                                        ##
//############################################################################

void FAR * AILCALL AIL_API_mem_alloc_lock(U32 size)
{
   return( mss_alloc( size ) );
}

void AILCALL AIL_API_mem_free_lock(void FAR *ptr)
{
   if (ptr != NULL)
      mss_free( ptr );
}

//############################################################################
//##                                                                        ##
//##  Write file at *buf of length len                                      ##
//##                                                                        ##
//##  Overwrites any existing file                                          ##
//##                                                                        ##
//##  Returns 0 on error, else 1                                            ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_file_write(char const FAR *filename, void const FAR *buf, U32 len)
{
   U32 i;
   int handle;
   U16 readamt;

   disk_err = 0;

   handle = creat(filename, 0666);

   if (handle==-1)
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to create file.");
      return 0;
      }

   while (len)
      {
      readamt=(U16) ((len >= (32768-512)) ? (32768-512) : len);

      i = write(handle,buf,readamt);

      if (i == -1)
         {
         disk_err = AIL_CANT_WRITE_FILE;
         close(handle);
         return 0;
         }

      if (i != readamt)
         {
         disk_err = AIL_DISK_FULL;
         AIL_set_error("Unable to write to file (disk full?).");
         close(handle);
         return 0;
         }

      len -= readamt;
      buf += readamt;
      }

   close(handle);

   return 1;
}


//############################################################################
//##                                                                        ##
//##  Write wave file at *buf of length len                                 ##
//##                                                                        ##
//##  Overwrites any existing file                                          ##
//##                                                                        ##
//##  Returns 0 on error, else 1                                            ##
//##                                                                        ##
//############################################################################

S32 AILCALL AIL_API_WAV_file_write(char const FAR *filename, void const FAR *buf, U32 len, S32 rate, S32 format)
{
   WAVEOUT wo;

   AIL_memcpy(&wo.riffmark,"RIFF",4);
   wo.rifflen=len+sizeof(WAVEOUT)-8;
   AIL_memcpy(&wo.wavemark,"WAVE",4);
   AIL_memcpy(&wo.fmtmark,"fmt ",4);
   wo.fmtlen=16;
   wo.fmttag=WAVE_FORMAT_PCM;
   wo.channels=(S16)((format&DIG_F_STEREO_MASK)?2:1);
   wo.sampersec=rate;
   wo.bitspersam=(S16)((format&DIG_F_16BITS_MASK)?16:8);
   wo.blockalign=(S16)(((S32)wo.bitspersam*(S32)wo.channels) / 8);
   wo.avepersec=(rate *(S32)wo.bitspersam*(S32)wo.channels) / 8;
   AIL_memcpy(&wo.datamark,"data",4);
   wo.datalen=len;

   {

   U32 i;
   int handle;
   U16 readamt;

   disk_err = 0;

   handle = creat(filename, 0);

   if (handle==-1)
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to create file.");
      return 0;
      }

   write(handle,&wo,sizeof(wo));

   while (len)
      {
      readamt=(U16) ((len >= (32768-512)) ? (32768-512) : len);

      i = write(handle,buf,readamt);

      if (i == -1)
         {
         disk_err = AIL_CANT_WRITE_FILE;
         close(handle);
         return 0;
         }

      if (i != readamt)
         {
         disk_err = AIL_DISK_FULL;
         AIL_set_error("Unable to write to file (disk full?).");
         close(handle);
         return 0;
         }

      len -= readamt;
      buf = AIL_ptr_add(buf,readamt);
      }

   close(handle);

   return 1;
  }
}

S32  AIL_sprintf            (char FAR *dest,
                             char const FAR *fmt, ...)
{
  S32 len;

  va_list ap;

  va_start(ap,
           fmt);

  len=vsprintf(dest,
               fmt,
               ap);

  va_end  (ap);

  return( len );

}
