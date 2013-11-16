/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavcodec API use example.
 *
 * Note that libavcodec only handles codecs (mpeg, mpeg4, etc...),
 * not file formats (avi, vob, mp4, mov, mkv, mxf, flv, mpegts, mpegps, etc...). See library 'libavformat' for the
 * format handling
 * @example doc/examples/decoding_encoding.c
 */

#include <math.h>

#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavformat/avformat.h>

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480


/*
 * Video decoding example
 */

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename)
{
    FILE *f;
    int i;

    f=fopen(filename,"w");
    fprintf(f,"P5\n%d %d\n%d\n",xsize,ysize,255);
    for(i=0;i<ysize;i++)
        fwrite(buf + i * wrap,1,xsize,f);
    fclose(f);
}

static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,
                              AVFrame *frame, int *frame_count, AVPacket *pkt, int last)
{
    int len, got_frame;
    char buf[1024];

    len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
    if (len < 0) {
        fprintf(stderr, "Error while decoding frame %d\n", *frame_count);
        return len;
    }
    if (got_frame) {
        printf("Saving %sframe %3d\n", last ? "last " : "", *frame_count);
        fflush(stdout);

        /* the picture is allocated by the decoder, no need to free it */
        snprintf(buf, sizeof(buf), outfilename, *frame_count);
        pgm_save(frame->data[0], frame->linesize[0],
                 avctx->width, avctx->height, buf);
        (*frame_count)++;
    }
    if (pkt->data) {
        pkt->size -= len;
        pkt->data += len;
    }
    return 0;
}

static int my_video_decode(const char* outfilename, const char* filename)
{
    int err, ret, frame_count;
    AVFormatContext *ic = NULL;
    AVInputFormat *iformat = NULL;
    AVCodec *codec;
    AVCodecContext *avctx = NULL;
    AVFrame *frame = av_frame_alloc();
    AVPacket avpkt;
    AVStream* video_st;

    av_init_packet(&avpkt);

    // read_loop

    ic = avformat_alloc_context();
    err = avformat_open_input(&ic, filename, iformat, NULL);
    if (err < 0) {
        ret = -1;
        goto fail;
    }
    err = avformat_find_stream_info(ic, NULL);
    if (err < 0) {
        ret = -1;
        goto fail;
    }

    // stream_component_open
    if (ic->nb_streams != 1) {
      ret = -1;
      goto fail;
    }
    video_st = ic->streams[0];
    avctx = video_st->codec;
    codec = avcodec_find_decoder(avctx->codec_id);
    avctx->codec_id = codec->id;

    // EXPERIMENT WITH FAST
    // avctx->flags2 |= CODEC_FLAG2_FAST;

    if (codec->capabilities & CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;

    err = avcodec_open2(avctx, codec, NULL);
    if (err < 0) {
        ret = -1;
        goto fail;
    }
    
    // video thread
    avcodec_get_frame_defaults(frame);
    av_free_packet(&avpkt);
    
    
    // video decode example
    if (avcodec_open2(avctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    frame = avcodec_alloc_frame();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    frame_count = 0;
    for(;;) {
        err = av_read_frame(ic, &avpkt);
        if (avpkt.size == 0)
            break;

        /* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
           and this is the only method to use them because you cannot
           know the compressed data size before analysing it.

           BUT some other codecs (msmpeg4, mpeg4) are inherently frame
           based, so you must call them with all the data for one
           frame exactly. You must also initialize 'width' and
           'height' before initializing them. */

        /* NOTE2: some codecs allow the raw parameters (frame size,
           sample rate) to be changed at any frame. We handle this, so
           you should also take care of it */

        /* here, we use a stream based decoder (mpeg1video), so we
           feed decoder and see if it could decode a frame */
        while (avpkt.size > 0)
            if (decode_write_frame(outfilename, avctx, frame, &frame_count, &avpkt, 0) < 0)
                exit(1);
    }

    /* some codecs, such as MPEG, transmit the I and P frame with a
       latency of one frame. You must do the following to have a
       chance to get the last frame of the video */
    avpkt.data = NULL;
    avpkt.size = 0;
    decode_write_frame(outfilename, avctx, frame, &frame_count, &avpkt, 1);

    avcodec_close(avctx);
    av_free(avctx);
    avcodec_free_frame(&frame);
    printf("\n");

fail:
    return ret;
}


int main(int argc, char **argv)
{
    const char *input;

    /* register all the codecs */
    av_register_all();

    if (argc < 2) {
        printf("usage: %s input.bik\n", argv[0]);
        return 1;
    }
    input = argv[1];
    my_video_decode("test.pgm", input);

    return 0;
}
