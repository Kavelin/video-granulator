
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "frame.h"


void cleanup_frames(FrameRGB *frames, int frame_count) {
    for (int i = 0; i < frame_count; ++i) {
        if (frames[i].pixels) {
            av_free(frames[i].pixels);
            frames[i].pixels = NULL;
        }
    }
    free(frames);
}


int buffer(char* filename, FrameRGB **frames, int *frame_count, int *width, int *height, double *fps) {

    av_log_set_level(AV_LOG_ERROR);

    avformat_network_init();
    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open input: %s\n", filename);
        return 2;
    }
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream info\n");
        avformat_close_input(&fmt_ctx);
        return 3;
    }

    int video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        fprintf(stderr, "No video stream found\n");
        avformat_close_input(&fmt_ctx);
        return 4;
    }

    AVStream *video_stream = fmt_ctx->streams[video_stream_index];
    AVCodecParameters *codecpar = video_stream->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Decoder not found\n");
        avformat_close_input(&fmt_ctx);
        return 5;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "Failed to alloc codec context\n");
        avformat_close_input(&fmt_ctx);
        return 6;
    }
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        fprintf(stderr, "Failed to copy codec params\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 7;
    }
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "Failed to open codec\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 8;
    }

    *width  = codec_ctx->width;
    *height = codec_ctx->height;
    AVRational time_base = video_stream->time_base;
    AVRational framerate = av_guess_frame_rate(fmt_ctx, video_stream, NULL);
    *fps = framerate.num && framerate.den ? (double)framerate.num / framerate.den : 30.0;
    int64_t total_frames_est = video_stream->nb_frames > 0 ? video_stream->nb_frames : -1;

    //fprintf(stderr, "Video: %s  %dx%d  fps=%.3f  frames(est)=%" PRId64 "\n", filename, width, height, fps, total_frames_est);

    // Prepare scaling context to RGB24
    struct SwsContext *sws_ctx = sws_getContext(
        *width, *height, codec_ctx->pix_fmt,
        *width, *height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL
    );
    if (!sws_ctx) {
        fprintf(stderr, "Could not create sws context\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 9;
    }

    // Decode all frames into an array
    *frames = NULL;
    *frame_count = 0;
    int frames_capacity = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frame = av_frame_alloc();
    AVFrame  *rgb_frame = av_frame_alloc();
    int rgb_stride = av_image_get_linesize(AV_PIX_FMT_RGB24, *width, 0);
    int rgb_bufsize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, *width, *height, 1);

    uint8_t *rgb_buffer = av_malloc(rgb_bufsize);
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer, AV_PIX_FMT_RGB24, *width, *height, 1);

    // Read packets and decode
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    // scale to RGB (writes into rgb_frame->data)
                    sws_scale(sws_ctx,
                              (const uint8_t * const*)frame->data, frame->linesize,
                              0, *height,
                              rgb_frame->data, rgb_frame->linesize);

                    // copy RGB buffer into own heap buffer per frame
                    if (*frame_count >= frames_capacity) {
                        int newcap = frames_capacity == 0 ? 1024 : frames_capacity * 2;
                        FrameRGB *tmp = realloc(*frames, newcap * sizeof(FrameRGB));
                        if (!tmp) {
                            fprintf(stderr, "Out of memory while reallocating frames\n");
                            cleanup_frames(*frames, *frame_count);
                            av_frame_free(&frame);
                            av_frame_free(&rgb_frame);
                            av_free(rgb_buffer);
                            av_packet_free(&pkt);
                            sws_freeContext(sws_ctx);
                            avcodec_free_context(&codec_ctx);
                            avformat_close_input(&fmt_ctx);
                            return 10;
                        }
                        *frames = tmp;
                        // initialize new slots
                        for (int i = frames_capacity; i < newcap; ++i) {
                            (*frames)[i].pixels = NULL;
                            (*frames)[i].linesize = 0;
                        }
                        frames_capacity = newcap;
                    }

                    // allocate and copy
                    uint8_t *p = av_malloc(rgb_bufsize);
                    if (!p) {
                        fprintf(stderr, "OOM allocating frame buffer\n");
                        cleanup_frames(*frames, *frame_count);
                        av_frame_free(&frame);
                        av_frame_free(&rgb_frame);
                        av_free(rgb_buffer);
                        av_packet_free(&pkt);
                        sws_freeContext(sws_ctx);
                        avcodec_free_context(&codec_ctx);
                        avformat_close_input(&fmt_ctx);
                        return 11;
                    }
                    memcpy(p, rgb_frame->data[0], rgb_bufsize);
                    (*frames)[*frame_count].pixels = p;
                    (*frames)[*frame_count].linesize = rgb_frame->linesize[0];
                    (*frame_count)++;
                }
            }
        }
        av_packet_unref(pkt);
    }

    // flush decoder
    avcodec_send_packet(codec_ctx, NULL);
    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
        sws_scale(sws_ctx,
                  (const uint8_t * const*)frame->data, frame->linesize,
                  0, *height,
                  rgb_frame->data, rgb_frame->linesize);

        if (*frame_count >= frames_capacity) {
            int newcap = frames_capacity == 0 ? 1024 : frames_capacity * 2;
            FrameRGB *tmp = realloc(*frames, newcap * sizeof(FrameRGB));
            if (!tmp) {
                fprintf(stderr,"OOM\n");
                cleanup_frames(*frames, *frame_count); 
                return 12;
            }
            *frames = tmp;
            for (int i = frames_capacity; i < newcap; ++i) { 
                (*frames)[i].pixels = NULL; 
                (*frames)[i].linesize = 0; 
            }
            frames_capacity = newcap;
        }

        uint8_t *p = av_malloc(rgb_bufsize);
        if (!p) {
            fprintf(stderr, "OOM allocating frame buffer\n");
            cleanup_frames(*frames, *frame_count);
            av_frame_free(&frame);
            av_frame_free(&rgb_frame);
            av_free(rgb_buffer);
            av_packet_free(&pkt);
            sws_freeContext(sws_ctx);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            return 11;
        }
        memcpy(p, rgb_frame->data[0], rgb_bufsize);
        (*frames)[*frame_count].pixels = p;
        (*frames)[*frame_count].linesize = rgb_frame->linesize[0];
        (*frame_count)++;
    }

    // free ffmpeg decode temps
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    av_free(rgb_buffer);
    av_packet_free(&pkt);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    if (*frame_count == 0) {
        fprintf(stderr, "No frames decoded.\n");
        cleanup_frames(*frames, *frame_count);
        return 13;
    }

    fprintf(stderr, "Decoded %d frames into memory. Starting playback.\n", *frame_count);
    return 0;
}

