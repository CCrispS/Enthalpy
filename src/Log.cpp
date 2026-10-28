#include "Log.h"

#include "Settings.h"

#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <share.h>

static FILE* g_LogFile = nullptr;
static CRITICAL_SECTION g_LogCS;
static bool g_LogReady = false;
static char g_LogPath[MAX_PATH] = { 0 };

static bool TryOpen(const char* path)
{
	// deny other writers, but allow readers so the log can be tailed while the game runs
	g_LogFile = _fsopen(path, "w", _SH_DENYWR);
	if (!g_LogFile)
		return false;

	strncpy_s(g_LogPath, sizeof(g_LogPath), path, _TRUNCATE);
	return true;
}

static void OpenLogFile()
{
	// Preferred: beside the DLL, which is what users expect.  This fails when the game sits
	// under Program Files and the process is not elevated - Mental Omega is a real case - so
	// there has to be a writable fallback rather than silently producing no log at all.
	char besideDll[MAX_PATH] = { 0 };
	snprintf(besideDll, sizeof(besideDll), "%sEnthalpy.log", Settings::PluginDirectory());

	if (TryOpen(besideDll))
		return;

	char fallbackDirectory[MAX_PATH] = { 0 };
	char* localAppData = nullptr;
	size_t required = 0;
	if (_dupenv_s(&localAppData, &required, "LOCALAPPDATA") == 0 && localAppData)
	{
		snprintf(fallbackDirectory, sizeof(fallbackDirectory), "%s\\Enthalpy", localAppData);
		free(localAppData);
	}

	if (fallbackDirectory[0] == '\0')
		return;

	CreateDirectoryA(fallbackDirectory, nullptr);

	char fallbackPath[MAX_PATH] = { 0 };
	snprintf(fallbackPath, sizeof(fallbackPath), "%s\\Enthalpy.log", fallbackDirectory);
	TryOpen(fallbackPath);
}

void LogInit(HMODULE hModule)
{
	UNREFERENCED_PARAMETER(hModule);

	InitializeCriticalSection(&g_LogCS);
	g_LogReady = true;

	// Enthalpydebug decides whether the file exists at all, so it has to be read here and not
	// on the first gesture.  With it off nothing is created and every Logf becomes a no-op.
	if (!Settings::ReadDebugLog())
		return;

	OpenLogFile();

	SYSTEMTIME st;
	GetLocalTime(&st);
	Logf("==== Enthalpy loaded %04d-%02d-%02d %02d:%02d:%02d ====",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	if (g_LogFile)
		Logf("log: %s", g_LogPath);
	else
		// Nothing was created, so the caller has no way to see this line either; it is kept so
		// that a debugger or a future sink can still report why logging is unavailable.
		OutputDebugStringA("Enthalpy: debug logging requested but no writable log path was found");

	const char* ini = Settings::IniFileUsed();
	Logf("settings: ini=%s", (ini && ini[0]) ? ini : "(none found, using defaults)");
}

void LogShutdown()
{
	Logf("==== Enthalpy unloaded ====");

	if (g_LogReady)
	{
		if (g_LogFile)
		{
			fclose(g_LogFile);
			g_LogFile = nullptr;
		}
		g_LogReady = false;
		DeleteCriticalSection(&g_LogCS);
	}
}

void Logf(const char* fmt, ...)
{
	if (!g_LogReady || !g_LogFile)
		return;

	EnterCriticalSection(&g_LogCS);

	SYSTEMTIME st;
	GetLocalTime(&st);

	char buffer[2048];
	int prefix = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "[%02d:%02d:%02d.%03d] ",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	va_list args;
	va_start(args, fmt);
	vsnprintf_s(buffer + prefix, sizeof(buffer) - prefix, _TRUNCATE, fmt, args);
	va_end(args);

	fputs(buffer, g_LogFile);
	fputc('\n', g_LogFile);
	fflush(g_LogFile);

	LeaveCriticalSection(&g_LogCS);
}

const char* LogFilePath()
{
	return g_LogPath;
}
