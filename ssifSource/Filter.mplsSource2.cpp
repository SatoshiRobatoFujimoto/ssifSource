//
// BD3D video decoding tool
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


#include "stdafx.h"
#include "Filter.mplsSource2.hpp"
#include "mplsReader.h"

#include <common.h>
#include <Tools.WinApi.hpp>

#define FILTER_NAME "mplsSource2"

using namespace Tools::WinApi;

namespace Filter {

	PVideoFrame mplsSource2::GetFrame(int n, IScriptEnvironment* env) {
		return currentClip->GetFrame(n, env);
	}

	mplsSource2::mplsSource2(IScriptEnvironment* env, AVSValue args) :
		proxyLeft(nullptr), proxyRight(nullptr)
	{
		// Note, this->vi will be initialized at the end of constructor

		bool leftView = args[2].AsBool(true);
		bool rightView = args[3].AsBool(true);
		flagMVC = false;

		std::string mplsFilename = args[0].AsString();
		std::string mplsDir = ExtractFileDir(mplsFilename);
		ssifPath = args[1].Defined() ? args[1].AsString() : (mplsDir + "..\\STREAM\\");
		if (IsDirectoryExists((ssifPath + "SSIF").c_str())) {
			flagMVC = true;
			ssifPath += "SSIF\\";
		}

		// parse playlist
		mpls_file_t mpls_file;
		playlist_t playlist_base;
		try {
			mpls_file = init_mpls(&mplsFilename[0]);
			playlist_base = create_playlist_t();
			parse_stream_clips(&mpls_file, &playlist_base);
			print_stream_clips_header(&playlist_base);
			print_stream_clips(&playlist_base);
		}
		catch (std::exception& err) {
			env->ThrowError("%s", err.what());
		}

		std::stringstream muxedFilenamePart;
		std::stringstream optionFileListPart;
		int num_frames = 0;

		try {
			// swap views auto-detection
			flagSwapViews = TEST(mpls_file.data[0x38], 0x10);
			printf("%s: Swap views flag value is %s\n", FILTER_NAME, flagSwapViews ? "true" : "false");

			// prepare demuxer settings
			bool first = true;
			stream_clip_t *clip = playlist_base.stream_clip_list.first;
			while (clip != nullptr) {
				int currentFramecount = static_cast<int>(static_cast<double>(clip->raw_duration) * 24 / (45 * 1001) + 0.5);
				printf("%s: adding file %s with %d frames to sequences list.\n", FILTER_NAME, clip->filename, currentFramecount);

				std::string filename = clip->filename;
				filename.erase(filename.length() - 5);
				if (!first)
					muxedFilenamePart << '+';
				muxedFilenamePart << filename;
				filename = ssifPath + filename + (flagMVC ? ".ssif" : ".m2ts");
				if (!first)
					optionFileListPart << '+';
				optionFileListPart << '"' << filename << '"';

				num_frames += currentFramecount;
				first = false;
				clip = clip->next;
			}
			free_playlist_members(&playlist_base);
			free_mpls_file_members(&mpls_file);
		}
		catch (...) {
			free_playlist_members(&playlist_base);
			free_mpls_file_members(&mpls_file);
			throw;
		}

		std::string metaFilename = GetTempFilename();
		{
			std::ofstream metafile(metaFilename.c_str());
			metafile << "MUXOPT --no-pcr-on-video-pid --new-audio-pes --demux --vbr --vbv-len=500" << std::endl;
			if (leftView)
				metafile << "V_MPEG4/ISO/AVC, " << optionFileListPart.str() << ", track=4113" << std::endl;
			if (rightView)
				metafile << "V_MPEG4/ISO/MVC, " << optionFileListPart.str() << ", track=4114" << std::endl;
		}

		printf("\n%s: creating demuxer process ... ", FILTER_NAME);

		std::string
			leftWriter = uniqueId.MakePipeName(muxedFilenamePart.str() + ".track_4113.264"),
			rightWriter = uniqueId.MakePipeName(muxedFilenamePart.str() + ".track_4114.mvc"),
			leftReader = uniqueId.MakePipeName("tsmuxer_left"),
			rightReader = uniqueId.MakePipeName("tsmuxer_right");

		if (leftView)
			proxyLeft = std::make_unique<ProxyThread>(leftWriter.c_str(), leftReader.c_str());
		if (rightView)
			proxyRight = std::make_unique<ProxyThread>(rightWriter.c_str(), rightReader.c_str());

		process = std::make_unique<ProcessHolder>(
			"tsMuxeR.exe",
			"\"" + metaFilename + "\" \"" + uniqueId.GetPipePath() + "\"",
			args[7].AsBool(false)
		);

		printf("OK\n");

		// ssifSource params
		AVSValue ssArgs[] = {
			AVSValue(),                     // ssif_file
			num_frames,                     // frame_count
			leftView,                       // left_view
			rightView,                      // right_view
			args[4],                        // horizontal_stack
			args[5].AsBool(flagSwapViews),  // swap_views
			args[6],                        // intel_params
			args[7],                        // debug
			args[8],                        // use_ldecod
			leftReader.c_str(),             // avc264
			rightReader.c_str(),            // mvc264
			AVSValue(),                     // muxed264
			args[9],                        // width
			args[10],                       // height
			AVSValue(),                     // stop_after
		};
		currentClip = env->Invoke("ssifSource", AVSValue(ssArgs, sizeof(ssArgs) / sizeof(ssArgs[0]))).AsClip();
		vi = currentClip->GetVideoInfo();

		process->Resume();
	}

	AvsParams mplsSource2::CreateParams =
		"[mpls_file]s"
		"[ssif_path]s"
		"[left_view]b"
		"[right_view]b"
		"[horizontal_stack]b"
		"[swap_views]b"
		"[intel_params]s"
		"[debug]b"
		"[use_ldecod]b"
		"[width]i"
		"[height]i";

	AVSValue mplsSource2::Create(AVSValue args, void* user_data, IScriptEnvironment* env) {
		try {
			return new mplsSource2(env, args);
		}
		catch (std::exception& ex) {
			env->ThrowError("ERROR in %s : %s", FILTER_NAME, ex.what());
		}
		return AVSValue();
	}

}
