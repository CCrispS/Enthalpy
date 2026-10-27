#pragma once

#include <windows.h>

void LogInit(HMODULE hModule);
void LogShutdown();
void Logf(const char* fmt, ...);

// Path the log was opened with, or an empty string when nothing was written.  Logged at
// startup so a user can see where the file went (it is not always beside the DLL).
const char* LogFilePath();
