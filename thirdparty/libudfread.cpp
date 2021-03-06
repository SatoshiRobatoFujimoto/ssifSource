// MIT license
// 
// Copyright 2017 Vyacheslav Napadovsky <napadovskiy@gmail.com>.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#include "libudfread.hpp"

#include <algorithm>
#include <vector>
#include <string>

#include "config.h"
#include "udfread.h"

extern "C" LIBUDFREAD_API std::streambuf* CreateUdfFileBuf(const char* filename) {
	try {
		return new udffilebuf(filename);
	}
	catch (...) {
		return nullptr;
	}
}

extern "C" LIBUDFREAD_API void DeleteUdfFileBuf(std::streambuf* ptr) {
	delete ptr;
}





constexpr int default_buffer_size = UDF_BLOCK_SIZE;

std::string GetRealPath(const std::string& path);

udffilebuf::udffilebuf(const char* filename) :
	_udf(nullptr),
	_file(nullptr),
	_buffer(nullptr)
{
	std::string dvd_fn(filename);
	std::string file_fn;

	dvd_fn = GetRealPath(dvd_fn);

	std::replace(dvd_fn.begin(), dvd_fn.end(), '/', '\\');
	size_t pos = dvd_fn.find_first_of('\\');
	while (pos != std::string::npos) {
		dvd_fn[pos] = 0;

		struct _stat64 info;
		if (_stati64(dvd_fn.c_str(), &info) == 0) {
			if ((info.st_mode & S_IFREG) != 0) {
				// found an image
				dvd_fn[pos] = '\\';
				file_fn = dvd_fn.substr(pos);
				std::replace(file_fn.begin(), file_fn.end(), '\\', '/');
				dvd_fn.resize(pos);
				break;
			}
			if ((info.st_mode & S_IFDIR) == 0)
				throw std::runtime_error("invalid file");
		}
		// else ignore errors of stat
		
		// still directory
		dvd_fn[pos] = '\\';
		pos = dvd_fn.find_first_of('\\', pos + 1);
	}

	try {
		_udf = udfread_init();
		if (_udf == nullptr)
			throw std::bad_alloc();
		if (udfread_open(_udf, dvd_fn.c_str()) < 0)
			throw std::runtime_error("Can't open the image");
		_file = udfread_file_open(_udf, file_fn.c_str());
		if (_file == nullptr)
			throw std::runtime_error("Can't open file in the image");
		_size = udfread_file_size(_file);
		_buffer = new char[default_buffer_size];
	}
	catch (...) {
		udfread_file_close(_file);
		udfread_close(_udf);
		throw;
	}
}

udffilebuf::~udffilebuf() {
	delete[] _buffer;
	udfread_file_close(_file);
	udfread_close(_udf);
}

std::streampos udffilebuf::seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode /* which */) {
	int whence;
	switch (way) {
		case std::ios_base::beg:
			whence = UDF_SEEK_SET;
			break;
		case std::ios_base::cur:
			whence = UDF_SEEK_CUR;
			break;
		case std::ios_base::end:
			whence = UDF_SEEK_END;
			break;
		default:
			return std::streamoff(-1);
	}
	auto ret = udfread_file_seek(_file, off, whence);
	this->setg(eback(), egptr(), egptr());
	return ret;
}

std::streampos udffilebuf::seekpos(std::streampos sp, std::ios_base::openmode which) {
	return seekoff(sp, std::ios_base::beg, which);
}

int udffilebuf::underflow() {
	if (this->gptr() == this->egptr()) {
		auto bytesLeft = _size - getpos();
		if (bytesLeft > 0) {
			if (bytesLeft > default_buffer_size)
				bytesLeft = default_buffer_size;
			auto ret = udfread_file_read(_file, &_buffer[0], (size_t)bytesLeft);
			if (ret < 0)
				throw std::runtime_error("error in udfread_file_read");
			this->setg(&_buffer[0], &_buffer[0], &_buffer[0] + ret);
		}
	}
	return this->gptr() == this->egptr()
		? std::char_traits<char>::eof()
		: std::char_traits<char>::to_int_type(*this->gptr());
}

std::streamoff udffilebuf::getpos() {
	return udfread_file_tell(_file);
}


#ifdef _WIN32

#include <Windows.h>
#include <Shlwapi.h>
std::string GetRealPath(const std::string& path) {
	std::string ret;
	ret.resize(MAX_PATH);
	if (!PathCanonicalizeA(&ret[0], path.c_str()))
		throw std::runtime_error("Can't canonicalize the path");
	ret.resize(strlen(ret.c_str()));
	return ret;
}

#else

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
std::string GetRealPath(const std::string& path) {
	char ret[PATH_MAX];
	if (realpath("this_source.c", buf) == nullptr)
		throw std::runtime_error("realpath method failed");
	return std::string(ret);
}

#endif
