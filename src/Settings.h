#pragma once

#include <Windows.h>

// Tunables read from an INI beside the game, section [Enthalpy].
//
// Which file is used is resolved once by looking for the first candidate that actually has
// an [Enthalpy] section: Enthalpy.ini, then RA2MD.INI (stock YR), then RA2MO.ini (Mental
// Omega), then RA2.INI.  The game itself does not have to be any particular mod, and a mod
// that ships its own settings file still works.
//
// The section is re-read on every gesture, so values can be edited and tried without
// restarting the game.  Enthalpydebug is the exception: it is read once at load time,
// because it decides whether the log file is created at all.
namespace Settings
{
	struct Values
	{
		// Whether pressing both buttons with nothing selected may reuse the units that were
		// selected the last time.  Off by default: with an empty selection nothing happens.
		bool FormationSelectSaved = false;

		// Whether the coloured cell footprint is drawn under each ghost.  Off by default:
		// the ghost alone reads better, the square is only useful while learning the grid.
		bool TargetBlock = false;

		// Opacity of the formation ghosts, 0..4 (0 = invisible, 4 = opaque).
		int GhostOpacity = 2;

		// Whether the drop shadow painted by the voxel renderer is kept in the silhouette.
		bool GhostShadow = false;

		// Silhouette colours, written as "R,G,B" (0..255 each) in the ini and stored as the
		// engine's 16 bit 565 pixels.  Defaults reproduce the original green / red look.
		DWORD ColorValid = 0x03E0;    // 0,251,0  legal cell
		DWORD ColorInvalid = 0xF800;  // 255,0,0  illegal cell
		DWORD ColorEdge = 0x03E0;     // 0,251,0  outline

		// Whether the DLL writes a log file.  Off by default.
		bool DebugLog = false;
	};

	// Reads the resolved INI and falls back to the defaults above for any key that is
	// missing or unreadable.
	Values Load();

	// The values that were read last, for code that cannot re-read the file.
	const Values& Current();

	// Reads only Enthalpydebug, without touching Current().  Used at load time, before the
	// plugin has done anything else, to decide whether a log file should exist.
	bool ReadDebugLog();

	// Full path of the INI the values came from, or an empty string when no candidate had an
	// [Enthalpy] section (in which case every value is a default).  Logged at startup so a
	// user can see which file to edit.
	const char* IniFileUsed();

	// Resolves (once) and returns the directory the plugin lives in, with a trailing slash.
	const char* PluginDirectory();
}
