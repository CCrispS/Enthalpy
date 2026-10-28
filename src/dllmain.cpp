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
