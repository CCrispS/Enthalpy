#include "Compat.h"

#include <stdio.h>
#include <string.h>

namespace
{
	struct SiteCheck
	{
		DWORD Address;             // absolute virtual address in the game image
		const unsigned char* Bytes; // instruction bytes that must be there
		int Count;
		const char* Label;
	};

	// The instructions this plugin overwrites.  These are the values read out of Yuri's
	// Revenge 1.001; any other build will differ and must be refused instead of patched.
	const unsigned char kSiteInit[] = { 0x8D, 0x8D, 0xCC, 0xF5, 0xFF, 0xFF };
	const unsigned char kSiteFrame[] = { 0x81, 0xEC, 0xF4, 0x00, 0x00, 0x00 };
	const unsigned char kSiteRender[] = { 0x68, 0xA0, 0x6F, 0x88, 0x00 };
	const unsigned char kSiteClick[] = { 0x81, 0xC1, 0x80, 0x00, 0x00, 0x00 };

	const SiteCheck kSites[] = {
		{ 0x006BD68D, kSiteInit, sizeof(kSiteInit), "init" },
		{ 0x006D3D10, kSiteFrame, sizeof(kSiteFrame), "tactical render" },
		{ 0x006D9134, kSiteRender, sizeof(kSiteRender), "ghost draw" },
		{ 0x00692423, kSiteClick, sizeof(kSiteClick), "click suppression" },
	};

	// Path of gamemd.exe: the module this code lives in sits in the same folder.
	void GameExePath(char* buffer, size_t capacity)
	{
		buffer[0] = '\0';

		char directory[MAX_PATH] = { 0 };
		HMODULE module = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&GameExePath), &module))
		{
			return;
		}

		if (!GetModuleFileNameA(module, directory, MAX_PATH))
			return;

		char* lastSlash = nullptr;
		for (char* cursor = directory; *cursor; ++cursor)
		{
			if (*cursor == '\\' || *cursor == '/')
				lastSlash = cursor;
		}

		if (lastSlash)
			*(lastSlash + 1) = '\0';
		else
			directory[0] = '\0';

		snprintf(buffer, capacity, "%sgamemd.exe", directory);
	}

	// Minimal PE reader: maps a virtual address to a file offset through the section table.
	struct PeImage
	{
		FILE* File = nullptr;
		DWORD ImageBase = 0;
		DWORD SizeOfImage = 0;

		// Section table, kept small on purpose: the game has five sections.
		struct Section
		{
			DWORD VirtualAddress;
			DWORD VirtualSize;
			DWORD PointerToRawData;
			DWORD SizeOfRawData;
		} Sections[16];
		int SectionCount = 0;
	};

	bool OpenPe(const char* path, PeImage& image)
	{
		if (fopen_s(&image.File, path, "rb") != 0 || !image.File)
			return false;

		IMAGE_DOS_HEADER dos = { 0 };
		if (fread(&dos, sizeof(dos), 1, image.File) != 1 || dos.e_magic != IMAGE_DOS_SIGNATURE)
			return false;

		if (fseek(image.File, dos.e_lfanew, SEEK_SET) != 0)
			return false;

		DWORD signature = 0;
		if (fread(&signature, sizeof(signature), 1, image.File) != 1
			|| signature != IMAGE_NT_SIGNATURE)
		{
			return false;
		}

		IMAGE_FILE_HEADER fileHeader = { 0 };
		if (fread(&fileHeader, sizeof(fileHeader), 1, image.File) != 1)
			return false;

		IMAGE_OPTIONAL_HEADER32 optional = { 0 };
		if (fread(&optional, fileHeader.SizeOfOptionalHeader, 1, image.File) != 1)
			return false;

		image.ImageBase = optional.ImageBase;
		image.SizeOfImage = optional.SizeOfImage;

		const long sectionTable = dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)
			+ fileHeader.SizeOfOptionalHeader;
		if (fseek(image.File, sectionTable, SEEK_SET) != 0)
			return false;

		const int count = (fileHeader.NumberOfSections < 16) ? fileHeader.NumberOfSections : 16;
		for (int index = 0; index < count; ++index)
		{
			IMAGE_SECTION_HEADER header = { 0 };
			if (fread(&header, sizeof(header), 1, image.File) != 1)
				return false;

			image.Sections[index].VirtualAddress = header.VirtualAddress;
			image.Sections[index].VirtualSize = header.Misc.VirtualSize;
			image.Sections[index].PointerToRawData = header.PointerToRawData;
			image.Sections[index].SizeOfRawData = header.SizeOfRawData;
		}

		image.SectionCount = count;
		return true;
	}

	void ClosePe(PeImage& image)
	{
		if (image.File)
		{
			fclose(image.File);
			image.File = nullptr;
		}
	}

	// Reads the bytes that a virtual address maps to, or false when the address is not part
	// of any section or of the file's raw data.
	bool ReadVirtual(const PeImage& image, DWORD address, unsigned char* buffer, int count)
	{
		if (address < image.ImageBase)
			return false;

		const DWORD rva = address - image.ImageBase;
		for (int index = 0; index < image.SectionCount; ++index)
		{
			const PeImage::Section& section = image.Sections[index];
			const DWORD size = section.VirtualSize;
			if (rva < section.VirtualAddress || rva >= section.VirtualAddress + size)
				continue;

			const DWORD delta = rva - section.VirtualAddress;
			if (delta + static_cast<DWORD>(count) > section.SizeOfRawData)
				return false;

			if (fseek(image.File, static_cast<long>(section.PointerToRawData + delta), SEEK_SET) != 0)
				return false;

			return fread(buffer, 1, count, image.File) == static_cast<size_t>(count);
		}

		return false;
	}

	void Append(char* buffer, size_t capacity, const char* text)
	{
		const size_t used = strlen(buffer);
		if (used + 1 >= capacity)
			return;

		strncat_s(buffer, capacity, text, capacity - used - 1);
	}
}

