/*
    MILES 3D STATIC BUILD INITIALIZED
    31 OF JULY 2020
    ARVES100
*/

#include <mss.h>

extern "C" BOOL WINAPI MSSDllMain(HINSTANCE hinstDll, DWORD fdwRreason, LPVOID plvReserved);
extern "C" S32 hwMain(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 dx7snMain(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 dx7slMain(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 dx7shMain(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 a3d2Main(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 EAX2Main(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 EAXMain(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 swMain(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 EAX3Main(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 a3dMain(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 MSS_DSPInit(HPROVIDER provider_handle, U32 up_down);
extern "C" S32 MSS_MP3API(HPROVIDER provider_handle, U32 up_down);

extern "C" S32 MSS_RIB_Main(HPROVIDER provider_handle, U32 up_down)
{
    static HPROVIDER EAXPH, EAX2PH;

    if (up_down)
    {
        EAX2PH = RIB_alloc_provider_handle(0);
        EAXPH = RIB_alloc_provider_handle(0);
    }

    EAX3Main(provider_handle, up_down);
    EAX2Main(EAX2PH, up_down);
    EAXMain(EAXPH, up_down);

    static HPROVIDER a3d2PH;

    if (up_down)
    {
        a3d2PH = RIB_alloc_provider_handle(0);
    }

    a3dMain(provider_handle, up_down);
    a3d2Main(a3d2PH, up_down);

    static HPROVIDER dx7slPH, dx7shPH;

    if (up_down)
    {
        dx7slPH = RIB_alloc_provider_handle(0);
        dx7shPH = RIB_alloc_provider_handle(0);
    }

    dx7snMain(provider_handle, up_down);
    dx7slMain(dx7slPH, up_down);
    dx7shMain(dx7shPH, up_down);

    static HPROVIDER hwarePH;

    if (up_down)
    {
        hwarePH = RIB_alloc_provider_handle(0);
    }

    swMain(provider_handle, up_down);
    hwMain(hwarePH, up_down);

    static HPROVIDER dspPH;

    if (up_down)
    {
        dspPH = RIB_alloc_provider_handle(0);
    }

    if (!MSS_DSPInit(dspPH, up_down))
        return FALSE;


    static HPROVIDER mp3PH;

    if (up_down)
    {
        mp3PH = RIB_alloc_provider_handle(0);
    }

    if (!MSS_MP3API(mp3PH, up_down))
        return FALSE;

    return(TRUE);
}

extern "C" BOOL WINAPI M3DMain(HINSTANCE hinstDll,U32 fdwReason, LPVOID plvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!MSSDllMain(hinstDll, fdwReason, plvReserved))
            return FALSE;

        DisableThreadLibraryCalls(hinstDll);
        return(MSS_RIB_Main(RIB_provider_library_handle(), 1));

    case DLL_PROCESS_DETACH:
        return(MSS_RIB_Main(RIB_provider_library_handle(), 0));
    }

    return TRUE;
}
