//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  MSSCD.C: Red Book CD-audio support for MSS                            ##
//##                                                                        ##
//##  32-bit protected-mode source                                          ##
//##                                                                        ##
//##  Version 1.00 of 15-Jun-96: Originally written.                        ##
//##  Version 1.01 of 11-May-97: AIL_redbook_open_drive (Serge Plagnol)     ##
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

#ifdef __WATCOMC__
  #include <i86.h>
#else
  #include <dos.h>
#endif

static void* ptrtoreal;
static U32 segtoreal;
static U32 seltoreal;
static U32 numopen=0;

#ifdef DPMI

#pragma pack(push,1)

//############################################################################
//##                                                                        ##
//## Low-level red book structures                                          ##
//##                                                                        ##
//############################################################################

typedef struct _SIMINT {
 U32 edi;
 U32 esi;
 U32 ebp;
 U32 rsvd;
 U32 ebx;
 U32 edx;
 U32 ecx;
 U32 eax;
 U16 flags;
 U16 es;
 U16 ds;
 U16 fs;
 U16 gs;
 U16 ip;
 U16 cs;
 U16 sp;
 U16 ss;
} SIMINT;

typedef struct _REQHDR {
  U8 length;
  U8 subunit;
  U8 command;
  U16 status;
  U8 reserved[8];
} REQHDR;

typedef struct _IOCTL {
  U8 mediadesc;
  U32 address;
  U16 bytes;
  U16 sector;
  U32 volid;
} IOCTL;

typedef struct _DISKINFO {
  U8 command;
  U8 first;
  U8 last;
  U32 leadout;
} DISKINFO;

typedef struct _TRACKINFO {
  U8 command;
  U8 track;
  U32 start;
  U8 info;
} TRACKINFO;

typedef struct _PLAYREQ {
  U8 addr;
  U32 start;
  U32 length;
} PLAYREQ;

typedef struct _DEVSTAT {
  U8 command;
  U32 status;
} DEVSTAT;

typedef struct _QINFO {
  U8 command;
  U8 control;
  U8 track;
  U8 pointindex;
  U8 tmin;
  U8 tsec;
  U8 tframe;
  U8 zer0;
  U8 dmin;
  U8 dsec;
  U8 dframe;
} QINFO;

typedef struct _WRITE {
  U8 command;
} WRITE;

#pragma pack(pop)


//############################################################################
//##                                                                        ##
//## Simulates a real mode interrupt                                        ##
//##                                                                        ##
//############################################################################

static void simint(U32 intr,SIMINT* sis)
{
  union REGS inr,outr;

  inr.x.eax=0x300;
  inr.x.ebx=intr;
  inr.x.ecx=0;
  inr.x.edi=(U32)sis;

  sis->ss=0;
  sis->sp=0;

  AIL_int386(0x31,&inr,&outr);
}

#endif



//############################################################################
//##                                                                        ##
//## low-level function to call the mscdex driver                           ##
//##                                                                        ##
//############################################################################

static U32 calldrv(U32 which,REQHDR* rh,U32 rhseg,U32 rhofs)  // call the cd driver through mscdex
{
  SIMINT sim;
  AIL_memset(&sim,0,sizeof(SIMINT));

  sim.eax=0x1510;
  sim.ecx=which;
  sim.es=(U16)(rhseg>>16);
  sim.ebx=rhofs;
  rh->status=0xffff;
  rh->length=sizeof(REQHDR);
  rh->subunit=0;
  simint(0x2f,&sim);
  return((rh->status&0x8000)?0:1);
}

//############################################################################
//##                                                                        ##
//## low-level function to do an mscdex ioctl call                          ##
//##                                                                        ##
//############################################################################

static U32 mscdexread(U32 which,REQHDR* rh,U32 rhseg, U32 rhofs)  // do a mscdex read request
{
  IOCTL* io=(IOCTL*)(rh+1);
  rh->command=3;
  io->mediadesc=0;
  io->sector=0;
  io->volid=0;
  return( calldrv(which,rh,rhseg,rhofs) );
}



//############################################################################
//##                                                                        ##
//## low-level function to convert red book to milliseconds                 ##
//##                                                                        ##
//############################################################################

static U32 RBtoMS(U32 val) 
{
  return( (((val>>8)&255)*1000)+(((val>>16)&255)*60000)+(((val&255)*1000)/75) );
}

//############################################################################
//##                                                                        ##
//## low-level function to convert separate red book values to milliseconds ##
//##                                                                        ##
//############################################################################

static U32 RBseptoMS(U32 min,U32 sec,U32 fr) 
{
  return( ((sec&255)*1000)+((min&255)*60000)+((fr*1000)/75) );
}

