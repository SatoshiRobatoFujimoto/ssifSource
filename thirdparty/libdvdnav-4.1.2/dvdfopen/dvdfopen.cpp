#include "stdafx.h"
#include "dvdfopen.h"

class fsfile: public common_file_reader {
private:
	HANDLE file;
public:
	fsfile(const char* filename) {
		file = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	}
	fsfile(const wchar_t* filename) {
		file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	}
	~fsfile() {
		CloseHandle(file);
	}
	uint64_t read(void* buffer, uint64_t bytes) {
		const DWORD default_buf_size = 1024*1024;
		uint64_t read_bytes = 0;
		uint64_t bytes_left = bytes;
		if (file == INVALID_HANDLE_VALUE)
			return 0;
		do {
			DWORD ret;
			if (!ReadFile(file, buffer, (DWORD)min(bytes_left, (uint64_t)default_buf_size), &ret, NULL))
				break;
			read_bytes += ret;
			bytes_left -= ret;
			buffer = (void*)((char*)buffer + ret);
		} while (bytes_left > 0);
		return read_bytes;
	}
	bool seek(int64_t offset, int seek_type) {
		if (file == INVALID_HANDLE_VALUE)
			return false;

		DWORD st;
		if (seek_type < 0)
			st = FILE_BEGIN;
		else if (seek_type > 0)
			st = FILE_END;
		else
			st = FILE_CURRENT;
		LONG hw = (DWORD)(offset >> 32);

		DWORD ret = SetFilePointer(file, (DWORD)offset, &hw, st);
		return (ret != (DWORD)(-1));
	}
	uint64_t getpos() {
		if (file == INVALID_HANDLE_VALUE)
			return 0;
		LONG hw;
		DWORD ret = SetFilePointer(file, 0, &hw, FILE_CURRENT);
		if (GetLastError() != NO_ERROR)
			return 0;
		return ((uint64_t)ret) | (((uint64_t)hw) << 32);
	}
	uint64_t filesize() {
		if (file == INVALID_HANDLE_VALUE)
			return 0;
		DWORD hw;
		DWORD ret = GetFileSize(file, &hw);
		if (ret == (DWORD)(-1))
			return 0;
		return ((uint64_t)ret) | (((uint64_t)hw) << 32);
	}
	bool iserror() {
		return file == INVALID_HANDLE_VALUE;
	}
};

class dvdfile: public common_file_reader {
private:
	dvd_reader_t *dvd;
	dvd_file_t *file;
public:
	dvdfile(const char* dvd_filename, const char* internal_filename) {
		dvd = DVDOpen(dvd_filename);
		if (!dvd) return;
		file = DVDOpenFilename(dvd, const_cast<char*>(internal_filename));
	}
	~dvdfile() {
		if (file) DVDCloseFile(file);
		if (dvd) DVDClose(dvd);
	}
	uint64_t read(void* buffer, uint64_t bytes) {
		if (iserror())
			return 0;
		const size_t default_buf_size = 2048;
		int64_t diff = filesize() - getpos();
		uint64_t bytes_left = (uint64_t)max(diff, 0);
		bytes_left = min(bytes_left, bytes);
		uint64_t read_bytes = 0;
		do {
			size_t to_read = (size_t)min((uint64_t)default_buf_size, bytes_left);
			if (DVDReadBytes(file, buffer, to_read) <= 0)
				break;
			read_bytes += to_read;
			bytes_left -= to_read;
			buffer = (void*)((char*)buffer + to_read);
		} while (bytes_left > 0);
		return read_bytes;
	}
	bool seek(int64_t offset, int seek_type) {
		if (seek_type < 0)
			return DVDFileSeek(file, offset) != -1;
		else if (seek_type > 0)
			return DVDFileSeek(file, filesize()+offset) != -1;
		else
			return DVDFileSeek(file, getpos()+offset) != -1;
	}
	uint64_t getpos() {
		return DVDGetFilePos(file);
	}
	uint64_t filesize() {
		if (iserror())
			return 0;
		return DVDGetFileSize(file);
	}
	bool iserror() {
		return !dvd || !file;
	}
};

common_file_reader* universal_fopen(const wchar_t *filename) {
	USES_CONVERSION;
	common_file_reader *res = new fsfile(filename);
	if (res->iserror()) {
		delete res;
		res = NULL;

		std::wstring cpy(filename);
		size_t ret = cpy.rfind(L"|");
		if (ret != std::wstring::npos) {
			std::wstring dvd_filename = cpy.substr(0, ret);
			std::wstring internal_filename = L"/" + cpy.substr(ret+1);
			for(std::wstring::iterator it = internal_filename.begin(); it != internal_filename.end(); ++it)
				if (*it == '\\')
					*it = '/';
			res = new dvdfile(W2A(dvd_filename.c_str()), W2A(internal_filename.c_str()));
			if (res->iserror()) {
				delete res;
				res = NULL;
			}
		}
	}
	return res;
}

common_file_reader* universal_fopen(const char *filename) {
	USES_CONVERSION;
	return universal_fopen(A2W(filename));
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
	}
	return TRUE;
}
