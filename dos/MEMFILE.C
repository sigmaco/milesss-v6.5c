//############################################################################
//##                                                                        ##
//##  MEMFILE.C                                                             ##
//##                                                                        ##
//##  32-bit DLL driver loader, DOS file routines, and memory primitives    ##
//##                                                                        ##
//##  V1.00 of 16-Aug-92: Initial version for Watcom C                      ##
//##  V1.01 of  1-May-93: Zortech C++ v3.1 compatibility added              ##
//##  V1.02 of 16-Nov-93: Metaware High C/C++ v3.1 compatibility added      ##
//##  V1.03 of 30-Dec-93: Para-align all but first object in file           ##
//##  V1.10 of  2-Jun-94: Added MEM_alloc() / MEM_free() pointers           ##
//##                      Added MEM_alloc_DOS() / MEM_free_DOS () functions ##
//##  V1.11 of  7-Sep-94: Added VMM_lock() / VMM_unlock() functions         ##
//##  V1.12 of 21-Oct-94: Implemented Watcom/Flashtek support               ##
//##  V1.13 of  9-Jun-95: Borland/Zortech support updated                   ##
//##  V1.14 of 28-Jun-95: Use correct function code for FlashTek page-lock  ##
//##  V1.15 of 24-Jun-96: Switch to rad file i/o routines                   ##
//##  V1.16 of  7-Jul-96: Yanked the DLL stuff, renamed to MEMFILE.C        ##
//##                                                                        ##
//##  Project: 386FX Sound & Light(TM)                                      ##
//##  Authors: John Lemberger, John Miles, Jeff Roberts                     ##
//##                                                                        ##
//##  C source compatible with Watcom C386 v9.0 or later                    ##
//##                           Zortech C++ v3.1 or later                    ##
//##                           MetaWare High C/C++ v3.1 or later            ##
//##                                                                        ##
//############################################################################
//##                                                                        ##
//##  Copyright (C) RAD Game Tools, Inc.                                    ##
//##                                                                        ##
//##  Contact RAD Game Tools at 425-893-4300 for technical support.         ##
//##                                                                        ##
//############################################################################

#include <dos.h>
#include <stdlib.h>

#include "mss.h"
#include "imssapi.h"


//
// Global pointers to malloc() / free() methods
//

void* AILCALLBACK (*AIL_mem_alloc) (U32) = 0;
void  AILCALLBACK (*AIL_mem_free)  (void *) = 0;

char* __pascal (*AIL_getenv) (char* vari)=0;
void __pascal (*AIL_int386) (U32 intnum,void* inr, void* outr)=0;
S32 __cdecl (*AIL_sprintf) (char FAR *dest, char const FAR *fmt, ...)=0;
S32 __cdecl (*AIL_fprintf) (U32 hand, char const FAR *fmt, ...)=0;


//############################################################################
//##                                                                        ##
//## Declare malloc() / free() handlers to be used by MSS                   ##
//## By default, the compiler's standard C library routines are used        ##
//##                                                                        ##
//## Both functions return the former malloc() / free() handlers            ##
//##                                                                        ##
//############################################################################

void * cdecl AIL_mem_use_malloc(void * AILCALLBACK (*fn)(U32))
{
   void *old;

   old = (void *) AIL_mem_alloc;

   AIL_mem_alloc = fn;

   return (void *) old;
}

void * cdecl AIL_mem_use_free(void AILCALLBACK (*fn)(void *))
{
   void *old;

   old = (void *) AIL_mem_free;

   AIL_mem_free = fn;

   return old;
}

static void* freelist=0;

static void addtobackfree(void* ptr)
{
  *((void**)ptr)=freelist;
  freelist=ptr;
}

//only call when you know we're in foreground
static void checkforbackfrees(void)
{
  void* fl=freelist;
  freelist=0;

  while (fl)
  {
    void* next=*((void**)fl);
    AIL_API_mem_free_lock(fl);
    fl=next;
  }
}

//############################################################################
//##                                                                        ##
//## Allocate and free memory via current malloc() / free() alias handler,  ##
//## applying VMM locking to allocated memory and releasing VMM locking on  ##
//## freed memory                                                           ##
//##                                                                        ##
//############################################################################

