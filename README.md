(Granular synthesis)[https://en.wikipedia.org/wiki/Granular_synthesis] but for videos


made to learn video manipulation using c!



Run on Linux
```bash
>>>gcc main.c buffer.c window.c -o build/vidgrains $(pkg-config --cflags --libs sdl2 libavformat libavcodec libswscale libavutil)
>>>./build/vidgrains in/put/file.mp4
```
