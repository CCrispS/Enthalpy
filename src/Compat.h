#pragma once

#include <Windows.h>

// Checks that the running game is a build this plugin was written against.
//
// Every hook address in this plugin is an absolute address in Yuri's Revenge 1.001, so a
// different gamemd.exe (1.000, a mod's patched exe, Red Alert 2 without the expansion) does
// not merely fail to work - it jumps into the middle of unrelated code and crashes.  Rather
// than trusting the user to know that, the plugin reads the bytes it is about to patch from
// the file on disk and reports exactly what it found.
namespace Compat
{
	struct Report
	{
		// True when every hook site carries the expected instruction bytes.
		bool BuildMatches = false;

		// Size of gamemd.exe in bytes, and the image base recorded in its headers.
		DWORD ImageSize = 0;
		DWORD ImageBase = 0;

		// Human readable one-line summary for the log.
		char Summary[512] = { 0 };
	};

	// Reads gamemd.exe next to the loaded module and verifies the instructions this plugin
	// patches.  Never modifies anything; on failure the summary explains which site differed.
	Report VerifyGameBuild();
}
