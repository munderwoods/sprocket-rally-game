// The menu screens, the screen transitions and the records file. Engine-free: each screen
// is drawn from a plain view struct into the low-resolution framebuffer, so
// tools/headless/ui_shots.cc can render all of them to image files without a window.
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

namespace sr {

const char* const k_cup_names[CUPS] = {"SPROCKET CUP", "TURBO CUP", "STAR CUP"};
// Themes are spread across the cups (the two tiny-kart tables, the two watery ones and the
// dark ones are kept apart), and each cup runs roughly easy to hard.
const int k_cup_tracks[CUPS][CUP_RACES] = {{0, 4, 7, 1}, {5, 3, 9, 6}, {8, 2, 11, 10}};
const char* const k_diff_names[DIFFS] = {"EASY", "NORMAL", "HARD"};
const int k_points[RACERS] = {10, 8, 6, 5, 4, 3, 2, 1};
const PropImg& cup_icon(int cup) { return cup == 2 ? k_cup3 : (cup == 1 ? k_cup2 : k_cup1); }

// ---- records --------------------------------------------------------------------------------
// A small text file, one record per line:
//   sprocket-rally records 1
//   track <index> time <seconds> lap <seconds>
//   cup <index> diff <0..2> rank <1..8>
static bool records_path(char* out, int cap, bool make_dirs) {
    const char* home = nullptr;
#if defined(_WIN32)
    home = getenv("LOCALAPPDATA");
    if (!home || !*home) home = getenv("USERPROFILE");
    static const char* const parts[] = {"/sprocket-rally"};
#else
    home = getenv("HOME");
    static const char* const parts[] = {"/.local", "/share", "/sprocket-rally"};
#endif
    if (!home || !*home) return false;
    int n = snprintf(out, (size_t)cap, "%s", home);
    for (int i = 0; i < (int)(sizeof parts / sizeof parts[0]); ++i) {
        n += snprintf(out + n, (size_t)(cap - n), "%s", parts[i]);
        if (n >= cap - 16) return false;
        if (make_dirs) {
#if defined(_WIN32)
            _mkdir(out);
#else
            mkdir(out, 0755);
#endif
        } // fine if it exists
    }
    snprintf(out + n, (size_t)(cap - n), "/records.txt");
    return true;
}

bool records_load(Records& rec, const char* path) {
    rec = Records{};
    char buf[512];
    if (!path) { if (!records_path(buf, sizeof buf, false)) return false; path = buf; }
    FILE* fp = fopen(path, "r");
    if (!fp) return false;
    char line[128];
    while (fgets(line, sizeof line, fp)) {
        int a = 0, b = 0, c = 0;
        float t = 0, l = 0;
        if (sscanf(line, "track %d time %f lap %f", &a, &t, &l) == 3) {
            if (a >= 0 && a < REC_TRACKS && t >= 0 && t < 6000 && l >= 0 && l < 6000) { rec.best_time[a] = t; rec.best_lap[a] = l; }
        } else if (sscanf(line, "cup %d diff %d rank %d", &a, &b, &c) == 3) {
            if (a >= 0 && a < CUPS && b >= 0 && b < DIFFS && c >= 0 && c <= RACERS) rec.cup_rank[a][b] = (u8)c;
        }
    }
    fclose(fp);
    return true;
}

// ---- settings ---------------------------------------------------------------------------------
// Key numbers are the engine's Key enum (kept as numbers so this file needs no engine header):
// Left 54, Right 55, Up 56, Down 57, LShift 58, RShift 59, LCtrl 60, RCtrl 61, Space 50;
// letters from A = 1. Pad buttons: South 0, West 2, LShoulder 9, RShoulder 10.
void settings_default_binds(Settings& s) {
    static const u16 keys[BIND_PROFILES][BIND_ACTIONS] = {
        {54, 55, 26 /*Z*/, 24 /*X*/, 58, 50},   // alone
        {54, 55, 56, 57, 59, 61},               // player one of two: arrows, right shift, right ctrl
        {1 /*A*/, 4 /*D*/, 23 /*W*/, 19 /*S*/, 58, 60}, // player two: WASD, left shift, left ctrl
    };
    static const u8 pad[BIND_PAD] = {0, 2, 10, 9};
    for (int p = 0; p < BIND_PROFILES; ++p) for (int a = 0; a < BIND_ACTIONS; ++a) s.keys[p][a] = keys[p][a];
    for (int i = 0; i < BIND_PAD; ++i) s.pad[i] = pad[i];
}

static bool settings_path(char* out, int cap, bool make_dirs) {
    if (!records_path(out, cap, make_dirs)) return false;
    char* slash = strrchr(out, '/');
    if (!slash || (int)(slash - out) + 16 >= cap) return false;
    snprintf(slash, (size_t)(cap - (int)(slash - out)), "/settings.txt");
    return true;
}

bool settings_load(Settings& s) {
    settings_default_binds(s);
    char path[512];
    if (!settings_path(path, sizeof path, false)) return false;
    FILE* fp = fopen(path, "r");
    if (!fp) return false;
    char line[256];
    while (fgets(line, sizeof line, fp)) {
        int a = 0, b[6] = {};
        if (sscanf(line, "aspect %d", &a) == 1) s.wide = a != 0;
        else if (sscanf(line, "crt %d", &a) == 1) s.crt = a < 0 ? 0 : a;
        else if (sscanf(line, "split %d", &a) == 1) s.split_rows = a != 0;
        else if (sscanf(line, "display %d", &a) == 1) s.display = a < 0 || a > 2 ? 0 : a;
        else if (sscanf(line, "monitor %d", &a) == 1) s.monitor = a < 0 ? 0 : a;
        else if (sscanf(line, "volume %d", &a) == 1) s.volume = a < 0 ? 0 : (a > 10 ? 10 : a);
        else if (sscanf(line, "keys %d %d %d %d %d %d %d", &a, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 7) {
            if (a >= 0 && a < BIND_PROFILES) for (int i = 0; i < BIND_ACTIONS; ++i) if (b[i] > 0 && b[i] < 200) s.keys[a][i] = (u16)b[i];
        } else if (sscanf(line, "devices %d %d %d %d", &b[0], &b[1], &b[2], &b[3]) == 4) {
            for (int i = 0; i < 4; ++i) if (b[i] >= 0 && b[i] < 2 + 4) s.dev[i] = (u8)b[i];
        } else if (sscanf(line, "pad %d %d %d %d", &b[0], &b[1], &b[2], &b[3]) == 4) {
            for (int i = 0; i < BIND_PAD; ++i) if (b[i] >= 0 && b[i] < 32) s.pad[i] = (u8)b[i];
        }
    }
    fclose(fp);
    return true;
}

bool settings_save(const Settings& s) {
    char path[512];
    if (!settings_path(path, sizeof path, true)) return false;
    FILE* fp = fopen(path, "w");
    if (!fp) return false;
    fprintf(fp, "sprocket-rally settings 1\naspect %d\ncrt %d\nsplit %d\ndisplay %d\nmonitor %d\n", (int)s.wide, s.crt, (int)s.split_rows, s.display, s.monitor);
    fprintf(fp, "volume %d\n", s.volume);
    for (int p = 0; p < BIND_PROFILES; ++p)
        fprintf(fp, "keys %d %d %d %d %d %d %d\n", p, (int)s.keys[p][0], (int)s.keys[p][1], (int)s.keys[p][2], (int)s.keys[p][3], (int)s.keys[p][4], (int)s.keys[p][5]);
    fprintf(fp, "pad %d %d %d %d\n", (int)s.pad[0], (int)s.pad[1], (int)s.pad[2], (int)s.pad[3]);
    fprintf(fp, "devices %d %d %d %d\n", (int)s.dev[0], (int)s.dev[1], (int)s.dev[2], (int)s.dev[3]);
    return fclose(fp) == 0;
}

static bool ghost_path(char* out, int cap, int track, bool make_dirs) {
    if (!records_path(out, cap, make_dirs)) return false;
    char* slash = strrchr(out, '/');
    if (!slash || (int)(slash - out) + 24 >= cap) return false;
    snprintf(slash, (size_t)(cap - (int)(slash - out)), "/ghost_%d.bin", track);
    return true;
}

struct GhostHeader { char magic[4]; int version, track, ch, n; f32 time; };

int ghost_load(int track, int* ch, f32* time, GhostSample* out, int cap) {
    char path[512];
    if (!ghost_path(path, sizeof path, track, false)) return 0;
    FILE* fp = fopen(path, "rb");
    if (!fp) return 0;
    GhostHeader h{};
    int n = 0;
    if (fread(&h, sizeof h, 1, fp) == 1 && !memcmp(h.magic, "SRGH", 4) && h.version == 2 && h.track == track && h.n > 0 && h.n <= cap && h.time > 0) {
        if (fread(out, sizeof(GhostSample), (size_t)h.n, fp) == (size_t)h.n) { n = h.n; *ch = h.ch & 7; *time = h.time; }
    }
    fclose(fp);
    return n;
}

bool ghost_save(int track, int ch, f32 time, const GhostSample* s, int n) {
    char path[512];
    if (n <= 0 || !ghost_path(path, sizeof path, track, true)) return false;
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;
    GhostHeader h{{'S', 'R', 'G', 'H'}, 2, track, ch, n, time};
    bool ok = fwrite(&h, sizeof h, 1, fp) == 1 && fwrite(s, sizeof(GhostSample), (size_t)n, fp) == (size_t)n;
    return fclose(fp) == 0 && ok;
}

bool records_save(const Records& rec, const char* path) {
    char buf[512];
    if (!path) { if (!records_path(buf, sizeof buf, true)) return false; path = buf; }
    FILE* fp = fopen(path, "w");
    if (!fp) return false;
    fprintf(fp, "sprocket-rally records 1\n");
    for (int i = 0; i < REC_TRACKS; ++i)
        if (rec.best_time[i] > 0 || rec.best_lap[i] > 0)
            fprintf(fp, "track %d time %.2f lap %.2f\n", i, (double)rec.best_time[i], (double)rec.best_lap[i]);
    for (int c = 0; c < CUPS; ++c)
        for (int d = 0; d < DIFFS; ++d)
            if (rec.cup_rank[c][d]) fprintf(fp, "cup %d diff %d rank %d\n", c, d, (int)rec.cup_rank[c][d]);
    return fclose(fp) == 0;
}

// ---- transitions ----------------------------------------------------------------------------
void fx_fade(Frame& f, f32 t) {
    if (t <= 0) return;
    f32 k = t >= 1 ? 0.0f : 1.0f - t;
    for (int i = 0; i < f.w * f.h; ++i) f.px[i] = shade(f.px[i], k);
}

void fx_mosaic(Frame& f, f32 t) {
    if (t <= 0) return;
    if (t > 1) t = 1;
    int b = 1 + (int)(t * 15.0f + 0.5f); // 1..16, as the SNES's mosaic register
    f32 k = 1.0f - t * t;                // the fade trails the mosaic, so the blocks are seen
    for (int by = 0; by < f.h; by += b)
        for (int bx = 0; bx < f.w; bx += b) {
            u32 c = shade(f.px[by * f.w + bx], k);
            for (int y = by; y < by + b && y < f.h; ++y)
                for (int x = bx; x < bx + b && x < f.w; ++x) f.px[y * f.w + x] = c;
        }
}

void fx_iris(Frame& f, f32 t, int cx, int cy) {
    if (t <= 0) return;
    if (t > 1) t = 1;
    int dx = cx > f.w - cx ? cx : f.w - cx, dy = cy > f.h - cy ? cy : f.h - cy;
    f32 maxr = kv::sqrt((f32)(dx * dx + dy * dy)) + 2.0f;
    f32 r = maxr * (1.0f - t);
    int r2 = (int)(r * r);
    for (int y = 0; y < f.h; ++y) {
        int yy = (y - cy) * (y - cy);
        for (int x = 0; x < f.w; ++x)
            if ((x - cx) * (x - cx) + yy > r2) f.px[y * f.w + x] = 0xff000000u;
    }
}

// ---- backdrops ------------------------------------------------------------------------------
// Scrolling two-color patterns under a scanline brightness gradient, one per screen.
void menu_backdrop(Frame& f, u32 tick, int variant) {
    struct Style { u32 a, b; int pattern; };
    static const Style k[] = {
        {rgb(48, 64, 160), rgb(40, 48, 136), 0},   // 0 mode select: blue checker
        {rgb(96, 56, 152), rgb(80, 40, 136), 2},   // 1 racer select: purple diamonds
        {rgb(32, 112, 88), rgb(24, 96, 80), 1},    // 2 track select: green stripes
        {rgb(168, 104, 40), rgb(152, 88, 32), 3},  // 3 cup select: gold chevrons
        {rgb(40, 56, 120), rgb(32, 40, 104), 4},   // 4 standings: slow bands
        {rgb(72, 120, 216), rgb(96, 144, 232), 5}, // 5 ceremony: sunburst
        {rgb(64, 72, 96), rgb(56, 64, 88), 0},     // 6 options: grey checker
        {rgb(152, 64, 56), rgb(136, 48, 48), 1},   // 7 difficulty: red stripes
    };
    const Style& s = k[variant & 7];
    int off = (int)(tick / 2);
    for (int y = 0; y < f.h; ++y) {
        f32 g = 1.18f - 0.5f * (f32)(y / 8) / (f32)(f.h / 8); // stepped, like an HDMA gradient
        u32 ca = shade(s.a, g), cb = shade(s.b, g);
        u32* row = f.px + y * f.w;
        for (int x = 0; x < f.w; ++x) {
            bool on = false;
            switch (s.pattern) {
            case 0: on = ((((x + off) >> 4) ^ ((y + off / 2) >> 4)) & 1) != 0; break;
            case 1: on = (((x + y + off) >> 4) & 1) != 0; break;
            case 2: {
                int u = ((x - off) & 31) - 16, v = ((y + off / 2) & 31) - 16;
                on = (u < 0 ? -u : u) + (v < 0 ? -v : v) < 11;
            } break;
            case 3: {
                int u = (x - f.w / 2); if (u < 0) u = -u;
                on = (((y + u / 2 - off) >> 4) & 1) != 0;
            } break;
            case 4: on = (((y + (int)(6.0f * kv::sin((f32)(x + off) * 0.05f)) + off / 2) >> 4) & 1) != 0; break;
            default: {
                f32 a = kv::atan2((f32)(y - f.h - 30), (f32)(x - f.w / 2));
                on = ((int)((a + (f32)tick * 0.004f) * 7.64f + 64.0f) & 1) != 0;
            } break;
            }
            row[x] = on ? ca : cb;
        }
    }
}

// ---- helpers --------------------------------------------------------------------------------
static int cursor_bob(u32 tick) {
    static const int k[4] = {0, 1, 2, 1};
    return k[(tick / 6) & 3];
}

static void draw_cursor(Frame& f, int x, int y, u32 tick) { draw_prop_img(f, k_cursor, x + cursor_bob(tick), y, 1); }

// Greedy word wrap at max_chars per line; center: x is the middle. Returns the line count.
static int draw_wrapped(Frame& f, int x, int y, const char* s, int max_chars, u32 color, int line_h, bool center) {
    char line[64];
    int lines = 0;
    if (max_chars > 60) max_chars = 60;
    while (*s) {
        int n = 0, last_space = -1;
        while (s[n] && n < max_chars) { if (s[n] == ' ') last_space = n; ++n; }
        int take = n;
        if (s[n] && s[n] != ' ' && last_space > 0) take = last_space;
        for (int i = 0; i < take; ++i) line[i] = s[i];
        line[take] = 0;
        if (center) draw_text_c(f, x, y + lines * line_h, line, color, 1);
        else draw_text(f, x, y + lines * line_h, line, color, 1, true);
        ++lines;
        s += take;
        while (*s == ' ') ++s;
    }
    return lines;
}

static void title_bar(Frame& f, const char* title, u32 tick) {
    KV_UNUSED(tick);
    int w = text_width(title, 2) + 28;
    int x = f.w / 2 - w / 2;
    dim_rect(f, 0, 3, f.w, 18, 0.55f);
    fill_rect(f, 0, 3, f.w, 1, rgb(255, 208, 72));
    fill_rect(f, 0, 20, f.w, 1, rgb(255, 208, 72));
    KV_UNUSED(x);
    draw_text_c(f, f.w / 2, 5, title, rgb(255, 232, 96), 2);
}

static void footer_hint(Frame& f, const char* s) {
    dim_rect(f, 0, f.h - 13, f.w, 13, 0.55f);
    draw_text_c(f, f.w / 2, f.h - 10, s, rgb(216, 224, 240), 1);
}

static void portrait_box(Frame& f, int ch, int x, int y, u32 rim) {
    fill_rect(f, x - 3, y - 3, 38, 38, rgb(16, 16, 24));
    fill_rect(f, x - 2, y - 2, 36, 36, rim);
    fill_rect(f, x, y, 32, 32, shade(k_chars[ch].color[0], 0.35f));
    for (int yy = 0; yy < 32; yy += 2) fill_rect(f, x, y + yy, 32, 1, shade(k_chars[ch].color[0], 0.45f));
    draw_prop_img(f, k_portrait[ch], x, y, 1);
}

static void mini_box(Frame& f, const u8* mini, int x, int y, int size, u32 tick) {
    fill_rect(f, x - 2, y - 2, size + 4, size + 4, rgb(16, 16, 24));
    fill_rect(f, x - 1, y - 1, size + 2, size + 2, rgb(24, 40, 72));
    for (int yy = 0; yy < size; yy += 4) fill_rect(f, x - 1, y + yy, size + 2, 1, rgb(32, 52, 92));
    if (mini) draw_mini(f, mini, x, y, size, rgb(240, 240, 248), rgb(96, 184, 255));
    else if (tick & 16) draw_text_c(f, x + size / 2, y + size / 2 - 3, "...", rgb(160, 176, 216), 1);
}

// ---- title ----------------------------------------------------------------------------------
void draw_title_overlay(Frame& f, u32 tick) {
    // the logo drops in, then floats
    int drop = tick < 40 ? (int)((40 - tick) * (40 - tick)) / 16 : 0;
    int bob = (int)(2.5f * kv::sin((f32)tick * 0.05f));
    int lx = f.w / 2 - k_logo.w / 2, ly = 14 - drop + bob;
    draw_prop_img(f, k_logo, lx, ly, 1);
    if (tick > 50 && (tick & 32)) draw_text_c(f, f.w / 2, 104, "PRESS START", rgb(255, 255, 255), 2);
    if (tick > 50) draw_text_c(f, f.w / 2, 122, "ENTER / START BUTTON", rgb(255, 232, 96), 1);
    dim_rect(f, 0, f.h - 36, f.w, 36, 0.4f);
    fill_rect(f, 0, f.h - 37, f.w, 1, rgb(255, 208, 72));
    if (f.w >= 360) {
        draw_text_c(f, f.w / 2, f.h - 32, "ARROWS STEER   Z GAS   X BRAKE   SHIFT HOP AND DRIFT   SPACE ITEM", rgb(216, 216, 224), 1);
        draw_text_c(f, f.w / 2, f.h - 22, "HOLD A DRIFT FOR A MINI-TURBO   COLLECT BOLTS FOR TOP SPEED", rgb(216, 216, 224), 1);
    } else {
        draw_text_c(f, f.w / 2, f.h - 32, "ARROWS STEER  Z GAS  X BRAKE", rgb(216, 216, 224), 1);
        draw_text_c(f, f.w / 2, f.h - 22, "SHIFT HOP/DRIFT  SPACE ITEM", rgb(216, 216, 224), 1);
    }
    draw_text_c(f, f.w / 2, f.h - 12, "TAB 4:3/16:9  Q/E CRT  F11 FULL", rgb(160, 200, 255), 1);
}

// ---- list screens ---------------------------------------------------------------------------
static void list_window(Frame& f, u32 tick, const ListView& v, int cx, int y) {
    bool small = false;
    for (int i = 0; i < v.count; ++i) if (v.values[i] || v.bars[i] >= 0) small = true;
    if (small) {
        int w = f.w >= 360 ? 300 : 244, row = v.count > 8 ? 14 : 16; // a long list closes up to fit
        int h = v.count * row + 14;
        int x = cx - w / 2;
        draw_window(f, x, y, w, h, v.style);
        for (int i = 0; i < v.count; ++i) {
            int ry = y + 9 + i * row;
            bool sel = i == v.sel;
            if (sel) { dim_rect(f, x + 4, ry - 3, w - 8, 13, 0.6f); draw_cursor(f, x + 7, ry - 2, tick); }
            draw_text(f, x + 22, ry, v.items[i], sel ? rgb(255, 232, 96) : rgb(232, 232, 240), 1, true);
            if (v.bars[i] >= 0) { // a slider: ten notches, lit up to the level
                int bx = x + w - (sel ? 18 : 12) - 59;
                for (int q = 0; q < 10; ++q) {
                    bool on = q < v.bars[i];
                    fill_rect(f, bx + q * 6, ry - 1, 5, 9, rgb(16, 16, 24));
                    fill_rect(f, bx + q * 6, ry + (on ? 0 : 3) - 1, 4, on ? 8 : 2, on ? (sel ? rgb(255, 232, 96) : rgb(176, 200, 255)) : rgb(72, 88, 128));
                }
                if (sel) {
                    draw_text(f, x + w - 14, ry, ">", rgb(255, 232, 96), 1, true);
                    draw_text(f, bx - 10, ry, "<", rgb(255, 232, 96), 1, true);
                }
            } else if (v.values[i]) {
                draw_text_r(f, x + w - (sel ? 18 : 12), ry, v.values[i], sel ? rgb(255, 255, 255) : rgb(176, 200, 255), 1);
                if (sel) {
                    draw_text(f, x + w - 14, ry, ">", rgb(255, 232, 96), 1, true);
                    draw_text(f, x + w - 26 - text_width(v.values[i]), ry, "<", rgb(255, 232, 96), 1, true);
                }
            }
        }
        return;
    }
    int maxw = 0;
    for (int i = 0; i < v.count; ++i) { int tw = text_width(v.items[i], 2); if (tw > maxw) maxw = tw; }
    int row = 22;
    int w = maxw + 52, h = v.count * row + 12;
    int x = cx - w / 2;
    draw_window(f, x, y, w, h, v.style);
    for (int i = 0; i < v.count; ++i) {
        int ry = y + 10 + i * row;
        bool sel = i == v.sel;
        if (sel) {
            dim_rect(f, x + 4, ry - 3, w - 8, 20, 0.6f);
            draw_cursor(f, x + 9, ry + 2, tick);
        }
        draw_text(f, x + 28, ry, v.items[i], sel ? ((tick & 16) ? rgb(255, 255, 255) : rgb(255, 232, 96)) : rgb(200, 208, 232), 2, true);
    }
}

void draw_list_screen(Frame& f, u32 tick, const ListView& v, int backdrop) {
    menu_backdrop(f, tick, backdrop);
    int y = 30;
    if (v.title && *v.title) title_bar(f, v.title, tick);
    else {
        draw_prop_img(f, k_logo, f.w / 2 - k_logo.w / 2, 6, 1);
        y = 76;
    }
    bool small = false;
    for (int i = 0; i < v.count; ++i) if (v.values[i] || v.bars[i] >= 0) small = true;
    int h = small ? v.count * (v.count > 8 ? 14 : 16) + 14 : v.count * 22 + 12;
    int room = f.h - 50 - y;
    if (h < room) y += (room - h) / 2;
    list_window(f, tick, v, f.w / 2, y);
    const char* hint = v.sel >= 0 && v.sel < 10 ? v.hints[v.sel] : nullptr;
    if (hint && *hint) {
        int w = f.w >= 360 ? 320 : 244;
        draw_window(f, f.w / 2 - w / 2, f.h - 46, w, 28, WIN_GLASS);
        draw_wrapped(f, f.w / 2, f.h - (text_width(hint) > w - 16 ? 41 : 36), hint, (w - 16) / 6, rgb(232, 232, 240), 9, true);
    }
    footer_hint(f, v.footer && *v.footer ? v.footer : "ENTER OK   ESC BACK");
}

void draw_pause(Frame& f, u32 tick, int sel, bool gp) {
    // every other scanline darker: the race freezes behind a "video" pause
    for (int y = 0; y < f.h; ++y) dim_rect(f, 0, y, f.w, 1, (y & 1) ? 0.38f : 0.5f);
    ListView v;
    v.items[0] = "RESUME"; v.items[1] = "RESTART"; v.items[2] = "OPTIONS"; v.items[3] = gp ? "QUIT CUP" : "QUIT";
    v.count = 4; v.sel = sel; v.style = WIN_BLUE;
    int w = text_width("PAUSED", 3);
    draw_text_c(f, f.w / 2, 30, "PAUSED", (tick & 32) ? rgb(255, 232, 96) : rgb(255, 255, 255), 3);
    fill_rect(f, f.w / 2 - w / 2 - 8, 56, w + 16, 1, rgb(255, 208, 72));
    list_window(f, tick, v, f.w / 2, 68);
}

// The options, reached from the pause menu: over the frozen race, like the pause itself.
void draw_pause_options(Frame& f, u32 tick, const ListView& v) {
    for (int y = 0; y < f.h; ++y) dim_rect(f, 0, y, f.w, 1, (y & 1) ? 0.3f : 0.42f);
    int w = text_width("OPTIONS", 2);
    draw_text_c(f, f.w / 2, 10, "OPTIONS", rgb(255, 232, 96), 2);
    fill_rect(f, f.w / 2 - w / 2 - 8, 28, w + 16, 1, rgb(255, 208, 72));
    list_window(f, tick, v, f.w / 2, 36);
    if (v.hints[v.sel]) draw_wrapped(f, f.w / 2, 36 + v.count * 16 + 22, v.hints[v.sel], (f.w - 24) / 6, rgb(216, 224, 240), 10, true);
    footer_hint(f, "LEFT/RIGHT CHANGE   ESC BACK");
}

// The key and button bindings: a small table, one cell picked at a time.
void draw_controls(Frame& f, u32 tick, const ControlsView& v) {
    if (v.in_race) { for (int y = 0; y < f.h; ++y) dim_rect(f, 0, y, f.w, 1, (y & 1) ? 0.3f : 0.42f); }
    else menu_backdrop(f, tick, 6);
    if (v.in_race) draw_text_c(f, f.w / 2, 8, "CONTROLS", rgb(255, 232, 96), 2);
    else title_bar(f, "CONTROLS", tick);
    static const char* const actions[BIND_ACTIONS] = {"STEER L", "STEER R", "GAS", "BRAKE", "HOP/DRIFT", "ITEM"};
    static const char* const heads[4] = {"ALONE", "2P P1", "2P P2", "PAD"};
    const int w = 244, x0 = f.w / 2 - w / 2, y0 = 30, row = 16;
    const int colx[4] = {x0 + 70, x0 + 114, x0 + 158, x0 + 202};
    draw_window(f, x0, y0, w, 8 * row + 26, WIN_TEAL);
    for (int c = 0; c < 4; ++c) draw_text_c(f, colx[c] + 19, y0 + 7, heads[c], rgb(176, 232, 208), 1);
    fill_rect(f, x0 + 6, y0 + 17, w - 12, 1, rgb(96, 168, 176));
    for (int r = 0; r < BIND_ACTIONS; ++r) {
        int y = y0 + 23 + r * row;
        draw_text(f, x0 + 9, y, actions[r], r == v.row ? rgb(255, 232, 96) : rgb(232, 232, 240), 1, true);
        for (int c = 0; c < 4; ++c) {
            bool on = r == v.row && c == v.col;
            bool fixed = c == 3 && r < 2; // steering on a pad is the stick and D-pad
            if (on) {
                fill_rect(f, colx[c] - 1, y - 3, 40, 13, v.capturing ? ((tick & 8) ? rgb(255, 96, 64) : rgb(160, 40, 32)) : rgb(255, 216, 64));
                fill_rect(f, colx[c], y - 2, 38, 11, rgb(16, 24, 48));
            }
            const char* s = fixed ? "STICK" : (on && v.capturing ? "..." : v.cell[r][c]);
            draw_text_c(f, colx[c] + 19, y, s, fixed ? rgb(120, 144, 168) : (on ? rgb(255, 255, 255) : rgb(200, 216, 240)), 1);
        }
    }
    static const char* const tail[2] = {"RESET TO DEFAULTS", "BACK"};
    for (int i = 0; i < 2; ++i) {
        int y = y0 + 23 + (BIND_ACTIONS + i) * row + 4;
        bool on = v.row == BIND_ACTIONS + i;
        if (on) { dim_rect(f, x0 + 4, y - 3, w - 8, 13, 0.6f); draw_cursor(f, x0 + 7, y - 2, tick); }
        draw_text(f, x0 + 22, y, tail[i], on ? rgb(255, 232, 96) : rgb(232, 232, 240), 1, true);
    }
    const char* hint = v.capturing ? (v.col == 3 ? "PRESS A PAD BUTTON.  ESC CANCELS." : "PRESS A KEY.  ESC CANCELS.")
                                   : "PLAYING ALONE, PLAYER ONE'S KEYS WORK TOO.";
    draw_wrapped(f, f.w / 2, y0 + 8 * row + 32, hint, (f.w - 16) / 6, v.capturing && (tick & 8) ? rgb(255, 232, 96) : rgb(216, 224, 240), 10, true);
    footer_hint(f, v.capturing ? "ESC CANCEL" : "ARROWS CHOOSE   ENTER CHANGE   ESC BACK");
}

// ---- racer select ---------------------------------------------------------------------------
static void stat_bar(Frame& f, int x, int y, const char* label, f32 v, f32 lo, f32 hi) {
    draw_text(f, x, y, label, rgb(176, 200, 255), 1, true);
    int n = (int)((v - lo) / (hi - lo) * 8.0f + 0.5f);
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    for (int i = 0; i < 8; ++i) {
        int bx = x + 40 + i * 6;
        fill_rect(f, bx, y, 5, 7, rgb(16, 16, 24));
        u32 c = i < n ? (i >= 6 ? rgb(255, 120, 64) : i >= 3 ? rgb(255, 208, 72) : rgb(120, 232, 96)) : rgb(48, 56, 96);
        fill_rect(f, bx, y, 4, 6, c);
        if (i < n) fill_rect(f, bx, y, 4, 1, rgb(255, 255, 255));
    }
}

static void racer_card(Frame& f, u32 tick, int ch, int x, int y, int w, int h, bool tall) {
    KV_UNUSED(tick);
    const Character& c = k_chars[ch];
    draw_window(f, x, y, w, h, WIN_BLUE);
    portrait_box(f, ch, x + 9, y + 9, c.color[0]);
    bool big_name = !tall && text_width(c.name, 2) <= w - 56;
    draw_text(f, x + 48, y + 7, c.name, c.color[0], big_name ? 2 : 1, true);
    draw_text(f, x + 48, y + (big_name ? 24 : 18), c.vehicle, rgb(255, 232, 96), 1, true);
    int sy;
    if (tall) {
        draw_text(f, x + 48, y + 29, "FAVOURITE", rgb(176, 200, 255), 1, true);
        draw_text(f, x + 48, y + 38, item_name(c.favorite), rgb(255, 255, 255), 1, true);
        fill_rect(f, x + w - 31, y + 25, 20, 20, rgb(16, 16, 24));
        fill_rect(f, x + w - 30, y + 26, 18, 18, rgb(40, 48, 104));
        if (const PropImg* icon = item_icon(c.favorite)) draw_prop_img(f, *icon, x + w - 29, y + 27, 1);
        draw_text(f, x + 12, y + 56, c.bio[0], rgb(232, 232, 240), 1, true);
        draw_text(f, x + 12, y + 67, c.bio[1], rgb(232, 232, 240), 1, true);
        fill_rect(f, x + 8, y + 82, w - 16, 1, rgb(136, 152, 216));
        sy = y + 92;
        int sx = x + w / 2 - 44;
        stat_bar(f, sx, sy, "SPEED", c.top, 0.92f, 1.08f);
        stat_bar(f, sx, sy + 13, "ACCEL", c.accel, 0.86f, 1.14f);
        stat_bar(f, sx, sy + 26, "TURN", c.turn, 0.92f, 1.12f);
        stat_bar(f, sx, sy + 39, "WEIGHT", c.weight, 0.5f, 1.7f);
        stat_bar(f, sx, sy + 52, "DRIFT", c.charge * c.turbo, 0.85f, 1.4f);
        stat_bar(f, sx, sy + 65, "DIRT", c.offroad, -0.05f, 0.7f);
        return;
    }
    draw_text(f, x + 48, y + 35, c.bio[0], rgb(232, 232, 240), 1, true);
    draw_text(f, x + 48, y + 44, c.bio[1], rgb(232, 232, 240), 1, true);
    sy = y + 58;
    int sx = x + 9;
    stat_bar(f, sx, sy, "SPEED", c.top, 0.92f, 1.08f);
    stat_bar(f, sx, sy + 10, "ACCEL", c.accel, 0.86f, 1.14f);
    stat_bar(f, sx, sy + 20, "TURN", c.turn, 0.92f, 1.12f);
    stat_bar(f, sx + 96, sy, "WEIGHT", c.weight, 0.5f, 1.7f);
    stat_bar(f, sx + 96, sy + 10, "DRIFT", c.charge * c.turbo, 0.85f, 1.4f);
    stat_bar(f, sx + 96, sy + 20, "DIRT", c.offroad, -0.05f, 0.7f);
    // the favourite item, down the right edge
    int fx = x + w - 22;
    draw_text_c(f, fx - 1, sy - 1, "FAV", rgb(176, 200, 255), 1);
    fill_rect(f, fx - 11, sy + 8, 20, 20, rgb(16, 16, 24));
    fill_rect(f, fx - 10, sy + 9, 18, 18, rgb(40, 48, 104));
    if (const PropImg* icon = item_icon(c.favorite)) draw_prop_img(f, *icon, fx - 9, sy + 10, 1);
}

void draw_racer_select(Frame& f, u32 tick, int sel, int player, const int* picked, int npicked) {
    menu_backdrop(f, tick, 1);
    static const char* const titles[5] = {"CHOOSE YOUR RACER", "PLAYER 1 RACER", "PLAYER 2 RACER", "PLAYER 3 RACER", "PLAYER 4 RACER"};
    title_bar(f, titles[player < 0 || player > 4 ? 0 : player], tick);
    bool wide = f.w >= 360;
    const int cell = 40;
    int gx = wide ? 14 : 6, gy = 25;
    for (int i = 0; i < RACERS; ++i) {
        int x = gx + (i % 4) * cell, y = gy + (i / 4) * cell;
        bool on = i == sel;
        u32 rim = on ? ((tick & 8) ? rgb(255, 255, 255) : rgb(255, 216, 64)) : shade(k_chars[i].color[0], 0.7f);
        fill_rect(f, x + 1, y + 1, cell - 2, cell - 2, rgb(16, 16, 24));
        fill_rect(f, x + 2, y + 2, cell - 4, cell - 4, rim);
        fill_rect(f, x + 4, y + 4, cell - 8, cell - 8, shade(k_chars[i].color[0], on ? 0.5f : 0.28f));
        draw_prop_img(f, k_portrait[i], x + 4, y + 4, 1);
        if (!on) dim_rect(f, x + 4, y + 4, cell - 8, cell - 8, 0.72f);
        for (int q = 0; q < npicked && picked; ++q)
            if (picked[q] == i) { // an earlier player's pick: marked, and not to be had twice
                char tag[3] = {(char)('1' + q), 'P', 0};
                dim_rect(f, x + 4, y + 4, cell - 8, cell - 8, 0.45f);
                fill_rect(f, x + 4, y + cell - 14, 15, 10, rgb(16, 16, 24));
                draw_text(f, x + 6, y + cell - 12, tag, rgb(255, 232, 96), 1, false);
            }
    }
    {
        int x = gx + (sel % 4) * cell, y = gy + (sel / 4) * cell;
        draw_cursor(f, x - 4, y + 15, tick);
    }
    // the highlighted racer's kart, turning on a stage: beside the grid, or under it in 16:9
    int kx = wide ? gx : gx + 4 * cell + 4, ky = wide ? gy + 2 * cell + 4 : gy;
    int kw = wide ? 4 * cell : 256 - 6 - kx, kh = wide ? f.h - 17 - ky : 2 * cell;
    draw_window(f, kx, ky, kw, kh, WIN_PURPLE);
    {
        int cx = kx + kw / 2, by = ky + kh - 14;
        for (int i = 0; i < 5; ++i) { // an oval turntable
            int hw = 30 - i * i;
            fill_rect(f, cx - hw, by - 5 + i, hw * 2, 1, rgb(24, 16, 56));
            fill_rect(f, cx - hw, by + 5 - i, hw * 2, 1, rgb(24, 16, 56));
        }
        fill_rect(f, cx - 31, by - 1, 62, 3, rgb(24, 16, 56));
        draw_kart_icon(f, sel, (int)(tick / 5), cx - 32, by - 54, 2);
    }
    if (wide) racer_card(f, tick, sel, gx + 4 * cell + 6, gy, f.w - 14 - (gx + 4 * cell + 6), f.h - 17 - gy, true);
    else racer_card(f, tick, sel, 6, gy + 2 * cell + 4, 244, 98, false);
    footer_hint(f, "ARROWS CHOOSE   ENTER OK   ESC BACK");
}

// ---- track select ---------------------------------------------------------------------------
static void record_lines(Frame& f, int x, int y, int w, const TrackCard& c) {
    char buf[16];
    draw_text(f, x, y, "BEST", rgb(176, 200, 255), 1, true);
    ui_time(buf, c.best_time);
    draw_text_r(f, x + w, y, buf, c.best_time > 0 ? rgb(255, 232, 96) : rgb(120, 136, 176), 1);
    draw_text(f, x, y + 10, "LAP", rgb(176, 200, 255), 1, true);
    ui_time(buf, c.best_lap);
    draw_text_r(f, x + w, y + 10, buf, c.best_lap > 0 ? rgb(255, 232, 96) : rgb(120, 136, 176), 1);
}

void draw_track_select(Frame& f, u32 tick, const TrackSelectView& v) {
    menu_backdrop(f, tick, 2);
    title_bar(f, "CHOOSE A TRACK", tick);
    bool wide = f.w >= 360;
    char buf[40];
    int lx = wide ? 14 : 6, ly = 25, lw = wide ? 134 : 116, row = (f.h - 15 - 25 - (wide ? 30 : 46) - 12) / (v.count > 0 ? v.count : 1);
    if (row > 17) row = 17;
    int lh = v.count * row + 12;
    draw_window(f, lx, ly, lw, lh, WIN_GREEN);
    for (int i = 0; i < v.count; ++i) {
        int y = ly + 6 + row / 2 - 3 + i * row;
        bool on = i == v.sel;
        if (on) { dim_rect(f, lx + 4, y - 3, lw - 8, 13, 0.6f); draw_cursor(f, lx + 6, y - 2, tick); }
        u32 c = on ? ((tick & 16) ? rgb(255, 255, 255) : rgb(255, 232, 96)) : (i == v.count - 1 && v.names[i][0] == 'R' ? rgb(160, 232, 255) : rgb(216, 232, 224));
        draw_text(f, lx + 20, y, v.names[i], c, 1, true);
    }
    // the track's card: map, theme, length, records
    int ix = lx + lw + 4, iw = f.w - lx - ix;
    draw_window(f, ix, ly, iw, lh, WIN_TEAL);
    const TrackCard& c = v.card;
    int ms = MINI;
    int mx = wide ? ix + 10 : ix + iw / 2 - ms / 2, my = ly + 9;
    mini_box(f, c.mini, mx, my, ms, tick);
    int tx = wide ? mx + ms + 12 : ix + 9, ty = wide ? ly + 9 : my + ms + 6, tw = wide ? ix + iw - 10 - tx : iw - 18;
    if (wide) { draw_text(f, tx, ty, c.name, rgb(255, 232, 96), 1, true); ty += 14; }
    draw_text(f, tx, ty, c.theme, rgb(255, 255, 255), 1, true);
    buf[0] = 0; ui_int(buf, c.length); ui_cat(buf, sizeof buf, "M");
    draw_text_r(f, tx + tw, ty, buf, rgb(176, 232, 208), 1);
    if (v.random) {
        draw_text(f, tx, ty + 12, "SEED", rgb(176, 200, 255), 1, true);
        ui_int(buf, (int)v.seed);
        draw_text_r(f, tx + tw, ty + 12, buf, rgb(255, 232, 96), 1);
        draw_text(f, tx, ty + 22, (tick & 16) ? "<  NEW TRACK  >" : " < NEW TRACK > ", rgb(160, 232, 255), 1, true);
    } else record_lines(f, tx, ty + 12, tw, c);
    // the blurb
    int by = ly + lh + 4, bh = f.h - 15 - by;
    draw_window(f, lx, by, f.w - 2 * lx, bh, WIN_GLASS);
    if (!wide) draw_text_c(f, f.w / 2, by + 7, c.name, rgb(255, 232, 96), 1);
    draw_wrapped(f, f.w / 2, by + (wide ? bh / 2 - 4 : 19), c.blurb, (f.w - 2 * lx - 16) / 6, rgb(232, 232, 240), 10, true);
    footer_hint(f, v.random ? "< > NEW TRACK   ENTER RACE   ESC BACK" : "ENTER RACE   ESC BACK");
}

// ---- cup select -----------------------------------------------------------------------------
static void cup_emblem(Frame& f, int cup, int x, int y) { // 48x48
    if (cup != CUP_RANDOM) { draw_prop_img(f, cup_icon(cup), x, y, 2); return; }
    // the random cup: a "?" on a medal
    int cx = x + 24, cy = y + 24;
    for (int yy = -22; yy <= 22; ++yy)
        for (int xx = -22; xx <= 22; ++xx) {
            int d = xx * xx + yy * yy;
            if (d > 22 * 22) continue;
            u32 c = d > 20 * 20 ? rgb(16, 16, 24) : d > 16 * 16 ? rgb(255, 208, 72) : d > 15 * 15 ? rgb(152, 96, 24) : ((xx + yy) & 4 ? rgb(40, 136, 120) : rgb(32, 120, 112));
            put_px(f, cx + xx, cy + yy, c);
        }
    draw_text(f, cx - 9, cy - 14, "?", rgb(255, 255, 255), 4, true);
}

static void cup_card(Frame& f, u32 tick, int cup, int x, int y, int w, int h, bool on, u8 best, u32 seed) {
    static const int style[CUPS] = {WIN_BLUE, WIN_PURPLE, WIN_TEAL};
    int shift = on ? cursor_bob(tick) : 0;
    draw_window(f, x, y, w, h, on ? style[cup % CUPS] : WIN_GLASS);
    if (on) {
        u32 c = (tick & 8) ? rgb(255, 255, 255) : rgb(255, 216, 64);
        fill_rect(f, x + 1, y + 1, w - 2, 1, c); fill_rect(f, x + 1, y + h - 2, w - 2, 1, c);
        fill_rect(f, x + 1, y + 1, 1, h - 2, c); fill_rect(f, x + w - 2, y + 1, 1, h - 2, c);
    }
    cup_emblem(f, cup, x + 4 + shift, y + h / 2 - 24);
    int tx = x + 57;
    // the name on two lines: "SPROCKET" / "CUP"
    char first[16];
    const char* n = k_cup_names[cup];
    int i = 0;
    for (; n[i] && n[i] != ' ' && i < 15; ++i) first[i] = n[i];
    first[i] = 0;
    u32 nc = on ? rgb(255, 232, 96) : rgb(176, 184, 208);
    draw_text(f, tx, y + 8, first, nc, 1, true);
    draw_text(f, tx, y + 18, n[i] ? n + i + 1 : "", nc, 1, true);
    int by = y + 32;
    if (cup == CUP_RANDOM) {
        char buf[16] = "";
        ui_int(buf, (int)seed);
        draw_text(f, tx, y + 31, "SEED", rgb(176, 200, 255), 1, true);
        draw_text(f, tx + 28, y + 31, buf, rgb(255, 255, 255), 1, true);
        by = y + 42;
    }
    if (best) {
        static const u32 medal[3] = {rgb(255, 216, 64), rgb(208, 224, 240), rgb(240, 152, 80)};
        draw_text(f, tx, by, "BEST", rgb(176, 200, 255), 1, true);
        draw_text(f, tx + 28, by, ui_ordinal(best - 1), best <= 3 ? medal[best - 1] : rgb(216, 216, 224), 1, true);
    } else draw_text(f, tx, by, "NO TROPHY", rgb(120, 136, 176), 1, true);
    if (!on) dim_rect(f, x + 2, y + 2, w - 4, h - 4, 0.8f);
}

void draw_cup_select(Frame& f, u32 tick, const CupSelectView& v) {
    menu_backdrop(f, tick, 3);
    title_bar(f, "CHOOSE A CUP", tick);
    draw_text_r(f, f.w - 6, 9, k_diff_names[v.diff % DIFFS], rgb(255, 255, 255), 1);
    bool wide = f.w >= 360;
    int lx = wide ? 14 : 6, ly = 25, cw = wide ? 150 : 116, ch = 58;
    for (int c = 0; c < CUPS; ++c) cup_card(f, tick, c, lx, ly + c * (ch + 2), cw, ch, c == v.sel, v.best_rank[c], v.seed);
    // the four tracks of the highlighted cup
    int wx = lx + cw + 4, wy = ly, ww = f.w - lx - wx, wh = CUPS * (ch + 2) - 2;
    draw_window(f, wx, wy, ww, wh, WIN_GLASS);
    int col = (ww - 8) / 2, rowh = (wh - 10) / 2, ms = wide ? 56 : 48;
    for (int i = 0; i < CUP_RACES; ++i) {
        const TrackCard& t = v.tracks[v.sel % CUPS][i];
        int cx = wx + 4 + (i % 2) * col + col / 2, ty = wy + 8 + (i / 2) * rowh;
        char num[2] = {(char)('1' + i), 0};
        mini_box(f, t.mini, cx - ms / 2, ty, ms, tick);
        fill_rect(f, cx - ms / 2 - 2, ty - 2, 10, 11, rgb(16, 16, 24));
        fill_rect(f, cx - ms / 2 - 1, ty - 1, 8, 9, rgb(255, 208, 72));
        draw_text(f, cx - ms / 2 + 1, ty, num, rgb(16, 16, 24), 1, false);
        int ny = ty + ms + 5;
        int lines = draw_wrapped(f, cx, ny, t.name, (col - 2) / 6, rgb(255, 232, 96), 9, true);
        char buf[16];
        ui_time(buf, t.best_time);
        if (lines < 2 || !wide) {
            if (v.sel == CUP_RANDOM) { if (lines < 2) draw_text_c(f, cx, ny + 9, t.theme, rgb(160, 176, 216), 1); }
            else if (lines < 2 || rowh - ms - 5 >= 27) draw_text_c(f, cx, ny + lines * 9, buf, t.best_time > 0 ? rgb(232, 232, 240) : rgb(120, 136, 176), 1);
        }
    }
    footer_hint(f, v.sel == CUP_RANDOM ? "UP/DOWN CUP   < > NEW SEED   ENTER START" : "UP/DOWN CHOOSE   ENTER START   ESC BACK");
}

// ---- standings ------------------------------------------------------------------------------
void draw_standings(Frame& f, u32 tick, const StandingsView& v) {
    menu_backdrop(f, tick, 4);
    bool wide = f.w >= 360;
    char buf[48] = "";
    ui_cat(buf, sizeof buf, k_cup_names[v.cup % CUPS]);
    title_bar(f, buf, tick);
    int w = 232, h = 8 * 19 + 33;
    int x0 = wide ? 14 : f.w / 2 - w / 2, y0 = 24;
    draw_window(f, x0, y0, w, h, WIN_BLUE);
    buf[0] = 0;
    ui_cat(buf, sizeof buf, v.race >= CUP_RACES ? "FINAL STANDINGS" : "STANDINGS AFTER RACE ");
    if (v.race < CUP_RACES) { char n[4]; ui_int(n, v.race); ui_cat(buf, sizeof buf, n); ui_cat(buf, sizeof buf, " OF 4"); }
    draw_text_c(f, x0 + w / 2, y0 + 7, buf, rgb(255, 255, 255), 1);
    fill_rect(f, x0 + 6, y0 + 17, w - 12, 1, rgb(255, 208, 72));
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < RACERS; ++i) {
            bool me = v.human_mask ? ((v.human_mask >> v.ch[i]) & 1u) != 0 : v.ch[i] == v.player_ch;
            if ((pass == 1) != me) continue; // the players' rows are drawn last, on top
            const Character& c = k_chars[v.ch[i]];
            int y = y0 + 22 + (int)(v.slot[i] * 19.0f + 0.5f);
            int place = (int)(v.slot[i] + 0.5f);
            if (me) {
                fill_rect(f, x0 + 4, y - 1, w - 8, 17, (tick & 16) ? rgb(72, 72, 16) : rgb(96, 80, 16));
                fill_rect(f, x0 + 4, y - 1, w - 8, 1, rgb(255, 216, 64));
                fill_rect(f, x0 + 4, y + 15, w - 8, 1, rgb(255, 216, 64));
            }
            static const u32 medal[3] = {rgb(255, 216, 64), rgb(208, 224, 240), rgb(240, 152, 80)};
            u32 col = me ? ((tick & 16) ? rgb(255, 255, 255) : rgb(255, 232, 96)) : rgb(224, 224, 232);
            draw_text(f, x0 + 8, y + 4, ui_ordinal(place), place < 3 ? medal[place] : rgb(176, 184, 208), 1, true);
            fill_rect(f, x0 + 30, y - 1, 18, 17, c.color[0]);
            fill_rect(f, x0 + 31, y, 16, 15, rgb(24, 24, 48));
            draw_img_half(f, k_portrait[v.ch[i]], x0 + 31, y - 1);
            draw_text(f, x0 + 53, y + 4, c.name, col, 1, true);
            char n[12];
            ui_int(n, v.shown[i]);
            draw_text_r(f, x0 + w - 36, y + 1, n, col, 2);
            if (v.gain[i] > 0) {
                char g[12] = "+";
                ui_int(g + 1, v.gain[i]);
                draw_text_r(f, x0 + w - 8, y + 4, g, rgb(120, 255, 160), 1);
            }
        }
    if (!v.counting && (tick & 32)) draw_text_c(f, x0 + w / 2, y0 + h - 10, "PRESS ENTER", rgb(248, 248, 248), 1);
    // what comes next
    if (wide) {
        int sx = x0 + w + 6, sw = f.w - sx - 14;
        draw_window(f, sx, y0, sw, h, WIN_TEAL);
        int cx = sx + sw / 2;
        if (v.next.mini || v.next.name[0]) {
            draw_text_c(f, cx, y0 + 8, "NEXT RACE", rgb(255, 255, 255), 1);
            mini_box(f, v.next.mini, cx - 32, y0 + 22, 64, tick);
            int lines = draw_wrapped(f, cx, y0 + 94, v.next.name, (sw - 12) / 6, rgb(255, 232, 96), 9, true);
            draw_wrapped(f, cx, y0 + 98 + lines * 9, v.next.blurb, (sw - 12) / 6, rgb(216, 224, 240), 9, true);
        } else {
            draw_text_c(f, cx, y0 + 8, "CUP COMPLETE", rgb(255, 255, 255), 1);
            cup_emblem(f, v.cup % CUPS, cx - 24, y0 + 30);
            draw_wrapped(f, cx, y0 + 94, "TO THE PODIUM!", (sw - 12) / 6, rgb(255, 232, 96), 9, true);
        }
    } else {
        buf[0] = 0;
        if (v.next.name[0]) { ui_cat(buf, sizeof buf, "NEXT: "); ui_cat(buf, sizeof buf, v.next.name); }
        else ui_cat(buf, sizeof buf, "CUP COMPLETE - TO THE PODIUM!");
        footer_hint(f, buf);
    }
}

// ---- trophy ceremony ------------------------------------------------------------------------
static void confetti(Frame& f, u32 t) {
    static const u32 cols[6] = {rgb(255, 64, 64), rgb(255, 216, 64), rgb(96, 232, 96), rgb(96, 176, 255), rgb(255, 128, 216), rgb(255, 255, 255)};
    for (int i = 0; i < 90; ++i) {
        u32 h = hash2(i, 77);
        int speed = 2 + (int)(h % 3);                        // quarter pixels per tick
        u32 span = (u32)f.h + 16, y0 = (h >> 8) % span, fallen = t * (u32)speed / 4;
        if (fallen + y0 < span) continue;                    // not in from the top yet
        int y = (int)((y0 + fallen) % span) - 8;
        int x = (int)((h >> 3) % (u32)f.w) + (int)(5.0f * kv::sin((f32)t * 0.07f + (f32)(h & 63)));
        u32 c = cols[(h >> 20) % 6];
        if (((t / 6 + h) & 3) == 0) fill_rect(f, x, y, 3, 1, c); // tumbling: seen edge-on now and then
        else fill_rect(f, x, y, 2, 2, c);
    }
}

void draw_ceremony(Frame& f, u32 tick, const CeremonyView& v) {
    menu_backdrop(f, tick, 5);
    bool wide = f.w >= 360;
    int cx = f.w / 2;
    char buf[48];
    title_bar(f, k_cup_names[v.cup % CUPS], tick);
    buf[0] = 0;
    ui_cat(buf, sizeof buf, k_diff_names[v.diff % DIFFS]);
    ui_cat(buf, sizeof buf, " CLASS");
    draw_text_c(f, cx, 26, buf, rgb(255, 255, 255), 1);

    // the podium; the karts drop onto it, third place first
    int px = cx - k_podium.w / 2, py = 78;
    // a floor under it
    for (int y = py + 30; y < f.h; ++y) dim_rect(f, 0, y, f.w, 1, 0.55f + 0.2f * (f32)((y - py - 30) / 8) / 14.0f);
    draw_prop_img(f, k_podium, px, py, 1);
    static const int block_x[3] = {60, 20, 100}, block_top[3] = {1, 9, 15};
    for (int rank = 2; rank >= 0; --rank) {
        u32 start = (u32)(2 - rank) * 40 + 20;
        if (v.t < start) continue;
        u32 dt = v.t - start;
        int fall = dt < 16 ? (int)((16 - dt) * (16 - dt)) / 3 : 0;
        int hop = dt >= 16 && dt < 28 ? (int)(4.0f * kv::sin(3.14159f * (f32)(dt - 16) / 12.0f)) : 0; // a little bounce on landing
        if (rank == 0 && v.t > 200) hop = (int)kv::fabs(5.0f * kv::sin((f32)v.t * 0.12f)); // the winner celebrates
        int angle = rank == 0 && v.t > 200 ? (int)(v.t / 5) : 18;
        draw_kart_icon(f, v.ch[rank], angle, px + block_x[rank] - 16, py + block_top[rank] + 1 - KART_ANCHOR_Y - fall - hop, 1);
    }
    // the trophies, set out in front
    if (v.t > 150) {
        const PropImg* tr[3] = {&k_trophy_gold, &k_trophy_silver, &k_trophy_bronze};
        u32 dt = v.t - 150;
        int rise = dt < 20 ? (int)(20 - dt) : 0;
        for (int rank = 0; rank < 3; ++rank) {
            int tx = px + block_x[rank] - 14, ty = py + 40 + rise;
            draw_prop_img(f, *tr[rank], tx, ty, 1);
            if (!rise && ((tick / 8 + (u32)rank * 3) % 6) == 0) { // a glint
                int gx = tx + 6 + rank * 5, gy = ty + 5;
                fill_rect(f, gx - 2, gy, 5, 1, rgb(255, 255, 255)); fill_rect(f, gx, gy - 2, 1, 5, rgb(255, 255, 255));
            }
        }
    }
    // the top three, named
    int wy = py + 76, ww = 204;
    draw_window(f, cx - ww / 2, wy, ww, 38, WIN_GLASS);
    for (int rank = 0; rank < 3; ++rank) {
        static const u32 medal[3] = {rgb(255, 216, 64), rgb(208, 224, 240), rgb(240, 152, 80)};
        int y = wy + 6 + rank * 9;
        bool me = rank == v.player_rank;
        u32 col = me ? ((tick & 16) ? rgb(255, 255, 255) : rgb(255, 232, 96)) : k_chars[v.ch[rank]].color[0];
        draw_text(f, cx - ww / 2 + 8, y, ui_ordinal(rank), medal[rank], 1, true);
        draw_text(f, cx - ww / 2 + 34, y, k_chars[v.ch[rank]].name, col, 1, true);
        buf[0] = 0; ui_int(buf, v.pts[rank]); ui_cat(buf, sizeof buf, " PTS");
        draw_text_r(f, cx + ww / 2 - 8, y, buf, rgb(232, 232, 240), 1);
    }
    // the player's result
    {
        const char* msg;
        buf[0] = 0;
        if (v.player_rank == 0) msg = "YOU ARE THE CHAMPION!";
        else {
            ui_cat(buf, sizeof buf, v.player_rank < 3 ? "YOU TOOK " : "YOU FINISHED ");
            ui_cat(buf, sizeof buf, ui_ordinal(v.player_rank));
            if (v.player_rank < 3) ui_cat(buf, sizeof buf, " PLACE!");
            msg = buf;
        }
        u32 c = (tick & 8) ? rgb(255, 255, 255) : (v.player_rank < 3 ? rgb(255, 216, 64) : rgb(160, 216, 255));
        if (v.t > 170) draw_text_c(f, cx, wy + 43, msg, c, text_width(msg, 2) < f.w - 8 ? 2 : 1);
    }
    if (wide) {
        // 16:9: the player's prize on the left, the whole table on the right
        int sw = 112;
        draw_window(f, 10, 40, sw, 104, WIN_GLASS);
        const Character& c = k_chars[v.ch[v.player_rank & 7]];
        draw_text_c(f, 10 + sw / 2, 47, c.name, c.color[0], 1);
        if (v.player_rank < 3) {
            const PropImg* tr[3] = {&k_trophy_gold, &k_trophy_silver, &k_trophy_bronze};
            draw_prop_img(f, *tr[v.player_rank], 10 + sw / 2 - 28, 62, 2);
        } else {
            const PropImg& b = place_badge(v.player_rank);
            draw_prop_img(f, b, 10 + sw / 2 - b.w, 72, 2);
        }
        draw_text_c(f, 10 + sw / 2, 131, v.player_rank < 3 ? "WELL RACED!" : "NEXT TIME!", rgb(232, 232, 240), 1);
        int rx = f.w - 10 - sw;
        draw_window(f, rx, 40, sw, 104, WIN_GLASS);
        draw_text_c(f, rx + sw / 2, 46, "FINAL POINTS", rgb(255, 255, 255), 1);
        for (int i = 0; i < RACERS; ++i) {
            int y = 59 + i * 10;
            bool me = i == v.player_rank;
            u32 col = me ? rgb(255, 232, 96) : k_chars[v.ch[i]].color[0];
            // the first word of the name is enough here
            char first[16];
            const char* n = k_chars[v.ch[i]].name;
            int k = 0;
            for (; n[k] && n[k] != ' ' && k < 15; ++k) first[k] = n[k];
            first[k] = 0;
            buf[0] = (char)('1' + i); buf[1] = 0;
            draw_text(f, rx + 8, y, buf, rgb(176, 200, 255), 1, true);
            draw_text(f, rx + 18, y, first, col, 1, true);
            ui_int(buf, v.pts[i]);
            draw_text_r(f, rx + sw - 8, y, buf, rgb(232, 232, 240), 1);
        }
    }
    confetti(f, v.t);
    if (v.t > 240 && (tick & 32)) footer_hint(f, "PRESS ENTER");
}

} // namespace sr