void * cdecl AIL_API_mem_alloc_lock(U32 size)
{
   void *ptr;

   // fail all background mallocs
   if (AIL_background())
     return(0);

   checkforbackfrees();

   size+=4;  // increase the pointer size to hide the size value

   if (size<8)
     size=8;

   ptr = AIL_mem_alloc(size);

   if (ptr == NULL)
     return(0);

   AIL_vmm_lock(ptr,size);

   *((U32*)ptr)=size;   // hide the size value

   return( ((U32*)ptr)+1 );

}

void cdecl AIL_API_mem_free_lock(void *ptr)
{

   if (ptr != NULL)
   {

      if (AIL_background())
        addtobackfree(ptr);
      else
      {
        checkforbackfrees();

        AIL_vmm_unlock(ptr,((U32*)ptr)[-1]);

        AIL_mem_free( ((U32*)ptr)-1 );
      }
   }
}

//############################################################################
//##                                                                        ##
//## Allocate memory from first megabyte of physical RAM                    ##
//##                                                                        ##
//## This memory is automatically locked to inhibit VMM swapping            ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_mem_alloc_DOS(U32  n_paras,     //)
                         void **protected_ptr,
                         U32 *segment_far_ptr,
                         U32 *selector)
{
   union REGS inregs,outregs;
   S32       success;
   void      *p1,*p2;

#ifdef DPMI          // Rational Systems DOS/4GW or Borland Power Pack

   inregs.x.eax = 0x100;
   inregs.x.ebx = n_paras;

   AIL_int386(0x31, &inregs, &outregs);

   if (outregs.x.cflag)
      {
      success = 0;
      }
   else
      {
      *segment_far_ptr =  outregs.x.eax          << 16;
      *selector        =  outregs.x.edx & 0xffff;

      //
      // Rational uses linear pointers directly, but Borland needs a fixup
      //

      #ifdef __BORLANDC__

         {
         #ifndef REALPTR
         #define REALPTR(x) ((void *) (U32) ((((U32) (x))>>16<<4) + ((x) & 0xffff) \
                            - AIL_sel_base(_DS)))
         #endif

         *protected_ptr = REALPTR(*segment_far_ptr);
         }

      #else

         *protected_ptr = (U8 *) ((outregs.x.eax & 0xffff) * 16);

      #endif

      success = 1;
      }

#else
#ifdef INT21         // Flashtek X32 / Phar Lap 386

#ifdef __ZTC__                      // Zortech C++
   inregs.e.eax = 0x4800;
   inregs.e.ebx = n_paras;

   int86(0x21, &inregs, &outregs);

   if (outregs.e.cflag)
      {
      success = 0;
      }
   else
      {
      *segment_far_ptr =          outregs.e.eax << 16;
      *protected_ptr   = (U8 *) outregs.e.ebx;
      *selector        =          0;

      success = 1;
      }

#else
#ifdef __HIGHC__                    // MetaWare C++
   inregs.w.eax = 0x25c0;
   inregs.w.ebx = n_paras;

   int86(0x21, &inregs, &outregs);

   if (outregs.w.cflag)
      {
      success = 0;
      }
   else
      {
      *segment_far_ptr =            outregs.w.eax          << 16;
      *protected_ptr   = (U8 *) ((outregs.w.eax & 0xffff) * 16);
      *selector        =            0;

      success = 1;
      }

#else                               // Watcom C++
   inregs.x.eax = 0x4800;
   inregs.x.ebx = n_paras;

   AIL_int386(0x21, &inregs, &outregs);

   if (outregs.x.cflag)
      {
      success = 0;
      }
   else
      {
      *segment_far_ptr =          outregs.x.eax << 16;
      *protected_ptr   = (U8 *) outregs.x.ebx;
      *selector        =          0;

      success = 1;
      }

#endif
#endif
#endif
#endif

   if (success)
      {
      p1 = (void *) (U32) ((*segment_far_ptr) >> 12);
      p2 = (void *) (U32) (((U32) p1) + (16 * n_paras) - 1);

      AIL_vmm_lock_range(p1,p2);
      }

   return success;
}

//############################################################################
//##                                                                        ##
//## Free memory allocated from first megabyte of physical RAM              ##
//##                                                                        ##
//############################################################################

