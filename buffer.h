#ifndef BUFFER_H
#define BUFFER_H

#include "frame.h"

void cleanup_frames(FrameRGB* frames, int frame_count);

int buffer(char* filename, FrameRGB** frames, int* frame_count, int* width, int* height, double* fps);

#endif // BUFFER_H