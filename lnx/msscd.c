//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  MSSCD.C: Red Book CD-audio support for mss                           ##
//##                                                                        ##
//##  16-bit protected-mode source compatible with MSC 7.0                  ##
//##  32-bit protected-mode source compatible with MSC 9.0                  ##
//##                                                                        ##
//##  Version 1.00 of 02-Feb-96: Originally written.                        ##
//##  Version 1.01 of 11-May-97: New AIL_redbook_open_drive (Serge Plagnol) ##
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

#include "SDL.h"
#include "SDL_cdrom.h"


#define FRAMES_TO_MS(F)	(((F) * 1000) / CD_FPS)
#define FRAMES_TO_S(F)	((F) / CD_FPS)
#define MS_TO_FRAMES(M) (((M) * CD_FPS) / 1000)

//############################################################################
//##                                                                        ##
//## Open a handle to a Red Book Device.                                    ##
//##                                                                        ##
//##    drive is the CD-ROM drive letter				    ##
//##                                                                        ##
//############################################################################

HREDBOOK AILCALL AIL_API_redbook_open_drive(S32 drive)
{
printf("FIXME: AIL_API_redbook_open_drive\n");
  return NULL;
}


//############################################################################
//##                                                                        ##
//## Open a handle to a Red Book Device.                                    ##
//##                                                                        ##
//##    which is the number of the CD-ROM to use (0=auto).                  ##
//##                                                                        ##
//############################################################################

HREDBOOK AILCALL AIL_API_redbook_open(U32 which)
{
  HREDBOOK r;
  SDL_CD *cdrom;

  if(!SDL_WasInit(SDL_INIT_CDROM)) {
    SDL_InitSubSystem(SDL_INIT_CDROM);
  }
  cdrom = SDL_CDOpen((int)which);
  if(!cdrom) {
    return NULL;
  }

  r=(HREDBOOK)AIL_mem_alloc_lock(sizeof(REDBOOK));
  r->paused=0;
  r->DeviceID=which;
  r->cdrom=cdrom;

  return(r);
}


//############################################################################
//##                                                                        ##
//## Close the handle to the Red Book device (free the memory, etc).        ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_redbook_close(HREDBOOK hand)
{
  if (hand==0)
    return;
  
  SDL_CDClose(hand->cdrom);

  AIL_mem_free_lock(hand);
}


//############################################################################
//##                                                                        ##
//## Eject a CD from a Red Book device.                                     ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_redbook_eject(HREDBOOK hand)
{
  if (hand==0)
    return;
  
  SDL_CDEject(hand->cdrom);
}


//############################################################################
//##                                                                        ##
//## Retract a CD back into a Red Book device.                              ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_redbook_retract(HREDBOOK hand)
{
  if (hand==0)
    return;

printf("FIXME: AIL_API_redbook_retract\n");
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

U32 AILCALL AIL_API_redbook_status(HREDBOOK hand)
{
  if (hand) {
    switch(SDL_CDStatus(hand->cdrom)) {
      case CD_STOPPED:
        return REDBOOK_STOPPED;
      case CD_PLAYING:
        return REDBOOK_PLAYING;
      case CD_PAUSED:
        return REDBOOK_PAUSED;
      default:
        break;
    }
  }
  return(REDBOOK_ERROR);
}


//############################################################################
//##                                                                        ##
//## Return the number of tracks on the Red book device.                    ##
//##                                                                        ##
//############################################################################

U32 AILCALL AIL_API_redbook_tracks(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_ERROR)
    return(0);
  
  return (U32)hand->cdrom->numtracks;
}


//##############################################################################
//##                                                                          ##
//## Returns the starting and ending millisec counts for the specified track. ##
//##                                                                          ##
//##############################################################################

void AILCALL AIL_API_redbook_track_info(HREDBOOK hand,U32 tracknum,U32 FAR* startmsec,U32 FAR* endmsec)
{
  U32 index;

  if (AIL_redbook_status(hand)==REDBOOK_ERROR || hand->cdrom->numtracks <= tracknum) {
    if (startmsec)
      *startmsec=0;
    if (endmsec)
      *endmsec=0;
    return;
  }

  index = tracknum - 1;
  if(startmsec) {
    *startmsec=FRAMES_TO_MS(hand->cdrom->track[index].offset);
  }
  if(endmsec) {
    *endmsec=FRAMES_TO_MS(hand->cdrom->track[index].offset+hand->cdrom->track[index].length);
  }
}


