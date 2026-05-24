//############################################################################
//##                                                                        ##
//##  Miles Sound System                                                    ##
//##                                                                        ##
//##  mssinput.cpp: Digital sound input API module and support routines     ##
//##                                                                        ##
//##  16-bit protected-mode source compatible with MSC 7.0                  ##
//##  32-bit protected-mode source compatible with MSC 9.0                  ##
//##                                                                        ##
//##  Version 1.00 of 8-Dec-98: Initial                                     ##
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
#include <stdlib.h>

//############################################################################
//##                                                                        ##
//## Open input device                                                      ##
//##                                                                        ##
//############################################################################

HDIGINPUT AILCALL AIL_API_open_input (AIL_INPUT_INFO FAR *info)
{
   HDIGINPUT dig;

   //
   // Allocate memory for DIG_INPUT_DRIVER structure
   //

   dig = (HDIGINPUT) AIL_mem_alloc_lock(sizeof(struct _DIG_INPUT_DRIVER));

   if (dig == NULL)
      {
      AIL_set_error("Could not allocate memory for input driver descriptor.");

      return NULL;
      }

   //
   // Explicitly initialize all DIG_INPUT_DRIVER fields to NULL/0
   //

   AIL_memset(dig,
              0,
              sizeof(*dig));

   //
   // Assign info field to descriptor
   //

   dig->info          = *info;

   //
   // Assign background timer
   //

   dig->background_timer = -1; //AIL_register_timer(background_callback);

   if (dig->background_timer == -1)
      {
      AIL_set_error("Out of timer handles");
      AIL_close_input(dig);

      return 0;
      }

   //
   // Return success
   //

   return dig;
}

//############################################################################
//##                                                                        ##
//## Close input device                                                     ##
//##                                                                        ##
//############################################################################

void AILCALL AIL_API_close_input     (HDIGINPUT dig)
{
   if (dig==0)
     return;

   AIL_mem_free_lock(dig);
}

//############################################################################
//##                                                                        ##
//## Get input device info                                                  ##
//##                                                                        ##
//############################################################################

AIL_INPUT_INFO FAR * AILCALL AIL_API_get_input_info (HDIGINPUT dig)
{
   return &dig->info;
}

//############################################################################
//##                                                                        ##
//## Enable/disable input                                                   ##
//##                                                                        ##
//############################################################################

S32  AILCALL AIL_API_set_input_state (HDIGINPUT         dig, //)
                                      S32               enable)
{
   if (dig->input_enabled == enable)
      {
      return dig->input_enabled;
      }

   if (enable)
      {
      //
      // Return success
      //

      dig->input_enabled = 1;

      return 1;
      }
   else
      {
      //
      // Return success
      //

      dig->input_enabled = 0;

      return 1;
      }
}
