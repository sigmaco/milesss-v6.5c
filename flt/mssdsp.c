#include "mss.h"

extern S32 AutoPanMain( HPROVIDER provider_handle, U32 up_down );
extern S32 BandPassMain( HPROVIDER provider_handle, U32 up_down );
extern S32 CaptureMain( HPROVIDER provider_handle, U32 up_down );
extern S32 ChorusMain( HPROVIDER provider_handle, U32 up_down );
extern S32 CompressMain( HPROVIDER provider_handle, U32 up_down );
extern S32 FlangeMain( HPROVIDER provider_handle, U32 up_down );
extern S32 HighPassMain( HPROVIDER provider_handle, U32 up_down );
extern S32 LagInterMain( HPROVIDER provider_handle, U32 up_down );
extern S32 MDelayMain( HPROVIDER provider_handle, U32 up_down );
extern S32 SDelayMain( HPROVIDER provider_handle, U32 up_down );
extern S32 ParmEqMain( HPROVIDER provider_handle, U32 up_down );
extern S32 PhaserMain( HPROVIDER provider_handle, U32 up_down );
extern S32 ResonMain( HPROVIDER provider_handle, U32 up_down );
extern S32 RingModMain( HPROVIDER provider_handle, U32 up_down );
extern S32 ShelfEqMain( HPROVIDER provider_handle, U32 up_down );
extern S32 DolbyMain( HPROVIDER provider_handle, U32 up_down );

#ifdef A1_NO_SATIC
extern S32 MSS_RIB_Main( HPROVIDER provider_handle, U32 up_down );
extern S32 MSS_RIB_Main( HPROVIDER provider_handle, U32 up_down )
#else
extern S32 MSS_DSPInit(HPROVIDER provider_handle, U32 up_down)
#endif
{
  static HPROVIDER bandpassPH, capturePH, chorusPH, compressPH, flangePH,
                   highpassPH, laginterPH, mdelayPH, sdelayPH, parmeqPH,
                   phaserPH, resonPH, ringmodPH, shelfeqPH, dolbyPH;

  if ( up_down )
  {
    bandpassPH = RIB_alloc_provider_handle(0);
#ifdef IS_WINDOWS
    capturePH = RIB_alloc_provider_handle(0);
#endif
    chorusPH = RIB_alloc_provider_handle(0);
    compressPH = RIB_alloc_provider_handle(0);
    flangePH = RIB_alloc_provider_handle(0);
    highpassPH = RIB_alloc_provider_handle(0);
    laginterPH = RIB_alloc_provider_handle(0);
    mdelayPH = RIB_alloc_provider_handle(0);
    sdelayPH = RIB_alloc_provider_handle(0);
    parmeqPH = RIB_alloc_provider_handle(0);
    phaserPH = RIB_alloc_provider_handle(0);
    resonPH = RIB_alloc_provider_handle(0);
    ringmodPH = RIB_alloc_provider_handle(0);
    shelfeqPH = RIB_alloc_provider_handle(0);
    dolbyPH = RIB_alloc_provider_handle(0);
  }

  BandPassMain( bandpassPH , up_down );
#ifdef IS_WINDOWS
  CaptureMain( capturePH, up_down );
#endif
  ChorusMain( chorusPH, up_down );
  CompressMain( compressPH, up_down );
  FlangeMain( flangePH, up_down );
  HighPassMain( highpassPH, up_down );
  LagInterMain( laginterPH, up_down );
  MDelayMain( mdelayPH, up_down );
  SDelayMain( sdelayPH, up_down );
  ParmEqMain( parmeqPH, up_down );
  PhaserMain( phaserPH, up_down );
  ResonMain( resonPH, up_down );
  RingModMain( ringmodPH, up_down );
  ShelfEqMain( shelfeqPH, up_down );
  DolbyMain( dolbyPH, up_down );

  AutoPanMain( provider_handle, up_down );

  return( TRUE );
}


#ifdef IS_WINDOWS

//############################################################################
//#                                                                          #
//# DLLMain registers FLT API interface at load time                         #
//#                                                                          #
//############################################################################

#ifdef A1_NO_SATIC
BOOL WINAPI DllMain(HINSTANCE hinstDll, //)
                          U32     fdwReason,
                          LPVOID    plvReserved)
{
   switch (fdwReason)
      {
      case DLL_PROCESS_ATTACH:
         DisableThreadLibraryCalls( hinstDll );
         return( MSS_RIB_Main( RIB_provider_library_handle(), 1 ) );

      case DLL_PROCESS_DETACH:

         return( MSS_RIB_Main( RIB_provider_library_handle(), 0 ) );
      }

   return TRUE;
}
#endif

#endif
