/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "precompiled.h"

// ffmpeg includes
extern "C" {
#ifndef INT64_C
#	define INT64_C(c) (c ## LL)
#	define UINT64_C(c) (c ## ULL)
#endif
#ifndef __STDC_CONSTANT_MACROS
#	define __STDC_CONSTANT_MACROS
#endif

#include <libavformat/avformat.h>
} // extern "C"


extern idCVar s_noSound;

#include "tr_local.h"

#define TEST 1

namespace { // anon

// utility

struct Buffer
{
    Buffer()
        : data(NULL), len(0), curr(0)
    { }

    unsigned char* data;
    int len;
    int curr;
};

int readFunction(void* opaque, uint8_t* buf, int buf_size)
{
    Buffer* me = reinterpret_cast<Buffer*>(opaque);
    int to_copy = std::min(me->len - me->curr, buf_size);
    memcpy(buf, me->data + me->curr, to_copy);
    me->curr += to_copy;
    return to_copy;
}

cinData_t getDefaultData()
{
    cinData_t c;
    memset( &c, 0, sizeof( c ) );
    return c;
}


// idCinematic implementation class
class idCinematicImpl : public idCinematic
{
public:
    idCinematicImpl();

    // frees all allocated memory
    virtual ~idCinematicImpl();

    // returns false if it failed to load
    virtual bool InitFromFile( const char* qpath, bool looping );

    // returns the length of the animation in milliseconds
    virtual int	AnimationLength();

    // the pointers in cinData_t will remain valid until the next UpdateForTime() call
    virtual cinData_t ImageForTime( int milliseconds );

    // closes the file and frees all allocated memory
    virtual void Close();

    // sets the cinematic to start at that time (can be in the past)
    virtual void ResetTime( int time );

    // gets the time the cinematic started
    virtual int GetStartTime();

    virtual void ExportToTGA( bool skipExisting = true );

    virtual float GetFrameRate() const;

private:
    bool ReadFile(const char* qpath);
    bool InitFfmpeg();
    void InitImages();
    bool DecodeFrame();
    void SaveToImage(idImage& img, uint8_t* data,
                     int linesize, int width, int height);

    bool closed;
    bool good;
    bool looping;
    unsigned int frame_count;
    int start_time;

    Buffer file_contents;
    Buffer read_buf;
    AVPacket avpkt;
    AVIOContext* avio;
    AVFormatContext* ic;
    AVCodecContext* avctx;
    AVFrame* frame;

