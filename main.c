// main.c
// Simple video "granulator" / glitch player using FFmpeg + SDL2.
// Decodes the whole video into memory (RGB24) then plays random grains.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

typedef struct {
    uint8_t *pixels;     // RGB24 buffer (width * height * 3)
    int linesize;        // width * 3
} FrameRGB;

static void cleanup_frames(FrameRGB *frames, int n) {
    for (int i = 0; i < n; ++i) {
        if (frames[i].pixels) {
            av_free(frames[i].pixels);
            frames[i].pixels = NULL;
        }
    }
    free(frames);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input-video\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    // Granulator params (tweak these)
    const int GRAIN_FRAMES = 8;        // length of each grain (frames)
    const int LOOP_FOREVER = 1;        // 1 = keep playing grains forever, 0 = stop after showing each frame once
    //const int SPRAY = 20;
    const int RANDOM_SEED = (int)time(NULL);

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

    int width  = codec_ctx->width;
    int height = codec_ctx->height;
    AVRational time_base = video_stream->time_base;
    AVRational framerate = av_guess_frame_rate(fmt_ctx, video_stream, NULL);
    double fps = framerate.num && framerate.den ? (double)framerate.num / framerate.den : 30.0;
    int64_t total_frames_est = video_stream->nb_frames > 0 ? video_stream->nb_frames : -1;

    fprintf(stderr, "Video: %s  %dx%d  fps=%.3f  frames(est)=%" PRId64 "\n",
            filename, width, height, fps, total_frames_est);

    // Prepare scaling context to RGB24
    struct SwsContext *sws_ctx = sws_getContext(
        width, height, codec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL
    );
    if (!sws_ctx) {
        fprintf(stderr, "Could not create sws context\n");
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return 9;
    }

    // Decode all frames into an array
    FrameRGB *frames = NULL;
    int frames_capacity = 0;
    int frames_count = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frame = av_frame_alloc();
    AVFrame  *rgb_frame = av_frame_alloc();
    int rgb_stride = av_image_get_linesize(AV_PIX_FMT_RGB24, width, 0);
    int rgb_bufsize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);

    uint8_t *rgb_buffer = av_malloc(rgb_bufsize);
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer, AV_PIX_FMT_RGB24, width, height, 1);

    // Read packets and decode
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    // scale to RGB (writes into rgb_frame->data)
                    sws_scale(sws_ctx,
                              (const uint8_t * const*)frame->data, frame->linesize,
                              0, height,
                              rgb_frame->data, rgb_frame->linesize);

                    // copy RGB buffer into own heap buffer per frame
                    if (frames_count >= frames_capacity) {
                        int newcap = frames_capacity == 0 ? 1024 : frames_capacity * 2;
                        FrameRGB *tmp = realloc(frames, newcap * sizeof(FrameRGB));
                        if (!tmp) {
                            fprintf(stderr, "OOM while realloc frames\n");
                            cleanup_frames(frames, frames_count);
                            av_frame_free(&frame);
                            av_frame_free(&rgb_frame);
                            av_free(rgb_buffer);
                            av_packet_free(&pkt);
                            sws_freeContext(sws_ctx);
                            avcodec_free_context(&codec_ctx);
                            avformat_close_input(&fmt_ctx);
                            return 10;
                        }
                        frames = tmp;
                        // initialize new slots
                        for (int i = frames_capacity; i < newcap; ++i) {
                            frames[i].pixels = NULL;
                            frames[i].linesize = 0;
                        }
                        frames_capacity = newcap;
                    }

                    // allocate and copy
                    uint8_t *p = av_malloc(rgb_bufsize);
                    if (!p) {
                        fprintf(stderr, "OOM allocating frame buffer\n");
                        cleanup_frames(frames, frames_count);
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
                    frames[frames_count].pixels = p;
                    frames[frames_count].linesize = rgb_frame->linesize[0];
                    frames_count++;
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
                  0, height,
                  rgb_frame->data, rgb_frame->linesize);

        if (frames_count >= frames_capacity) {
            int newcap = frames_capacity == 0 ? 1024 : frames_capacity * 2;
            FrameRGB *tmp = realloc(frames, newcap * sizeof(FrameRGB));
            if (!tmp) { fprintf(stderr,"OOM\n"); cleanup_frames(frames, frames_count); return 12; }
            frames = tmp;
            for (int i = frames_capacity; i < newcap; ++i) { frames[i].pixels = NULL; frames[i].linesize = 0; }
            frames_capacity = newcap;
        }

        uint8_t *p = av_malloc(rgb_bufsize);
        memcpy(p, rgb_frame->data[0], rgb_bufsize);
        frames[frames_count].pixels = p;
        frames[frames_count].linesize = rgb_frame->linesize[0];
        frames_count++;
    }

    // free ffmpeg decode temps
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    av_free(rgb_buffer);
    av_packet_free(&pkt);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    if (frames_count == 0) {
        fprintf(stderr, "No frames decoded.\n");
        cleanup_frames(frames, frames_count);
        return 13;
    }

    fprintf(stderr, "Decoded %d frames into memory. Starting playback.\n", frames_count);

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        cleanup_frames(frames, frames_count);
        return 14;
    }

    SDL_Window *win = SDL_CreateWindow("Video Granulator",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        cleanup_frames(frames, frames_count);
        return 15;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        cleanup_frames(frames, frames_count);
        return 16;
    }

    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!tex) {
        fprintf(stderr, "SDL_CreateTexture error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(win);
        SDL_Quit();
        cleanup_frames(frames, frames_count);
        return 17;
    }

    srand(RANDOM_SEED);

    int quit = 0;
    SDL_Event ev;
    const Uint32 frame_delay_ms = (Uint32)(1000.0 / fps + 0.5);

    while (!quit) {
        // choose a random start index such that we have GRAIN_FRAMES contiguous frames
        int max_start = frames_count - GRAIN_FRAMES;
        if (max_start <= 0) { // if video shorter than grain size, just play sequentially
            // play sequentially once
            for (int i = 0; i < frames_count && !quit; ++i) {
                // handle events
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) quit = 1;
                }
                SDL_UpdateTexture(tex, NULL, frames[i].pixels, frames[i].linesize);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, tex, NULL, NULL);
                SDL_RenderPresent(renderer);
                SDL_Delay(frame_delay_ms);
            }
            if (!LOOP_FOREVER) break;
            else continue;
        }

        int start = rand() % (max_start + 1);

        for (int g = 0; g < GRAIN_FRAMES && !quit; ++g) {
            int idx = start + g;
            // event handling each frame
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) quit = 1;
            }

            // update texture with frame
            SDL_UpdateTexture(tex, NULL, frames[idx].pixels, frames[idx].linesize);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, tex, NULL, NULL);
            SDL_RenderPresent(renderer);

            SDL_Delay(frame_delay_ms);
        }

        if (!LOOP_FOREVER) break;
    }

    // cleanup
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();

    cleanup_frames(frames, frames_count);
    fprintf(stderr, "Exited cleanly.\n");
    return 0;
}