//############################################################################
//##                                                                        ##
//## low-level function to convert milliseconds to cd sectors               ##
//##                                                                        ##
//############################################################################

static U32 MStoSector(U32 val) 
{
  return( ((val/60000)*60*75)+(((val/1000)%60)*75)+(((val%1000)*75)/1000) -150 );
}

//############################################################################
//##                                                                        ##
//## low-level function to convert BCD into binary                          ##
//##                                                                        ##
//############################################################################

static U8 BCDtoBin(U8 val) 
{
  return( ((val/16)*10)+(val&15) );
}


//############################################################################
//##                                                                        ##
//## low-level function to read the audio cd's contents                     ##
//##                                                                        ##
//############################################################################

static U32 rbcontents(U32 which, REDBOOKTRACKINFO* info)
{
  int i;
  REQHDR* rh=(REQHDR*)ptrtoreal;
  IOCTL* io=(IOCTL*)(rh+1);
  DISKINFO* di=(DISKINFO*)(io+1);
  TRACKINFO* ti=(TRACKINFO*)(di+1);

  io->bytes=sizeof(DISKINFO);
  io->address=segtoreal+(((U8*)di)-((U8*)rh));
  di->command=10;

  if (mscdexread(which,rh,segtoreal,0)) {
    io->bytes=sizeof(TRACKINFO);
    io->address=segtoreal+(((U8*)ti)-((U8*)rh));
    ti->command=11;
    for(i=di->first;i<=di->last;i++) {
      ti->track=i;
      if (!mscdexread(which,rh,segtoreal,0))
        goto error;
      info->trackstarts[i-di->first]=RBtoMS(ti->start)+1;
    }
    info->tracks=di->last-di->first+1;
    info->trackstarts[info->tracks]=RBtoMS(di->leadout);
    return(1);
  }
 error:
  return(0);
}

//############################################################################
//##                                                                        ##
//## low-level function to open the red book device                         ##
//##                                                                        ##
//############################################################################

static U32 rbopen(U32 which)
{
  SIMINT sim;
  AIL_memset(&sim,0,sizeof(SIMINT));

  sim.eax=0x1500;
  sim.ebx=0;
  simint(0x2f,&sim);
  if (which>=sim.ebx)
    return(255);

  if (numopen==0) {
    AIL_mem_alloc_DOS(8,&ptrtoreal,&segtoreal,&seltoreal);
    if (ptrtoreal==0)
      return(255);
  }
  
  sim.eax=0x150d;
  sim.ebx=0;
  sim.es=(U16)(segtoreal>>16);
  simint(0x2f,&sim);
  
  numopen++;
  return(*(((U8*)ptrtoreal)+which));
}

//############################################################################
//##                                                                        ##
//## low-level functions to open the red book device using the drive letter ##
//##                                                                        ##
//############################################################################

static U32 rbopendrive(U32 drive)
{
  SIMINT sim;

  if(drive>='a')
  {
    drive -= 'a';
  }
  else
  {
    drive -= 'A';
  }

  if(drive>25)
  {
    return(255);
  }

  AIL_memset(&sim,0,sizeof(SIMINT));

  sim.eax=0x150B;
  sim.ebx=0;
  sim.ecx=drive;
  simint(0x2f,&sim);
  if(((sim.ebx&0xFFFF)!=0xADAD)||!(sim.eax&0xFFFF))
    return(255);

  if (numopen==0) {
    AIL_mem_alloc_DOS(8,&ptrtoreal,&segtoreal,&seltoreal);
    if (ptrtoreal==0)
      return(255);
  }

  numopen++;
  return(drive) ;
}

//############################################################################
//##                                                                        ##
//## low-level function to close the red book device                        ##
//##                                                                        ##
//############################################################################

static void rbclose(void)
{
  if (numopen)
    if (--numopen==0) 
      AIL_mem_free_DOS(ptrtoreal,segtoreal,seltoreal);
}
 
//############################################################################
//##                                                                        ##
//## low-level function to IOCTL output                                     ##
//##                                                                        ##
//############################################################################

static U32 rbwrite(U32 which,S32 cmd)  // 1=eject, 2=reset, 5=retract
{
  REQHDR* rh=(REQHDR*)ptrtoreal;
  IOCTL* io=(IOCTL*)(rh+1);
  WRITE* e=(WRITE*)(io+1);

  io->bytes=sizeof(WRITE);
  io->address=segtoreal+(((U8*)e)-((U8*)rh));
  e->command=cmd;
  
  rh->command=12;
  io->mediadesc=0;
  io->sector=0;
  io->volid=0;

  return( calldrv(which,rh,segtoreal,0) );
}

