#include "Settings.h"

#include <Windows.h>
#include <stdio.h>

#include "Log.h"

namespace
{
	Settings::Values g_Current;
	char g_IniPath[MAX_PATH] = { 0 };
	char g_LegacyPath[MAX_PATH] = { 0 };
	char g_PluginDir[MAX_PATH] = { 0 };
	bool g_Resolved = false;

	// Directory the DLL lives in, with a trailing backslash.  The game always runs from the
	// same folder, so this is also the folder holding gamemd.exe and the game's own INIs.
	void ResolvePluginDirectory()
	{
		if (g_PluginDir[0] != '\0')
			return;

		HMODULE module = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&ResolvePluginDirectory), &module))
		{
			return;
		}

		char path[MAX_PATH] = { 0 };
		if (!GetModuleFileNameA(module, path, MAX_PATH))
			return;

		char* lastSlash = nullptr;
		for (char* cursor = path; *cursor; ++cursor)
		{
			if (*cursor == '\\' || *cursor == '/')
				lastSlash = cursor;
		}

		if (lastSlash)
			*(lastSlash + 1) = '\0';
		else
			path[0] = '\0';

		strncpy_s(g_PluginDir, sizeof(g_PluginDir), path, _TRUNCATE);
	}

	// A file only counts as the settings source when it exists *and* carries an [Enthalpy]
	// section; otherwise a mod's own INI without our section would silently mask the search
	// and every key would read as a default.
	bool HasEnthalpySection(const char* path)
	{
		if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
			return false;

		char probe[8] = { 0 };
		const DWORD length = GetPrivateProfileStringA("Enthalpy", "GhostOpacity", "",
			probe, sizeof(probe), path);
		if (length > 0)
			return true;

		// GhostOpacity may legitimately be absent while other keys are present, so probe a key
		// that exists in every documented example as well.
		const DWORD second = GetPrivateProfileStringA("Enthalpy", "TargetBlock", "",
			probe, sizeof(probe), path);
		return second > 0;
	}

	void ResolveIni()
	{
		if (g_Resolved)
			return;

		g_Resolved = true;

		ResolvePluginDirectory();
		if (g_PluginDir[0] == '\0')
			return;

		// Enthalpy.ini first: it is the file we can create and the one a user edits when the
		// game itself uses a mod-specific settings file.
		static const char* const kCandidates[] = {
			"Enthalpy.ini",
			"RA2MD.INI",
			"RA2MO.ini",
			"RA2.INI",
		};

		for (int index = 0; index < static_cast<int>(sizeof(kCandidates) / sizeof(kCandidates[0])); ++index)
		{
			char candidate[MAX_PATH] = { 0 };
			snprintf(candidate, sizeof(candidate), "%s%s", g_PluginDir, kCandidates[index]);
			if (HasEnthalpySection(candidate))
			{
				strncpy_s(g_IniPath, sizeof(g_IniPath), candidate, _TRUNCATE);
				snprintf(g_LegacyPath, sizeof(g_LegacyPath), "%sRA2MD.INI", g_PluginDir);
				return;
			}
		}
	}

	// A key may live in Enthalpy.ini or in the mod's own ini. Users keep editing RA2MD.INI long
	// after the plugin moved to its own file, and a silently ignored key looks like a broken
	// feature, so every read falls back to the legacy file when the key is absent.
	DWORD ReadKey(const char* key, char* value, DWORD size)
	{
		DWORD length = GetPrivateProfileStringA("Enthalpy", key, "", value, size, g_IniPath);
		if (length != 0)
			return length;
		if (g_LegacyPath[0] == '\0')
			return 0;
		return GetPrivateProfileStringA("Enthalpy", key, "", value, size, g_LegacyPath);
	}

	bool ReadBool(const char* path, const char* key, bool fallback)
	{
		char value[32] = { 0 };
		const DWORD length = ReadKey(key, value, sizeof(value));

		if (length == 0)
			return fallback;

		return value[0] == '1' || value[0] == 'y' || value[0] == 'Y'
			|| value[0] == 't' || value[0] == 'T';
	}

	int ReadInt(const char* path, const char* key, int fallback)
	{
		char value[32] = { 0 };
		const DWORD length = ReadKey(key, value, sizeof(value));

		if (length == 0)
			return fallback;

		return atoi(value);
	}

	// "R,G,B" with each component 0..255, packed into the 565 pixels the engine draws with.
	DWORD PackRgb(int red, int green, int blue)
	{
		if (red < 0) red = 0; else if (red > 255) red = 255;
		if (green < 0) green = 0; else if (green > 255) green = 255;
		if (blue < 0) blue = 0; else if (blue > 255) blue = 255;

		return (static_cast<DWORD>(red >> 3) << 11)
			| (static_cast<DWORD>(green >> 2) << 5)
			| static_cast<DWORD>(blue >> 3);
	}

	DWORD ReadColor(const char* path, const char* key, DWORD fallback)
	{
		char value[64] = { 0 };
		const DWORD length = ReadKey(key, value, sizeof(value));

		if (length == 0)
			return fallback;

		int red = 0, green = 0, blue = 0;
		if (sscanf_s(value, "%d,%d,%d", &red, &green, &blue) != 3)
			return fallback;

		return PackRgb(red, green, blue);
	}
}

