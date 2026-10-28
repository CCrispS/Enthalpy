// Enthalpy: DLL entry. Work here is deliberately tiny - opens the log, records the module handle,
// and disables thread notifications. Everything else waits for the WinMain hook, because the game
// is not ready to be touched while DllMain is still running under the loader lock.
#include "Log.h"

static HMODULE g_hModule = nullptr;

HMODULE GetSelfModule()
{
	return g_hModule;
}

BOOL WINAPI DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(lpReserved);

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		g_hModule = hModule;
		DisableThreadLibraryCalls(hModule);
		LogInit(hModule);
		break;
	case DLL_PROCESS_DETACH:
		LogShutdown();
		break;
	default:
		break;
	}

	return TRUE;
}