namespace Compat
{
	Report VerifyGameBuild()
	{
		Report report;

		char path[MAX_PATH] = { 0 };
		GameExePath(path, sizeof(path));

		PeImage image;
		if (path[0] == '\0' || !OpenPe(path, image))
		{
			snprintf(report.Summary, sizeof(report.Summary),
				"could not read gamemd.exe to verify the game build");
			return report;
		}

		report.ImageBase = image.ImageBase;
		report.ImageSize = image.SizeOfImage;

		snprintf(report.Summary, sizeof(report.Summary),
			"gamemd.exe: imageBase=0x%08X size=0x%X;", image.ImageBase, image.SizeOfImage);

		bool matches = true;
		for (int index = 0; index < static_cast<int>(sizeof(kSites) / sizeof(kSites[0])); ++index)
		{
			const SiteCheck& site = kSites[index];
			unsigned char actual[8] = { 0 };
			if (!ReadVirtual(image, site.Address, actual, site.Count))
			{
				char line[96];
				snprintf(line, sizeof(line), " %s@0x%06X unreadable;", site.Label, site.Address);
				Append(report.Summary, sizeof(report.Summary), line);
				matches = false;
				continue;
			}

			bool same = true;
			for (int byte = 0; byte < site.Count; ++byte)
			{
				if (actual[byte] != site.Bytes[byte])
					same = false;
			}

			if (!same)
			{
				char line[128];
				snprintf(line, sizeof(line), " %s@0x%06X expected %02X%02X%02X got %02X%02X%02X;",
					site.Label, site.Address,
					site.Bytes[0], site.Bytes[1], site.Bytes[2],
					actual[0], actual[1], actual[2]);
				Append(report.Summary, sizeof(report.Summary), line);
				matches = false;
			}
		}

		ClosePe(image);

		report.BuildMatches = matches;
		if (matches)
			Append(report.Summary, sizeof(report.Summary), " hook sites verified");
		else
			Append(report.Summary, sizeof(report.Summary),
				" THIS IS NOT YURI'S REVENGE 1.001 - plugin disabled");

		return report;
	}
}