//############################################################################
//##                                                                        ##
//## Returns a special hashed value that will uniquely identify the CD.     ##
//##                                                                        ##
//############################################################################

static U32 stupidhash[4]={0xb16eade1L,0x471f295aL,0x38bca4d5L,0xe41926fcL};
#define rotate(val) ((((U32)(val))<<8L)|(((U32)(val))>>24L))

U32 AILCALL AIL_API_redbook_id(HREDBOOK hand)
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
//## Returns the current position of a Red book device in seconds.          ##
//##                                                                        ##
//############################################################################

U32 AILCALL AIL_API_redbook_position(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_PLAYING) {
    return FRAMES_TO_S(hand->cdrom->track[hand->cdrom->cur_track].offset + hand->cdrom->cur_frame);
  }
  return(0);
}


//############################################################################
//##                                                                        ##
//## Returns the current track of a Red book device.                        ##
//##                                                                        ##
//############################################################################

U32 AILCALL AIL_API_redbook_track(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)==REDBOOK_PLAYING) {
    return(hand->cdrom->cur_track+1);
  }
  return(0);
}


//############################################################################
//##                                                                        ##
//## Starts a Red Book Device playing.  Returns the new Red book status.    ##
//##                                                                        ##
//##  startsec and endsec specify the starting and ending millisec count.   ##
//##                                                                        ##
//############################################################################

U32 AILCALL AIL_API_redbook_play(HREDBOOK hand,U32 startmsec, U32 endmsec)
{
  if (AIL_redbook_status(hand)!=REDBOOK_ERROR) {
    SDL_CDPlay(hand->cdrom, MS_TO_FRAMES(startmsec), MS_TO_FRAMES(endmsec));
  } else {
    return(REDBOOK_ERROR);
  }
  return(AIL_redbook_status(hand));
}


//############################################################################
//##                                                                        ##
//## Stops a playing Red Book Device.  Returns the new Red book status.     ##
//##                                                                        ##
//############################################################################

U32 AILCALL AIL_API_redbook_stop(HREDBOOK hand)
{
  if (AIL_redbook_status(hand)!=REDBOOK_ERROR) {
    SDL_CDStop(hand->cdrom);
  } else {
    return(REDBOOK_ERROR);
  }
  return(AIL_redbook_status(hand));
}


//################################################################################
//##                                                                            ##
//## Pauses the playing of a Red Book Device.  Returns the new Red book status. ##
//##                                                                            ##
//################################################################################

U32 AILCALL AIL_API_redbook_pause(HREDBOOK hand)
{
  U32 s=AIL_redbook_status(hand);

  if (s==REDBOOK_ERROR)
    return(REDBOOK_ERROR);

  if (s==REDBOOK_PLAYING) {
    SDL_CDPause(hand->cdrom);
  }
  return(AIL_redbook_status(hand));
}


//############################################################################
//##                                                                        ##
//## Resumes a paused Red Book Device.  Returns the new red book status.    ##
//##                                                                        ##
//############################################################################

U32 AILCALL AIL_API_redbook_resume(HREDBOOK hand)
{
  U32 s=AIL_redbook_status(hand);

  if (s==REDBOOK_ERROR)
    return(REDBOOK_ERROR);

  if (s==REDBOOK_PAUSED) {
    SDL_CDResume(hand->cdrom);
  }
  return(AIL_redbook_status(hand));
}


//############################################################################
//##                                                                        ##
//## Gets the current volume of the CD device.  Returns 0 to 1.0.           ##
//##                                                                        ##
//############################################################################

F32 AILCALL AIL_API_redbook_volume_level(HREDBOOK hand)
{
printf("FIXME: AIL_API_redbook_volume_level\n");
  return(-1);
}

//############################################################################
//##                                                                        ##
//## Sets the current volume of the CD device.  Returns 0 to 127.           ##
//##                                                                        ##
//############################################################################

F32 AILCALL AIL_API_redbook_set_volume_level(HREDBOOK hand, F32 volume)
{
printf("FIXME: AIL_API_redbook_set_volume_level\n");
  return(AIL_redbook_volume_level(hand));
}