void cdecl AIL_mem_free_DOS(void *protected_ptr,    //)
                        U32 segment_far_ptr,
                        U32 selector)
{
#ifdef DPMI          // Rational Systems DOS/4GW or Borland Power Pack
   union REGS inregs,outregs;

   inregs.x.eax = 0x101;
   inregs.x.edx = selector & 0xffff;

   AIL_int386(0x31, &inregs, &outregs);

   if (protected_ptr) protected_ptr = NULL;
   if (segment_far_ptr) segment_far_ptr = 0;

#else
#ifdef INT21         // Flashtek X32 / Phar Lap 386

#ifdef __ZTC__                      // Zortech C++

   //
   // Real-mode memory is never freed by X32
   //

#else
#ifdef __HIGHC__                    // MetaWare C++

#else                               // Watcom C++

   protected_ptr   = NULL;
   segment_far_ptr = 0;
   selector        = 0;

   //
   // Real-mode memory is never freed by X32
   //

#endif
#endif
#endif
#endif
}

//############################################################################
//##                                                                        ##
//## Lock linear memory region between *p1 and *p2, preventing              ##
//## virtual-memory systems from swapping it to disk                        ##
//##                                                                        ##
//## p1 and p2 are linear addresses passed as near 32-bit pointers, and may ##
//## be specified in either order                                           ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_vmm_lock_range(void *p1, void *p2)
{
   S32 success;

#ifdef DPMI          // Rational Systems DOS/4GW or Borland Power Pack

   union REGS inregs,outregs;
   U32      linear;
   U32      len;

   AIL_memset(&inregs,0,sizeof(inregs));

   linear =  min(((U32) p1), ((U32) p2));
   len    = (max(((U32) p1), ((U32) p2)) - linear) + 1;

   inregs.x.eax = 0x600;
   inregs.x.ebx = linear >> 16;
   inregs.x.ecx = linear &  0xffff;
   inregs.x.esi = len    >> 16;
   inregs.x.edi = len    &  0xffff;

   AIL_int386(0x31, &inregs, &outregs);

   success = outregs.x.cflag ? 0 : 1;
   
#else
#ifdef INT21         // Flashtek X32 / Phar Lap 386

#ifdef __ZTC__                      // Zortech C++

  success = 0;

#else
#ifdef __HIGHC__                    // MetaWare C++

  success = 0;

#else                               // Watcom C++

   union REGS   inregs,outregs;
   struct SREGS segregs;
   U32        linear;
   U32        len;

   linear =  min(((U32) p1), ((U32) p2));
   len    = (max(((U32) p1), ((U32) p2)) - linear) + 1;

   segread(&segregs);

   segregs.es = segregs.ds;

   inregs.x.eax = 0x252b;
   inregs.x.ebx = 0x501;
   inregs.x.ecx = linear;
   inregs.x.edx = len;

   AIL_int386x(0x21, &inregs, &outregs, &segregs);

   success = outregs.x.cflag ? 0 : 1;

#endif
#endif
#endif
#endif

   return success;
}

//############################################################################
//##                                                                        ##
//## Unlock linear memory region between *p1 and *p2, allowing              ##
//## virtual-memory systems to swap it to disk                              ##
//##                                                                        ##
//## p1 and p2 are linear addresses passed as near 32-bit pointers, and may ##
//## be specified in either order                                           ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_vmm_unlock_range(void *p1, void *p2)
{
   S32 success;

#ifdef DPMI          // Rational Systems DOS/4GW or Borland Power Pack

   union REGS inregs,outregs;
   U32      linear;
   U32      len;

   linear =  min(((U32) p1), ((U32) p2));
   len    = (max(((U32) p1), ((U32) p2)) - linear) + 1;

   AIL_memset(&inregs,0,sizeof(inregs));

   inregs.x.eax = 0x601;
   inregs.x.ebx = linear >> 16;
   inregs.x.ecx = linear &  0xffff;
   inregs.x.esi = len    >> 16;
   inregs.x.edi = len    &  0xffff;

   AIL_int386(0x31, &inregs, &outregs);

   success = outregs.x.cflag ? 0 : 1;
   
#else
#ifdef INT21         // Flashtek X32 / Phar Lap 386

#ifdef __ZTC__                      // Zortech C++

  success=0;

#else
#ifdef __HIGHC__                    // MetaWare C++

  success=0;

#else                               // Watcom C++

   union REGS   inregs,outregs;
   struct SREGS segregs;
   U32        linear;
   U32        len;

   linear =  min(((U32) p1), ((U32) p2));
   len    = (max(((U32) p1), ((U32) p2)) - linear) + 1;

   segread(&segregs);

   segregs.es = segregs.ds;

   inregs.x.eax = 0x252b;
   inregs.x.ebx = 0x61;
   inregs.x.ecx = linear;
   inregs.x.edx = len;

   AIL_int386x(0x21, &inregs, &outregs, &segregs);

   success = outregs.x.cflag ? 0 : 1;

#endif
#endif
#endif
#endif

   return success;
}

