// Renders each theme's sky and parallax layers as one strip, to judge them on their own.
//   bgshot <out.ppm>
#include "../../src/common.h"
#include <stdio.h>
#include <stdlib.h>
namespace sr { bool snd_start(kv::Engine&) { return false; } void snd_stop(kv::Engine&) {} void snd_music(int) {} void snd_sfx(int) {} void snd_engine(bool, f32) {} void snd_final_lap(bool) {} }
using namespace sr;
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const int W = 1024, H = HORIZON + 1, N = k_track_count;
    u32* img = (u32*)malloc((size_t)W * H * N * 4);
    for (int ti = 0; ti < N; ++ti) {
        static Track t;
        track_build(t, k_tracks[ti], mem_general());
        const Theme& th = *t.theme;
        u32* base = img + (size_t)ti * W * H;
        for (int y = 0; y < H; ++y) {
            f32 k = (f32)(y / 6 * 6) / (f32)HORIZON;
            u32 c = blend(th.sky_top, th.sky_bottom, k);
            for (int x = 0; x < W; ++x) base[y * W + x] = c;
        }
        if (th.stars) for (int i = 0; i < 70; ++i) { int sx = (int)(hash2(i, 5) % 1024), sy = (int)(hash2(i, 9) % (u32)(HORIZON - 20)); base[sy * W + sx] = (i % 7 == 0) ? rgb(248, 200, 255) : rgb(200, 200, 232); }
        for (int ly = 0; ly < 64; ++ly) {
            int y = HORIZON + 1 - 64 + ly;
            for (int x = 0; x < W; ++x) {
                u32 c = t.near_layer[(u32)(ly * 1024 + x)];
                if (!c) c = t.far_layer[(u32)(ly * 512 + (x & 511))];
                if (c) base[y * W + x] = c;
            }
        }
        track_free(t);
    }
    FILE* f = fopen(argv[1], "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H * N);
    for (int i = 0; i < W * H * N; ++i) { unsigned char c[3] = {(unsigned char)(img[i] & 255), (unsigned char)((img[i] >> 8) & 255), (unsigned char)((img[i] >> 16) & 255)}; fwrite(c, 1, 3, f); }
    fclose(f);
    return 0;
}
