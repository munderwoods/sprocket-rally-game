// A headless harness for Sprocket Rally: builds the game's own track, race and render code
// without the engine's window or audio, runs races flat out, and writes frames and
// top-down maps as PPM files. For checking layouts, balance and visuals without a window.
//
//   headless map <track> <out.ppm>                 top-down map with scenery, items, paths
//   headless race <track> <char> <seconds> <every> <prefix> [human]
//                                                    autopilot race; a frame every <every> s
#include "../../src/common.h"
#include "../../src/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sr {
bool snd_start(kv::Engine&) { return false; }
void snd_stop(kv::Engine&) {}
void snd_music(int) {}
void snd_sfx(int) {}
void snd_engine(bool, f32) {}
void snd_final_lap(bool) {}
}
using namespace sr;

static void write_ppm(const char* path, const u32* px, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) { u8 c[3] = {(u8)(px[i] & 255), (u8)((px[i] >> 8) & 255), (u8)((px[i] >> 16) & 255)}; fwrite(c, 1, 3, f); }
    fclose(f);
}

static GenTrack g_gen;
static const TrackDef& pick(int t) {
    if (t < k_track_count) return k_tracks[t];
    return track_generate((u32)(t - k_track_count + 1), g_gen);
}

int main(int argc, char** argv) {
    { // the sprites live in a file now: load them before anything is drawn
        char err[700];
        const char* art = getenv("SPROCKET_ART") ? getenv("SPROCKET_ART") : ART_BIN;
        if (!art_load(art, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
    }
    if (argc < 2) { fprintf(stderr, "usage: see headless.cc\n"); return 2; }
    Allocator* a = mem_general();
    if (!strcmp(argv[1], "map") && argc >= 4) {
        static Race r;
        race_init(r, pick(atoi(argv[2])), 0, true, a);
        const Track& t = r.track;
        u32* img = (u32*)malloc((size_t)MAP * MAP * 4);
        for (int i = 0; i < MAP * MAP; ++i) img[i] = t.theme->pal[t.color[(u32)i]];
        auto dot = [&](vec2 p, int rad, u32 c) {
            for (int y = (int)p.y - rad; y <= (int)p.y + rad; ++y)
                for (int x = (int)p.x - rad; x <= (int)p.x + rad; ++x)
                    if ((unsigned)x < MAP && (unsigned)y < MAP) img[y * MAP + x] = c;
        };
        for (const Prop& p : t.props) dot(p.pos, p.solid ? 2 : 1, p.solid ? rgb(255, 0, 255) : rgb(0, 70, 0));
        for (const ItemBox& b : t.boxes) dot(b.pos, 2, rgb(0, 255, 255));
        for (const Bolt& b : t.bolts) dot(b.pos, 1, rgb(255, 255, 0));
        for (int i = 0; i < RACERS; ++i) dot(r.racers[i].pos, 2, k_chars[r.racers[i].ch].color[0]);
        // direction arrows along the line every ~10% and the lap fraction as tick marks
        for (int k = 0; k < 20; ++k) {
            vec2 tan; vec2 p = track_point(t, t.length * (f32)k / 20.0f, &tan);
            for (int j = 0; j < 10; ++j) dot(p + tan * (f32)j, 0, k % 2 ? rgb(255, 255, 255) : rgb(255, 64, 64));
        }
        write_ppm(argv[3], img, MAP, MAP);
        printf("%s: length %.0f, %u props, %u boxes, %u bolts, theme %s\n", t.def->name, t.length, t.props.len, t.boxes.len, t.bolts.len, t.theme->name);
        return 0;
    }
    if (!strcmp(argv[1], "race") && argc >= 7) {
        static Race r;
        int track = atoi(argv[2]), ch = atoi(argv[3]);
        f32 seconds = (f32)atof(argv[4]), every = (f32)atof(argv[5]);
        const char* split = getenv("SPLIT"); // v or h: a two-player race, drawn split
        int nplayers = split ? (getenv("PLAYERS") ? atoi(getenv("PLAYERS")) : 2) : 1;
        int chars[4] = {ch, (ch + 3) % RACERS, (ch + 5) % RACERS, (ch + 6) % RACERS};
        race_init(r, pick(track), ch, false, a, nplayers, chars);
        r.autopilot = true;
        // TT=1: a time trial. A first run by another racer is recorded, then this one races its ghost.
        static GhostSample g_rec[60 * 300], g_best[60 * 300];
        if (getenv("TT")) {
            race_init(r, pick(track), (ch + 1) % RACERS, false, a);
            race_time_trial(r, g_best, 60 * 300, nullptr, 0, 0);
            r.autopilot = true;
            for (int i = 0; i < 60 * 300 && r.phase != RacePhase::Done; ++i) race_tick(r, Controls{}, 1.0f / 60.0f);
            int n = r.rec_n;
            printf("ghost run: %d samples, %.2fs\n", n, r.racers[r.player].finish_time);
            race_init(r, pick(track), ch, false, a);
            race_time_trial(r, g_rec, 60 * 300, g_best, n, (ch + 1) % RACERS);
            r.autopilot = true;
        }
        if (const char* d = getenv("DIFF")) r.cpu_level = (f32)atof(d); // 0 easy, 1 normal, 2 hard
        static u32 scratch[FB_MAXW * FB_H];
        static u32 fb[FB_MAXW * FB_H];
        Frame f; f.px = fb; f.w = argc > 7 && !strcmp(argv[7], "wide") ? FB_W169 : FB_W43; f.h = FB_H;
        f32 next = every;
        int shot = 0;
        const f32 dt = 1.0f / 60.0f;
        int last_lap[RACERS] = {};
        f32 lap_start[RACERS] = {};
        for (f32 t = 0; t < seconds; t += dt) {
            race_tick(r, Controls{}, dt);
            static int falls_seen[RACERS];
            for (int i = 0; i < RACERS; ++i) {
                const Racer& k = r.racers[i];
                if (k.stat_falls != falls_seen[i]) {
                    falls_seen[i] = k.stat_falls;
                    if (getenv("FALLS")) printf("  fall: %-14s t=%.1f pos=(%.0f,%.0f) s=%.0f/%.0f safe=%.0f speed=%.0f air=%d\n", k_chars[k.ch].name, r.race_time, k.pos.x, k.pos.y, k.s, r.track.length, k.last_safe_s, length(k.vel), (int)k.ramp_air);
                }
                if (k.lap != last_lap[i]) {
                    if (last_lap[i] >= 1) printf("  %-15s lap %d: %.2fs\n", k_chars[k.ch].name, last_lap[i], r.race_time - lap_start[i]);
                    lap_start[i] = r.race_time; last_lap[i] = k.lap;
                }
            }
            if (every > 0 && t >= next) {
                next += every;
                draw_race_screen(f, r, (u32)(t * 60.0f), nullptr, split && split[0] == 'h', scratch);
                char path[256];
                snprintf(path, sizeof path, "%s_%03d.ppm", argv[6], shot++);
                write_ppm(path, fb, f.w, f.h);
            }
            if (r.phase == RacePhase::Done && r.done_timer > 8.0f) break;
        }
        printf("%s after %.1fs:\n", r.track.def->name, r.race_time);
        for (int p = 0; p < RACERS; ++p)
            for (int i = 0; i < RACERS; ++i) {
                const Racer& k = r.racers[i];
                if (k.place != p) continue;
                printf("  %d. %-15s%s lap %d  %s%.2f  falls %d hits %d\n", p + 1, k_chars[k.ch].name, k.human ? "*" : " ", k.lap,
                       k.finished ? "time " : "at ", k.finished ? k.finish_time : k.total / r.track.length, k.stat_falls, k.stat_hits);
            }
        return 0;
    }
    return 2;
}