//############################################################################
//##                                                                        ##
//## Lock linear memory region at *start, preventing virtual-memory systems ##
//## from swapping it to disk                                               ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_vmm_lock(void *start, U32 size)
{
   return AIL_vmm_lock_range(start,(U8 *) start + size-1 );
}

//############################################################################
//##                                                                        ##
//## Unlock linear memory region at *start, allowing virtual-memory systems ##
//## to swap it to disk                                                     ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_vmm_unlock(void *start, U32 size)
{
   return AIL_vmm_unlock_range(start,(U8 *) start + size-1 );
}

#ifdef __BORLANDC__

//############################################################################
//##                                                                        ##
//##  Return base address of selector (For Borland support; DPMI only)      ##
//##                                                                        ##
//############################################################################

U32  cdecl AIL_sel_base         (U32  sel)
{
   union REGS inregs,outregs;

   inregs.x.eax = 6;
   inregs.x.ebx = sel;

   AIL_int386(0x31, &inregs, &outregs);

   return (outregs.x.ecx << 16) | (outregs.x.edx & 0xffff);
}

//############################################################################
//##                                                                        ##
//##  Set new limit for selector (For Borland support; DPMI only)           ##
//##                                                                        ##
//############################################################################

void   cdecl AIL_sel_set_limit    (U32  sel,                                 //)
                               U32  limit)
{
   union REGS inregs,outregs;

   inregs.x.eax = 8;
   inregs.x.ebx = sel;
   inregs.x.ecx = limit >> 16;
   inregs.x.edx = limit & 0xffff;

   AIL_int386(0x31, &inregs, &outregs);
}

#endif


//############################################################################
//##                                                                        ##
//##  Write file at *buf of length len                                      ##
//##                                                                        ##
//##  Overwrites any existing file                                          ##
//##                                                                        ##
//##  Returns 0 on error, else 1                                            ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_API_file_write(char const *filename, void const *buf, U32 len)
{
   S32 i;
   S32 handle;

   disk_err = 0;

   handle = AIL_fcreate(filename);
   if ((handle==NULL) || (handle==-1))
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to create file.");
      return 0;
      }

   i = AIL_fwrite(handle,buf,len);

   if (i !=len )
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to write to file (disk full?).");
      return 0;
      }

   AIL_lowfclose(handle);

   return 1;
}

//############################################################################
//##                                                                        ##
//##  Write WAV file at *buf of length len                                  ##
//##                                                                        ##
//##  Overwrites any existing file                                          ##
//##                                                                        ##
//##  Returns 0 on error, else 1                                            ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_API_WAV_file_write(char const *filename, void const *buf, U32 len, S32 rate, S32 format)
{
   WAVEOUT wo;

   S32 i;
   S32 handle;

   disk_err = 0;

   handle = AIL_fcreate(filename);
   if ((handle==NULL) || (handle==-1))
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to create file.");
      return 0;
      }

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
   
   AIL_fwrite(handle,&wo,sizeof(wo));

   i = AIL_fwrite(handle,buf,len);

   if (i !=len )
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to write to file (disk full?).");
      return 0;
      }

   AIL_lowfclose(handle);

   return 1;
}

//############################################################################
//##                                                                        ##
//##  Append to file at *buf of length len                                  ##
//##                                                                        ##
//##  Returns 0 on error, else 1                                            ##
//##                                                                        ##
//############################################################################

S32 cdecl AIL_file_append(char const *filename, void const *buf, U32 len)
{
   S32 i;
   S32 handle;

   disk_err = 0;

   handle = AIL_fappend(filename);
   if ((handle==NULL) || (handle==-1))
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to append to file.");
      return 0;
      }

   i = AIL_fwrite(handle,buf,len);

   if (i !=len )
      {
      disk_err = AIL_CANT_WRITE_FILE;
      AIL_set_error("Unable to write to file (disk full?).");
      return 0;
      }

   AIL_lowfclose(handle);

   return 1;
}

DXDEC
void AIL_MSS_version( char* dest, U32 len )
{
  char str[]=MSS_VERSION;
  if ((dest==0) || (len==0))
    return;

  len=(len>=4)?4:(len-1);
  AIL_memcpy(dest,str,len);
  dest[len]=0;
}