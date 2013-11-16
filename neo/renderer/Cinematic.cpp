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

static int readFunction(void* opaque, uint8_t* buf, int buf_size)
{
    Buffer* me = reinterpret_cast<Buffer*>(opaque);
    int to_copy = std::min(me->len - me->curr, buf_size);
    memcpy(buf, me->data + me->curr, to_copy);
    me->curr += to_copy;
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
    bool closed;
    bool looping;

    Buffer file_contents;
    Buffer read_buf;
    AVPacket avpkt;
    AVIOContext* avctx;
    AVFormatContext* ic;
};


// implementation

idCinematicImpl::idCinematicImpl()
    : closed(true), looping(false), ic(NULL)
{ }

idCinematicImpl::~idCinematicImpl( )
{
    Close();
}

bool idCinematicImpl::InitFromFile( const char* qpath, bool looping )
{
    this->looping = looping;

    // load the file in memory
    file_contents.len = fileSystem->ReadFile(qpath, (void**)&file_contents.data);
    if (file_contents.len < 0)
        return false;

    closed = false;

    // init the codec
    av_init_packet(&avpkt);
    ic = avformat_alloc_context();

    // see http://stackoverflow.com/questions/9604633/reading-a-file-located-in-memory-with-libavformat
    read_buf.data = (unsigned char*)av_malloc(4 * 1024);
    read_buf.len = 4 * 1024;
    avctx = avio_alloc_context(read_buf.data, read_buf.len, 0, (void*)&file_contents, readFunction, NULL, NULL);
    ic->pb = avctx;
    if (avformat_open_input(&ic, "", NULL, NULL) < 0)
        return false;
    if (avformat_find_stream_info(ic, NULL) < 0)
        return false;
    if (ic->nb_streams != 1)
        return false;
}

int idCinematicImpl::AnimationLength()
{
    return 0;
}

int idCinematicImpl::GetStartTime()
{
    return -1;
}

void idCinematicImpl::ResetTime( int milliseconds )
{
}

cinData_t idCinematicImpl::ImageForTime( int milliseconds )
{
    cinData_t c;
    memset( &c, 0, sizeof( c ) );
    return c;
}

void idCinematicImpl::ExportToTGA( bool skipExisting )
{
}

float idCinematicImpl::GetFrameRate() const
{
    return 30.0f;
}

void idCinematicImpl::Close()
{
    if (closed) return;

    // cleanup the file
    fileSystem->FreeFile(file_contents.data);
    file_contents.data = NULL;
    file_contents.len = 0;

    // cleanup ffmpeg stuff
    avformat_free_context(ic);
    av_free(avctx);
    av_free(read_buf.data);

    closed = true;
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