namespace Settings
{
	const char* PluginDirectory()
	{
		ResolvePluginDirectory();
		return g_PluginDir;
	}

	const char* IniFileUsed()
	{
		ResolveIni();
		return g_IniPath;
	}

	Values Load()
	{
		ResolveIni();

		Values values;
		if (g_IniPath[0] == '\0')
		{
			g_Current = values;
			return values;
		}

		values.FormationSelectSaved = ReadBool(g_IniPath, "FormationSelectSaved",
			values.FormationSelectSaved);
		values.TargetBlock = ReadBool(g_IniPath, "TargetBlock", values.TargetBlock);
		values.GhostShadow = ReadBool(g_IniPath, "GhostShadow", values.GhostShadow);
		values.ColorValid = ReadColor(g_IniPath, "GhostColorValid", values.ColorValid);
		values.ColorInvalid = ReadColor(g_IniPath, "GhostColorInvalid", values.ColorInvalid);
		// GhostColorValidEdge is the current name; GhostColorEdge is the pre-0.2 spelling and
// is only consulted when the new key is absent, so existing ini files keep working.
const DWORD legacyEdge = ReadColor(g_IniPath, "GhostColorEdge", values.ColorValidEdge);
values.ColorValidEdge = ReadColor(g_IniPath, "GhostColorValidEdge", legacyEdge);
values.ColorInvalidEdge = ReadColor(g_IniPath, "GhostColorInvalidEdge", values.ColorInvalidEdge);
	values.TargetLine = ReadBool(g_IniPath, "TargetLine", values.TargetLine);
		values.DebugLog = ReadBool(g_IniPath, "Enthalpydebug", values.DebugLog);

		// 0..4 with 0 = fully transparent and 4 = fully opaque; the ladder in between is
		// evenly spaced (25%, 50%, 75%), so the ini value is easy to reason about.
		const int opacity = ReadInt(g_IniPath, "GhostOpacity", values.GhostOpacity);
		if (opacity < 0)
			values.GhostOpacity = 0;
		else if (opacity > 4)
			values.GhostOpacity = 4;
		else
			values.GhostOpacity = opacity;

		Logf("settings: GhostOpacity=%d ini=%s legacy=%s", values.GhostOpacity, g_IniPath, g_LegacyPath);

		g_Current = values;
		return values;
	}

	const Values& Current()
	{
		return g_Current;
	}

	bool ReadDebugLog()
	{
		ResolveIni();

		if (g_IniPath[0] == '\0')
			return false;

		return ReadBool(g_IniPath, "Enthalpydebug", false);
	}
}