//############################################################################
//##                                                                        ##
//## low-level function to retrieve the position of the cd playback         ##
//##                                                                        ##
//############################################################################

static U32 rbpos(U32 which,S32 msortrack)
{
  REQHDR* rh=(REQHDR*)ptrtoreal;
  IOCTL* io=(IOCTL*)(rh+1);
  QINFO* qi=(QINFO*)(io+1);
  
  io->bytes=sizeof(QINFO);
  io->address=segtoreal+(((U8*)qi)-((U8*)rh));
  qi->command=12;

  if (mscdexread(which,rh,segtoreal,0)) 
    if (msortrack==0) 
      return( RBseptoMS(qi->dmin,qi->dsec,qi->dframe) );
    else
      return( BCDtoBin(qi->track) );

  return(0);
}

//############################################################################
//##                                                                        ##
//## low-level function to begin audio cd playback                          ##
//##                                                                        ##
//############################################################################

static void rbplay(U32 which, U32 start, U32 leng)
{
  REQHDR* rh=(REQHDR*)ptrtoreal;
  PLAYREQ* pr=(PLAYREQ*)(rh+1);

  rh->command=132;
  pr->addr=0;
  pr->start=MStoSector(start);
  pr->length=MStoSector(leng);
  calldrv(which,rh,segtoreal,0);
}

//############################################################################
//##                                                                        ##
//## low-level function to stop audio cd playback                           ##
//##                                                                        ##
//############################################################################

static void rbstop(U32 which)
{
  REQHDR* rh=(REQHDR*)ptrtoreal;

  rh->command=133;
  calldrv(which,rh,segtoreal,0);
}

//############################################################################
//##                                                                        ##
//## low-level function to read the audio cd's current status               ##
//##                                                                        ##
//############################################################################

static U32 rbstatus(U32 which)  // 1-playing, 2-stopped, 0=error
{
  REQHDR* rh=(REQHDR*)ptrtoreal;
  IOCTL* io=(IOCTL*)(rh+1);
  DEVSTAT* ds=(DEVSTAT*)(io+1);

  io->bytes=sizeof(DEVSTAT);
  io->address=segtoreal+(((U8*)ds)-((U8*)rh));
  ds->command=6;

  mscdexread(which,rh,segtoreal,0);
  
  if ((rh->status&0x8000) || (ds->status&1))
    return(0);
  else
    return( (rh->status&512)?1:2 );
}

//############################################################################
//##                                                                        ##
//## low-level function to set the cd volume (by BullFrog)                  ##
//##                                                                        ##
//############################################################################

void rbsetvol(U32 which, U8 volume)
{
	REQHDR* rh=(REQHDR*)ptrtoreal;
	IOCTL* io=(IOCTL*)(rh+1);
	U8* control_prot=(U8*)(io+1);

	rh->length		= sizeof(REQHDR) + sizeof(IOCTL);
	rh->subunit		= 0;
	rh->command		= 12;
	rh->status		= 0;
	io->mediadesc	= 0;
	io->sector		= 0;
	io->volid		= 0;
	control_prot[0] = 3;
	control_prot[1] = 0;
	control_prot[2] = volume*2;
	control_prot[3] = 1;
	control_prot[4] = volume*2;
	control_prot[5] = 2;
	control_prot[6] = 0;
	control_prot[7] = 3;
	control_prot[8] = 0;


	io->bytes		= 9;
	io->address		= segtoreal+(((U8*)control_prot)-((U8*)rh));
	calldrv(which,rh,segtoreal,0);
}

//############################################################################
//##                                                                        ##
//## low-level function to get the curren cd volume (by BullFrog)           ##
//##                                                                        ##
//############################################################################

U32 rbgetvol(U32 which)
{
	U32 volume;
	REQHDR* rh=(REQHDR*)ptrtoreal;
	IOCTL* io=(IOCTL*)(rh+1);
	U8* control_prot=(U8*)(io+1);

	rh->length		= sizeof(REQHDR) + sizeof(IOCTL);
	rh->subunit		= 0;
	rh->command		= 3;
	rh->status		= 0;
	io->mediadesc	= 0;
	io->sector		= 0;
	io->volid		= 0;
	control_prot[0] = 4;
	control_prot[1] = 0;
	control_prot[2] = 0;
	control_prot[3] = 1;
	control_prot[4] = 0;
	control_prot[5] = 2;
	control_prot[6] = 0;
	control_prot[7] = 3;
	control_prot[8] = 0;

	io->bytes		= 9;
	io->address		= segtoreal+(((U8*)control_prot)-((U8*)rh));
	calldrv(which,rh,segtoreal,0);
	volume = (control_prot[2] + control_prot[4])/2;

	return (volume >> 1);
}