    idImage yImg;
    idImage crImg;
    idImage cbImg;
    cinData_t imgData;
};


// implementation

idCinematicImpl::idCinematicImpl()
    : closed(true), good(false), looping(false)
    , frame_count(0), start_time(0)
    , avio(NULL), ic(NULL), avctx(NULL), frame(NULL)
    , yImg("yimg"), crImg("crimg"), cbImg("cbImg")
{
    imgData.imageWidth = 0;
    imgData.imageHeight = 0;
    imgData.imageY = &yImg;
    imgData.imageCr = &crImg;
    imgData.imageCb = &cbImg;
    imgData.status = FMV_EOF;
}

idCinematicImpl::~idCinematicImpl( )
{
    Close();
}

bool idCinematicImpl::InitFromFile( const char* qpath, bool looping )
{
    this->looping = looping;
#if !TEST
    if (!ReadFile(qpath))
        return false;
#endif

    closed = false;
    if (!InitFfmpeg())
        return false;

    InitImages();

    frame_count = 0;
    good = true;
    return true;
}

int idCinematicImpl::AnimationLength()
{
    // FIXME: what is the expected value here?
    if (!ic || !good)
        return 0;
    int duration = ic->duration / 1000;
    return duration;
}

int idCinematicImpl::GetStartTime()
{
    return start_time;
}

void idCinematicImpl::ResetTime( int milliseconds )
{
    start_time = milliseconds;
}

cinData_t idCinematicImpl::ImageForTime( int milliseconds )
{
    int err = av_read_frame(ic, &avpkt);
    if (err < 0 || avpkt.size == 0)
        return getDefaultData();

    while (avpkt.size > 0)
        if (!DecodeFrame())
            return getDefaultData();

    return imgData;
}

void idCinematicImpl::ExportToTGA( bool skipExisting )
{
}

float idCinematicImpl::GetFrameRate() const
{
    if (!good || !ic || !ic->streams)
        return 0.0f;

    AVStream* video_st = ic->streams[0];
    AVRational fr = video_st->avg_frame_rate;
    return fr.num / (float)fr.den;
}

void idCinematicImpl::Close()
{
    if (closed) return;

    // cleanup the file
    if (file_contents.data)
    {
        fileSystem->FreeFile(file_contents.data);
        file_contents.data = NULL;
        file_contents.len = 0;
    }

    // cleanup ffmpeg stuff
    // TODO
    // see: https://lists.ffmpeg.org/pipermail/libav-user/2012-December/003257.html
    // and: http://stackoverflow.com/questions/9604633/reading-a-file-located-in-memory-with-libavformat
    if (ic)
    {
        avformat_free_context(ic);
        ic = NULL;
    }
    if (avio)
    {
        av_free(avio);
        avio = NULL;
        read_buf.data = NULL;
        read_buf.len = 0;
    }
    if (frame)
    {
        avcodec_free_frame(&frame);
        frame = NULL;
    }

    closed = true;
    good = false;
}


bool idCinematicImpl::ReadFile(const char* qpath)
{
    // load the file in memory
    file_contents.len = fileSystem->ReadFile(qpath, (void**)&file_contents.data);
    return file_contents.len >= 0;
}

bool idCinematicImpl::InitFfmpeg()
{
    // init the codec
    av_init_packet(&avpkt);
    ic = avformat_alloc_context();

#if !TEST
    // see http://stackoverflow.com/questions/9604633/reading-a-file-located-in-memory-with-libavformat
    const int BUFSIZE = 32768;
    read_buf.data = (unsigned char*)av_malloc(BUFSIZE + FF_INPUT_BUFFER_PADDING_SIZE);
    read_buf.len = BUFSIZE;
    avio = avio_alloc_context(read_buf.data, read_buf.len, 0, (void*)&file_contents, readFunction, NULL, NULL);
    ic->pb = avio;

    if (avformat_open_input(&ic, "", NULL, NULL) < 0)
        return false;
#else
    if (avformat_open_input(&ic, "/opt/doom3-bfg/base/video/erebusteam.bik", NULL, NULL) < 0)
        return false;
#endif

    if (avformat_find_stream_info(ic, NULL) < 0)
        return false;
    if (ic->nb_streams < 1)
        return false;

    // TODO: Handle audio video (so multiple streams)

    AVStream* video_st = ic->streams[0];
    avctx = video_st->codec;
    AVCodec* codec = avcodec_find_decoder(avctx->codec_id);
    avctx->codec_id = codec->id;

    if (codec->capabilities & CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;

    frame = avcodec_alloc_frame();
    if (!frame)
        return false;
    avcodec_get_frame_defaults(frame);
    av_free_packet(&avpkt);

    if (avcodec_open2(avctx, codec, NULL) < 0)
        return false;

    return true;
}

void idCinematicImpl::InitImages()
{
    imgData.status = FMV_IDLE;
}

bool idCinematicImpl::DecodeFrame()
{
    int len, got_frame;
    char buf[1024];

    len = avcodec_decode_video2(avctx, frame, &got_frame, &avpkt);
    if (len < 0)
    {
        // DEBUG
        char msg[1024];
        av_strerror(len, msg, 1024);
        // ----
        return false;
    }

    if (got_frame)
    {
        // YUV420p in AVFrame
        // http://lists.libav.org/pipermail/libav-user/2008-June/000632.html
        int width = avctx->width, height = avctx->height;
        int half_width = width / 2, half_height = height / 2;
        SaveToImage(yImg, frame->data[0], frame->linesize[0], width, height);
        SaveToImage(yImg, frame->data[1], frame->linesize[1], half_width, half_height);
        SaveToImage(yImg, frame->data[2], frame->linesize[2], half_width, half_height);
    }
    if (avpkt.data)
    {
        avpkt.size -= len;
        avpkt.data += len;
    }
    return true;
}

void idCinematicImpl::
SaveToImage(idImage& img, uint8_t* data,
            int linesize, int width, int height)
{
    img.GenerateImage(data, width, height, TF_LINEAR, TR_CLAMP, TD_LIGHT);
}

} // anon namespace




//===========================================

/*
==============
idCinematic::InitCinematic
==============
*/
void idCinematic::InitCinematic( void )
{
    av_register_all();
}

/*
==============
idCinematic::ShutdownCinematic
==============
*/
void idCinematic::ShutdownCinematic( void )
{
}

/*
==============
idCinematic::Alloc
==============
*/
idCinematic* idCinematic::Alloc()
{
    return new idCinematicImpl;
    //return new idCinematic;
}

/*
==============
idCinematic::~idCinematic
==============
*/
idCinematic::~idCinematic( )
{
    Close();
}

/*
==============
idCinematic::InitFromFile
==============
*/
bool idCinematic::InitFromFile( const char* qpath, bool looping )
{
    return false;
}

/*
==============
idCinematic::AnimationLength
==============
*/
int idCinematic::AnimationLength()
{
    return 0;
}

/*
==============
idCinematic::GetStartTime
==============
*/
int idCinematic::GetStartTime()
{
    return -1;
}

/*
==============
idCinematic::ResetTime
==============
*/
void idCinematic::ResetTime( int milliseconds )
{
}

/*
==============
idCinematic::ImageForTime
==============
*/
cinData_t idCinematic::ImageForTime( int milliseconds )
{
    cinData_t c;
    memset( &c, 0, sizeof( c ) );
    return c;
}

/*
==============
idCinematic::ExportToTGA
==============
*/
void idCinematic::ExportToTGA( bool skipExisting )
{
}

/*
==============
idCinematic::GetFrameRate
==============
*/
float idCinematic::GetFrameRate() const
{
    return 30.0f;
}

/*
==============
idCinematic::Close
==============
*/
void idCinematic::Close()
{
}

/*
==============
idSndWindow::InitFromFile
==============
*/
bool idSndWindow::InitFromFile( const char* qpath, bool looping )
{
    idStr fname = qpath;

    fname.ToLower();
    if( !fname.Icmp( "waveform" ) )
    {
        showWaveform = true;
    }
    else
    {
        showWaveform = false;
    }
    return true;
}

/*
==============
idSndWindow::ImageForTime
==============
*/
cinData_t idSndWindow::ImageForTime( int milliseconds )
{
    return soundSystem->ImageForTime( milliseconds, showWaveform );
}

/*
==============
idSndWindow::AnimationLength
==============
*/
int idSndWindow::AnimationLength()
{
    return -1;
}
