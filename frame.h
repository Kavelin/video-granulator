#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

typedef struct {
    uint8_t *pixels;
    int linesize;
} FrameRGB;

#endif // FRAME_H