//############################################################################
//##                                                                        ##
//## Complete redbook_open(_drive)                                          ##
//##                                                                        ##
//############################################################################

static HREDBOOK rbcompleteopen(U32 which)
{
  HREDBOOK r;

  if (which==255) {
    return(0);
  }

  r=(HREDBOOK)AIL_mem_alloc_lock(sizeof(REDBOOK));
  if (r==0)
    return(0);

  AIL_memset(r,0,sizeof(REDBOOK));

  r->readcontents=1;
  r->paused=0;
  r->DeviceID=which;

  return(r);
}

//############################################################################
//##                                                                        ##
//## Open a handle to a Red Book Device.                                    ##
//##                                                                        ##
//##    which is the number of the CD-ROM to use (0=auto).                  ##
//##                                                                        ##
//############################################################################

HREDBOOK cdecl AIL_API_redbook_open(U32 which)
{
  return(rbcompleteopen(rbopen(which)));
}


//############################################################################
//##                                                                        ##
//## Open a handle to a Red Book Device.                                    ##
//##                                                                        ##
//##    drive is the CD-ROM drive letter				    ##
//##                                                                        ##
//############################################################################

HREDBOOK cdecl AIL_API_redbook_open_drive(S32 which)
{
  return(rbcompleteopen(rbopendrive(which)));
}


//############################################################################
//##                                                                        ##
//## Close the handle to the Red Book device (free the memory, etc).        ##
//##                                                                        ##
//############################################################################

void cdecl AIL_API_redbook_close(HREDBOOK hand)
{
  if (hand==0)
    return;
  rbclose();
  AIL_mem_free_lock(hand);
}


//############################################################################
//##                                                                        ##
//## Eject a CD from a Red Book device.                                     ##
//##                                                                        ##
//############################################################################

void cdecl AIL_API_redbook_eject(HREDBOOK hand)
{
  if (hand==0)
    return;
  rbwrite(hand->DeviceID,0);
  hand->paused=0;
}


//############################################################################
//##                                                                        ##
//## Retract a CD into a Red Book device.                                   ##
//##                                                                        ##
//############################################################################

void cdecl AIL_API_redbook_retract(HREDBOOK hand)
{
  if (hand==0)
    return;
  rbwrite(hand->DeviceID,5);
  hand->paused=0;
}


//############################################################################
//##                                                                        ##
//## Returns the current status of a Red book device.  Possibilities are:   ##
//##                                                                        ##
//##    REDBOOK_ERROR     CD is a data disk or was removed or is bad.       ##
//##    REDBOOK_STOPPED   CD is stopped.                                    ##
//##    REDBOOK_PLAYING   CD is playing.                                    ##
//##    REDBOOK_PAUSED    CD is playing, but is paused.                     ##
//##                                                                        ##
//############################################################################

U32 cdecl AIL_API_redbook_status(HREDBOOK hand)
{
  U32 stat;
  if (hand) {
    stat=rbstatus(hand->DeviceID);
    if (stat) {

      if (hand->readcontents) {
        if (rbcontents(hand->DeviceID,&hand->info)) 
          hand->readcontents=0;
        else
          goto error;
      }

      if (stat==1)
        return(REDBOOK_PLAYING);
      else 
        return(hand->paused?REDBOOK_PAUSED:REDBOOK_STOPPED);

    }

  error:
    hand->paused=0;     // reset paused status if there is an error
    hand->readcontents=1;

  }

  return(REDBOOK_ERROR);
}


//############################################################################
//##                                                                        ##
//## Return the number of tracks on the Red book device.                    ##
//##                                                                        ##
//############################################################################

U32 cdecl AIL_API_redbook_tracks(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_ERROR)
    return(0);

   return(hand->info.tracks);
}


//##############################################################################
//##                                                                          ##
//## Returns the starting and ending millisec counts for the specified track. ##
//##                                                                          ##
//##############################################################################

void cdecl AIL_API_redbook_track_info(HREDBOOK hand,U32 tracknum,U32* startmsec,U32* endmsec)
{
  if ((AIL_redbook_status(hand)==REDBOOK_ERROR) || (tracknum<1) || (tracknum>hand->info.tracks)) {
    if (startmsec)
      *startmsec=0;
    if (endmsec)
      *endmsec=0;
    return;
  }

  if (startmsec)
    *startmsec=hand->info.trackstarts[tracknum-1];

  if (endmsec)
    *endmsec=hand->info.trackstarts[tracknum]-1;
}


