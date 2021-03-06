//
// WinAPI pipe tools 
// Copyright 2016 Vyacheslav Napadovsky.
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


#pragma once

namespace Tools {
	namespace Pipe {

		enum {
			READ_CHUNK_SIZE = 0x80000,					// read chunk size
			PIPE_BUFFER_SIZE = 128 * 0x100000,			// request queue site from OS
			CLONE_BUFFER_SIZE = PIPE_BUFFER_SIZE,		// same for CloneThread
			WAIT_TIME = 3000,							// in ms, for hangs detection
		};

		class Thread {
		public:
			HANDLE hRead;
			HANDLE hThread;
			Thread() : hRead(nullptr), hThread(nullptr), error(false), stopEvent(false) { }
			Thread(const Thread& other) = delete;
			virtual ~Thread() { }
			operator bool() { return !error; }
		protected:
			volatile bool error;
			volatile bool stopEvent;
		};

		class ProxyThread : public Thread {
		public:
			HANDLE hWrite;
			ProxyThread(LPCSTR name_read, LPCSTR name_write);
			~ProxyThread();
		private:
			static DWORD WINAPI ThreadProc(LPVOID param);
		};

		class CloneThread : public Thread {
		public:
			HANDLE hWrite1, hWrite2;
			CloneThread(LPCSTR name_read, LPCSTR name_write1, LPCSTR name_write2);
			~CloneThread();
		private:
			static DWORD WINAPI ThreadProc(LPVOID param);
		};

		class NullThread : public Thread {
		public:
			NullThread(LPCSTR name_read);
			~NullThread();
		private:
			static DWORD WINAPI ThreadProc(LPVOID param);
		};

		class FrameSeparator : public Thread {
		public:
			volatile char *buffer;
			int chunkSize;

			FrameSeparator(LPCSTR name, int chunkSize);
			~FrameSeparator();
			void WaitForData();
			void DataParsed();
		private:
			HANDLE heDataReady;
			HANDLE heDataParsed;
			static DWORD WINAPI ThreadProc(LPVOID param);
		};

	}
}