//############################################################################
//##                                                                        ##
//## Returns a special hashed value that will uniquely identify the CD.     ##
//##                                                                        ##
//############################################################################

static U32 stupidhash[4]={0xb16eade1L,0x471f295aL,0x38bca4d5L,0xe41926fcL};
#define rotate(val) ((((U32)(val))<<8L)|(((U32)(val))>>24L))

U32 cdecl AIL_API_redbook_id(HREDBOOK hand)
{
  U32 count,hash,tracks,starts,ends,i;
  if (AIL_redbook_status(hand)==REDBOOK_ERROR) 
    return(0);

  count=0;
  hash=0;
  
  tracks=AIL_redbook_tracks(hand);
  hash=hash^stupidhash[(++count&3)]^tracks;
  hash=rotate(hash);

  for(i=0;i<tracks;i++) {
    AIL_redbook_track_info(hand,i+1,&starts,&ends);
    hash=hash^stupidhash[(++count&3)]^starts;
    hash=rotate(hash);
    hash=hash^stupidhash[(++count&3)]^ends;
    hash=rotate(hash);
  } 

  return(hash);
}


//############################################################################
//##                                                                        ##
//## Returns the current position of a Red book device in milliseconds.     ##
//##                                                                        ##
//############################################################################

U32 cdecl AIL_API_redbook_position(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_PLAYING)
    return(rbpos(hand->DeviceID,0));

  return(0);
}


//############################################################################
//##                                                                        ##
//## Returns the current track on of a Red book device.                     ##
//##                                                                        ##
//############################################################################

U32 cdecl AIL_API_redbook_track(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_PLAYING)
    return(rbpos(hand->DeviceID,1));

  return(0);
}

//############################################################################
//##                                                                        ##
//## Starts a Red Book Device playing.  Returns the new Red book status.    ##
//##                                                                        ##
//##  startsec and endsec specify the starting and ending millisec count.   ##
//##                                                                        ##
//############################################################################

U32 cdecl AIL_API_redbook_play(HREDBOOK hand,U32 startmsec, U32 endmsec)
{
  U32 stat=AIL_redbook_status(hand);
  if (stat!=REDBOOK_ERROR) {
    if (stat!=REDBOOK_STOPPED)
      rbstop(hand->DeviceID);
    if (startmsec|endmsec) {
      hand->lastendsec=endmsec;
      rbplay(hand->DeviceID,startmsec,endmsec-startmsec);
    }
  }
  
  return(AIL_redbook_status(hand));
}


//############################################################################
//##                                                                        ##
//## Stops a playing Red Book Device.  Returns the new Red book status.     ##
//##                                                                        ##
//############################################################################

U32 cdecl AIL_API_redbook_stop(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)!=REDBOOK_ERROR) {
    hand->paused=0;
    rbstop(hand->DeviceID);
    rbstop(hand->DeviceID);
  }
  return(AIL_redbook_status(hand));
}


//################################################################################
//##                                                                            ##
//## Pauses the playing of a Red Book Device.  Returns the new Red book status. ##
//##                                                                            ##
//################################################################################

U32 cdecl AIL_API_redbook_pause(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_PLAYING) {
    hand->pausedsec=AIL_redbook_position(hand);
    hand->paused=1;
    rbstop(hand->DeviceID);
  }
  return(AIL_redbook_status(hand));
}


//############################################################################
//##                                                                        ##
//## Resumes a paused Red Book Device.  Returns the new red book status.    ##
//##                                                                        ##
//############################################################################

U32 cdecl AIL_API_redbook_resume(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_PAUSED) {
    hand->paused=0;
    AIL_redbook_play(hand,hand->pausedsec,hand->lastendsec);
  }
  return(AIL_redbook_status(hand));
}


//############################################################################
//##                                                                        ##
//## Get the volume of a Red Book Device.  Returns 0 to 127.                ##
//##                                                                        ##
//############################################################################

F32 cdecl AIL_API_redbook_volume_level(HREDBOOK hand)
{
  if (hand)
    return(rbgetvol(hand->DeviceID)/127.0F);
  return(-1);
}


//############################################################################
//##                                                                        ##
//## Set the volume of a Red Book Device.  Use 0 to 127.                    ##
//##                                                                        ##
//############################################################################

F32 cdecl AIL_API_redbook_set_volume_level(HREDBOOK hand, F32 volume)
{
  if (hand) {
    rbsetvol(hand->DeviceID,(U8)( volume * 127.0f));
    return(AIL_redbook_volume_level(hand));
  }
  return(-1);
}
