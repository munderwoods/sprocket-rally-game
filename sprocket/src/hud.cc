// The race HUD, the minimap and the widgets every screen shares (framed windows, small
// formatting helpers): everything drawn over the world view, in the same low-resolution
// framebuffer. Engine-free; the headless tools build this file as it is.
#include "ui.h"

namespace sr {

// ---- formatting -----------------------------------------------------------------------------
const char* ui_ordinal(int place) {
    static const char* k[] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH"};
    return k[place & 7];
}

void ui_time(char* buf, f32 t) {
    if (t <= 0) { const char* d = "-'--\"--"; int i = 0; for (; d[i]; ++i) buf[i] = d[i]; buf[i] = 0; return; }
    int cs = (int)(t * 100.0f);
    int m = cs / 6000, s = (cs / 100) % 60, c = cs % 100;
    buf[0] = (char)('0' + m % 10); buf[1] = '\''; buf[2] = (char)('0' + s / 10); buf[3] = (char)('0' + s % 10);
    buf[4] = '"'; buf[5] = (char)('0' + c / 10); buf[6] = (char)('0' + c % 10); buf[7] = 0;
}

void ui_int(char* buf, int v) {
    char tmp[12];
    int n = 0, o = 0;
    if (v < 0) { buf[o++] = '-'; v = -v; }
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; ++i) buf[o + i] = tmp[n - 1 - i];
    buf[o + n] = 0;
}

void ui_cat(char* buf, int cap, const char* s) {
    int n = 0;
    while (buf[n]) ++n;
    while (*s && n < cap - 1) buf[n++] = *s++;
    buf[n] = 0;
}

// ---- widgets --------------------------------------------------------------------------------
void draw_text_r(Frame& f, int xr, int y, const char* s, u32 color, int scale) {
    draw_text(f, xr - text_width(s, scale), y, s, color, scale, true);
}

void draw_img_half(Frame& f, const PropImg& img, int x, int y) {
    for (int yy = 0; yy < img.h / 2; ++yy)
        for (int xx = 0; xx < img.w / 2; ++xx) {
            u8 v = img.px[(yy * 2) * img.w + xx * 2];
            if (v) put_px(f, x + xx, y + yy, img.pal[v]);
        }
}

void draw_window(Frame& f, int x, int y, int w, int h, int style) {
    static const u32 top[] = {rgb(56, 88, 200), rgb(40, 144, 88), rgb(128, 64, 184), rgb(184, 56, 64), rgb(32, 136, 152), 0};
    static const u32 bot[] = {rgb(16, 16, 80), rgb(8, 48, 48), rgb(40, 16, 80), rgb(72, 16, 32), rgb(8, 40, 72), 0};
    static const u8 bayer[4] = {0, 2, 3, 1};
    if (w < 8 || h < 8) return;
    const int bands = 7;
    for (int yy = 2; yy < h - 2; ++yy) {
        f32 lv = (f32)(yy - 2) / (f32)(h - 4) * (f32)(bands - 1);
        int base = (int)lv;
        f32 fr = lv - (f32)base;
        for (int xx = 2; xx < w - 2; ++xx) {
            int px = x + xx, py = y + yy;
            if ((unsigned)px >= (unsigned)f.w || (unsigned)py >= (unsigned)f.h) continue;
            u32& d = f.px[py * f.w + px];
            if (style == WIN_GLASS) {
                // darken and tint blue, a little lighter toward the top
                d = blend(shade(d, 0.30f), rgb(16, 24, 96), 0.35f - 0.15f * (f32)yy / (f32)h);
                continue;
            }
            int b = base + ((f32)bayer[(py & 1) * 2 + (px & 1)] * 0.25f + 0.125f < fr ? 1 : 0);
            d = blend(top[style], bot[style], (f32)b / (f32)(bands - 1));
        }
    }
    u32 out = rgb(16, 16, 24), hi = rgb(248, 248, 248), lo = rgb(136, 152, 216), gold = rgb(255, 208, 72);
    // the frame: a dark outline with cut corners, a bright rim, a softer inner rim
    fill_rect(f, x + 1, y, w - 2, 1, out);
    fill_rect(f, x + 1, y + h - 1, w - 2, 1, out);
    fill_rect(f, x, y + 1, 1, h - 2, out);
    fill_rect(f, x + w - 1, y + 1, 1, h - 2, out);
    fill_rect(f, x + 1, y + 1, w - 2, 1, hi);
    fill_rect(f, x + 1, y + h - 2, w - 2, 1, lo);
    fill_rect(f, x + 1, y + 1, 1, h - 2, hi);
    fill_rect(f, x + w - 2, y + 1, 1, h - 2, lo);
    put_px(f, x + 1, y + 1, out); put_px(f, x + w - 2, y + 1, out);
    put_px(f, x + 1, y + h - 2, out); put_px(f, x + w - 2, y + h - 2, out);
    // gold rivets in the corners
    put_px(f, x + 2, y + 2, gold); put_px(f, x + w - 3, y + 2, gold);
    put_px(f, x + 2, y + h - 3, gold); put_px(f, x + w - 3, y + h - 3, gold);
    // a shadow under the window
    for (int xx = 2; xx < w; ++xx) {
        int px = x + xx, py = y + h;
        if ((unsigned)px < (unsigned)f.w && (unsigned)py < (unsigned)f.h) f.px[py * f.w + px] = shade(f.px[py * f.w + px], 0.55f);
    }
}

const PropImg& place_badge(int place) {
    static const PropImg* k[8] = {&k_place1, &k_place2, &k_place3, &k_place4, &k_place5, &k_place6, &k_place7, &k_place8};
    return *k[place & 7];
}

void draw_lights(Frame& f, int stage, int x, int y, int scale) {
    const PropImg* base = stage <= 0 ? &k_light0 : stage == 1 ? &k_light1 : stage >= 4 ? &k_light3 : &k_light2;
    draw_prop_img(f, *base, x, y, scale);
    if (stage != 3) return;
    // three reds: there is no such image, so light the third lamp with the second's pixels
    const PropImg &a = k_light1, &b = k_light2, &d = k_light0;
    int first1 = -1, first2 = -1;
    for (int xx = 0; xx < a.w && (first1 < 0 || first2 < 0); ++xx)
        for (int yy = 0; yy < a.h; ++yy) {
            int i = yy * a.w + xx;
            if (first1 < 0 && a.pal[a.px[i]] != d.pal[d.px[i]]) first1 = xx;
            if (first2 < 0 && b.pal[b.px[i]] != a.pal[a.px[i]]) first2 = xx;
        }
    int step = first2 - first1;
    if (first1 < 0 || first2 < 0 || step <= 0) return;
    for (int yy = 0; yy < b.h; ++yy)
        for (int xx = first2; xx < b.w - step; ++xx) {
            int i = yy * b.w + xx;
            if (b.pal[b.px[i]] != a.pal[a.px[i]]) fill_rect(f, x + (xx + step) * scale, y + yy * scale, scale, scale, b.pal[b.px[i]]);
        }
}

// ---- minimap --------------------------------------------------------------------------------
void draw_mini(Frame& f, const u8* mini, int x, int y, int size, u32 road, u32 hazard) {
    if (!mini || size <= 0) return;
    if (size > MINI) size = MINI;
    for (int pass = 0; pass < 2; ++pass)
        for (int dy = 0; dy < size; ++dy)
            for (int dx = 0; dx < size; ++dx) {
                u8 v = 0;
                int sy0 = dy * MINI / size, sy1 = ((dy + 1) * MINI + size - 1) / size;
                int sx0 = dx * MINI / size, sx1 = ((dx + 1) * MINI + size - 1) / size;
                for (int sy = sy0; sy < sy1 && sy < MINI; ++sy)
                    for (int sx = sx0; sx < sx1 && sx < MINI; ++sx) {
                        u8 m = mini[sy * MINI + sx];
                        if (m > v) v = m;
                    }
                if (!v) continue;
                if (pass == 0) put_px(f, x + dx + 1, y + dy + 1, rgb(16, 16, 24));
                else put_px(f, x + dx, y + dy, v == 2 ? hazard : road);
            }
}

void draw_minimap_track(Frame& f, const Track& t, int x, int y) {
    dim_rect(f, x - 1, y - 1, MINI + 2, MINI + 2, 0.45f);
    draw_mini(f, t.mini, x, y, MINI, rgb(216, 216, 232), rgb(96, 176, 255));
    vec2 tan;
    vec2 s0 = track_point(t, 0, &tan);
    fill_rect(f, x + (int)(s0.x / 16) - 1, y + (int)(s0.y / 16) - 1, 2, 2, rgb(255, 64, 64));
}

static void mini_racers(Frame& f, const Race& r, int x, int y, int size, u32 tick) {
    f32 k = (f32)size / (f32)(MAP);
    for (int i = RACERS - 1; i >= 0; --i) {
        const Racer& q = r.racers[i];
        if (q.human || q.absent) continue;
        int px = x + (int)(q.pos.x * k), py = y + (int)(q.pos.y * k);
        fill_rect(f, px - 1, py - 1, 3, 3, rgb(16, 16, 24));
        fill_rect(f, px, py, 2, 2, k_chars[q.ch].color[0]);
    }
    if (r.ghost && r.ghost_n > 0) { // the ghost: a hollow white mark
        int gi = r.phase == RacePhase::Countdown ? 0 : (int)(r.race_time * 60.0f + 0.5f);
        if (gi < r.ghost_n) {
            int px = x + (int)(r.ghost[gi].x * k), py = y + (int)(r.ghost[gi].y * k);
            fill_rect(f, px - 1, py - 1, 3, 3, rgb(255, 255, 255));
            fill_rect(f, px, py, 1, 1, rgb(16, 16, 24));
        }
    }
    for (int i = 0; i < RACERS; ++i) {
        const Racer& q = r.racers[i];
        if (!q.human) continue;
        int px = x + (int)(q.pos.x * k), py = y + (int)(q.pos.y * k);
        bool on = (tick & 8) != 0;
        fill_rect(f, px - 2, py - 2, 5, 5, on ? rgb(255, 255, 255) : rgb(16, 16, 24));
        fill_rect(f, px - 1, py - 1, 3, 3, on ? k_chars[q.ch].color[0] : rgb(255, 232, 96));
    }
}

void draw_minimap(Frame& f, const Race& r, int x, int y, bool racers) {
    draw_minimap_track(f, r.track, x, y);
    if (racers) mini_racers(f, r, x, y, MINI, (u32)(r.clock * 60.0f));
}

// ---- HUD ------------------------------------------------------------------------------------
// The little the HUD remembers between frames, to animate changes: when the place last
// changed and when the item roulette landed. It resets itself when a new race starts.
namespace {
struct HudState {
    f32  clock = -1;
    int  place = -1;
    u32  place_tick = 0;
    bool rolling = false;
    int  item = IT_NONE;
    int  land_item = IT_NONE;
    u32  land_tick = 0;
};
static HudState s_huds[2]; // one per view
} // namespace

static void results_panel(Frame& f, const Race& r, u32 tick, const HudExtra& ex) {
    const Racer& p = r.racers[r.player];
    char buf[40];
    bool wide = f.w >= 360;
    int w = 232, h = 204;
    int x0 = wide ? 14 : f.w / 2 - w / 2, y0 = 8;
    draw_window(f, x0, y0, w, h, WIN_GLASS);
    draw_text_c(f, x0 + w / 2, y0 + 6, "RESULTS", rgb(255, 232, 96), 2);
    if (r.time_trial) {
        // alone against the clock: the laps, and how the run stands against the ghost
        const Character& c = k_chars[p.ch];
        fill_rect(f, x0 + w / 2 - 19, y0 + 28, 38, 38, c.color[0]);
        fill_rect(f, x0 + w / 2 - 17, y0 + 30, 34, 34, rgb(24, 24, 48));
        draw_prop_img(f, k_portrait[p.ch], x0 + w / 2 - 16, y0 + 31, 1);
        draw_text_c(f, x0 + w / 2, y0 + 70, c.name, rgb(255, 232, 96), 1);
        for (int i = 0; i < LAPS; ++i) {
            int y = y0 + 88 + i * 14;
            char lab[8] = "LAP 1";
            lab[4] = (char)('1' + i);
            draw_text(f, x0 + 56, y, lab, rgb(160, 200, 255), 1, true);
            ui_time(buf, p.lap_times[i]);
            bool best = p.lap_times[i] > 0 && p.lap_times[i] <= p.best_lap;
            draw_text_r(f, x0 + w - 56, y, buf, best ? rgb(120, 255, 144) : rgb(248, 248, 248), 1);
        }
        const char* verdict = ex.ghost_time <= 0 ? "THIS RUN IS NOW YOUR GHOST" : (p.finish_time < ex.ghost_time ? "YOU BEAT YOUR GHOST!" : "THE GHOST KEEPS ITS TIME");
        u32 vc = ex.ghost_time > 0 && p.finish_time < ex.ghost_time ? ((tick & 8) ? rgb(255, 255, 255) : rgb(255, 216, 64)) : rgb(200, 208, 232);
        draw_text_c(f, x0 + w / 2, y0 + 138, verdict, vc, 1);
    }
    for (int place = 0; place < RACERS && !r.time_trial; ++place) {
        int idx = -1;
        for (int i = 0; i < RACERS; ++i) if ((ex.frozen ? ex.place[i] : r.racers[i].place) == place) idx = i;
        if (idx < 0) continue;
        const Racer& k = r.racers[idx];
        int y = y0 + 26 + place * 17;
        const Character& c = k_chars[k.ch];
        if (k.human) {
            fill_rect(f, x0 + 4, y - 1, w - 8, 17, (tick & 16) ? rgb(72, 72, 16) : rgb(96, 80, 16));
            fill_rect(f, x0 + 4, y - 1, w - 8, 1, rgb(255, 216, 64));
            fill_rect(f, x0 + 4, y + 15, w - 8, 1, rgb(255, 216, 64));
        }
        u32 col = k.human ? ((tick & 16) ? rgb(255, 255, 255) : rgb(255, 232, 96)) : rgb(224, 224, 232);
        static const u32 medal[3] = {rgb(255, 216, 64), rgb(208, 224, 240), rgb(240, 152, 80)};
        draw_text(f, x0 + 8, y + 4, ui_ordinal(place), place < 3 ? medal[place] : rgb(176, 184, 208), 1, true);
        fill_rect(f, x0 + 30, y - 1, 18, 17, c.color[0]);
        fill_rect(f, x0 + 31, y, 16, 15, rgb(24, 24, 48));
        draw_img_half(f, k_portrait[k.ch], x0 + 31, y - 1);
        draw_text(f, x0 + 53, y + 4, c.name, col, 1, true);
        ui_time(buf, k.finished ? k.finish_time : 0);
        int tr = x0 + w - 8;
        if (ex.gp) {
            char pts[8] = "+";
            ui_int(pts + 1, ex.points[idx]);
            draw_text_r(f, tr, y + 4, pts, rgb(120, 255, 160), 1);
            tr -= 22;
        }
        draw_text_r(f, tr, y + 4, buf, col, 1);
    }
    int fy = y0 + 26 + RACERS * 17 + 3;
    draw_text(f, x0 + 8, fy, "BEST LAP", rgb(160, 200, 255), 1, true);
    ui_time(buf, p.best_lap);
    draw_text(f, x0 + 62, fy, buf, rgb(248, 248, 248), 1, true);
    if (ex.new_lap && (tick & 8)) draw_text(f, x0 + 110, fy, "NEW RECORD!", rgb(255, 96, 96), 1, true);
    else if (!ex.new_lap && ex.rec_lap > 0) {
        draw_text(f, x0 + 122, fy, "RECORD", rgb(120, 144, 200), 1, true);
        ui_time(buf, ex.rec_lap);
        draw_text_r(f, x0 + w - 8, fy, buf, rgb(176, 192, 232), 1);
    }
    draw_text(f, x0 + 8, fy + 10, "TIME", rgb(160, 200, 255), 1, true);
    ui_time(buf, p.finish_time);
    draw_text(f, x0 + 62, fy + 10, buf, rgb(248, 248, 248), 1, true);
    if (ex.new_time && (tick & 8)) draw_text(f, x0 + 110, fy + 10, "NEW RECORD!", rgb(255, 96, 96), 1, true);
    else if (!ex.new_time && ex.rec_time > 0) {
        draw_text(f, x0 + 122, fy + 10, "RECORD", rgb(120, 144, 200), 1, true);
        ui_time(buf, ex.rec_time);
        draw_text_r(f, x0 + w - 8, fy + 10, buf, rgb(176, 192, 232), 1);
    }
    int my = fy + 23;
    if (ex.menu_count > 0) {
        int total = 0;
        for (int i = 0; i < ex.menu_count; ++i) total += text_width(ex.menu[i]) + 26;
        int mx = x0 + w / 2 - (total - 14) / 2;
        for (int i = 0; i < ex.menu_count; ++i) {
            bool sel = i == ex.menu_sel;
            if (sel) draw_prop_img(f, k_cursor, mx + (int)((tick / 8) & 1), my - 2, 1);
            draw_text(f, mx + 12, my, ex.menu[i], sel ? rgb(255, 232, 96) : rgb(200, 208, 232), 1, true);
            mx += text_width(ex.menu[i]) + 26;
        }
    } else if (tick & 32) draw_text_c(f, x0 + w / 2, my, "PRESS ENTER", rgb(248, 248, 248), 1);

    if (wide) {
        // 16:9: the player's own card beside the table
        int sx = x0 + w + 6, sw = f.w - sx - 14;
        const Character& c = k_chars[p.ch];
        draw_window(f, sx, y0, sw, h, WIN_GLASS);
        int cx = sx + sw / 2;
        fill_rect(f, cx - 18, y0 + 8, 36, 36, c.color[0]);
        fill_rect(f, cx - 16, y0 + 10, 32, 32, rgb(24, 24, 48));
        draw_prop_img(f, k_portrait[p.ch], cx - 16, y0 + 10, 1);
        draw_text_c(f, cx, y0 + 49, c.name, c.color[0], 1);
        draw_text_c(f, cx, y0 + 59, c.vehicle, rgb(176, 200, 255), 1);
        const PropImg& badge = place_badge(p.place);
        draw_prop_img(f, badge, cx - badge.w, y0 + 74, 2);
        int ly = y0 + 128;
        auto row = [&](const char* label, int v) {
            draw_text(f, sx + 10, ly, label, rgb(160, 200, 255), 1, true);
            ui_int(buf, v);
            draw_text_r(f, sx + sw - 10, ly, buf, rgb(248, 248, 248), 1);
            ly += 11;
        };
        row("BOLTS", p.bolts);
        row("HITS TAKEN", p.stat_hits);
        row("FALLS", p.stat_falls);
        if (ex.gp) row("POINTS", ex.points[r.player]);
        draw_kart_icon(f, p.ch, (int)(tick / 6), cx - 16, y0 + h - 40, 1);
    }
}

// One player's HUD. `layout` 0 is the whole screen; the others are a split view's, pared down
// to what fits: 1 lap block top-left and place bottom-right, 2 lap block top-right (the inner
// corner belongs to the shared map), 3 place bottom-left (likewise).
static void hud_impl(Frame& f, const Race& r, int pi, int slot, u32 tick, const HudExtra* extra, int layout) {
    if (pi < 0) return;
    const Racer& p = r.racers[pi];
    char buf[40];
    const bool compact = layout != 0, low = f.h < 150;
    bool wide = f.w >= 360 && !compact;
    f32 view_angle = r.cam_angle;
    int seat = 0; // which player this is, 0..3
    for (int hu = 1; hu < r.humans; ++hu) if (pi == r.human_idx[hu]) { view_angle = r.cam_angles[hu]; seat = hu; }
    HudState& s = s_huds[slot & 1];
    bool rolling = p.roulette > 0 && p.item != IT_NONE;
    if (s.place < 0 || r.clock < s.clock) {
        s = HudState{};
        s.place = p.place;
        s.place_tick = tick - 1000;
        s.land_tick = tick - 1000;
        s.item = p.item;
        s.rolling = rolling;
    }
    s.clock = r.clock;
    if (p.place != s.place) {
        if (r.phase == RacePhase::Racing && r.race_time > 1.5f) s.place_tick = tick;
        s.place = p.place;
    }
    if ((s.rolling && !rolling && p.item != IT_NONE) || (!rolling && !s.rolling && p.item != IT_NONE && s.item == IT_NONE)) {
        s.land_tick = tick;
        s.land_item = p.item;
    }
    s.rolling = rolling;
    s.item = p.item;

    bool results = !compact && r.phase == RacePhase::Done && r.done_timer >= 2.5f && (!extra || extra->results);
    if (results) {
        HudExtra def;
        results_panel(f, r, tick, extra ? *extra : def);
        return;
    }

    int lap = p.lap < 1 ? 1 : (p.lap > LAPS ? LAPS : p.lap);
    static const PropImg* const bolt_frames[4] = {&k_bolt0, &k_bolt1, &k_bolt2, &k_bolt3};
    if (compact) {
        // lap over bolts, in one small block in a top corner
        int bx = layout == 2 ? f.w - 38 : 5;
        dim_rect(f, bx - 2, 3, 36, 31, 0.62f);
        buf[0] = (char)('0' + lap); buf[1] = 0;
        draw_text(f, bx + 2, 5, buf, rgb(255, 255, 255), 2, true);
        buf[0] = '/'; buf[1] = (char)('0' + LAPS); buf[2] = 0;
        draw_text(f, bx + 16, 11, buf, rgb(255, 232, 96), 1, true);
        draw_prop_img(f, *bolt_frames[(tick / 8) & 3], bx, 20, 1);
        buf[0] = (char)('0' + (p.bolts / 10) % 10); buf[1] = (char)('0' + p.bolts % 10); buf[2] = 0;
        draw_text(f, bx + 15, 23, buf, p.bolts >= 10 ? rgb(255, 224, 96) : rgb(248, 248, 248), 1, true);
        // whose view this is: a tag in the racer's own color
        fill_rect(f, bx - 2, 35, 17, 11, rgb(16, 16, 24));
        fill_rect(f, bx - 1, 36, 15, 9, shade(k_chars[p.ch].color[0], 0.55f));
        buf[0] = 'P'; buf[1] = (char)('1' + seat); buf[2] = 0;
        draw_text(f, bx + 1, 37, buf, rgb(255, 255, 255), 1, false);
    } else {
    // lap and time, top-left; the last lap's time pops in under them
    draw_text(f, 8, 13, "LAP", rgb(255, 232, 96), 1, true);
    buf[0] = (char)('0' + lap); buf[1] = 0;
    draw_text(f, 29, 6, buf, rgb(255, 255, 255), 2, true);
    buf[0] = '/'; buf[1] = (char)('0' + LAPS); buf[2] = 0;
    draw_text(f, 42, 13, buf, rgb(255, 232, 96), 1, true);
    ui_time(buf, p.finished ? p.finish_time : (r.race_time > 0 ? r.race_time : 0.001f));
    draw_text(f, 8, 24, buf, rgb(248, 248, 248), 1, true);
    if (p.lap >= 2 && p.last_lap > 0 && r.race_time - p.lap_start < 3.0f) {
        bool best = p.lap > 2 && p.last_lap <= p.best_lap;
        ui_time(buf, p.last_lap);
        draw_text(f, 8, 36, "LAP TIME", rgb(160, 216, 255), 1, true);
        draw_text(f, 8, 45, buf, best && (tick & 8) ? rgb(255, 232, 96) : rgb(248, 248, 248), 1, true);
        if (best && (tick & 8)) draw_text(f, 8, 55, "BEST LAP!", rgb(120, 255, 144), 1, true);
    }

    }
    // item slot, top-centre
    int ix = f.w / 2 - 12, iy = compact ? 4 : 6;
    fill_rect(f, ix + 3, iy + 3, 18, 18, rgb(24, 24, 56));
    for (int yy = 0; yy < 18; yy += 2) fill_rect(f, ix + 3, iy + 3 + yy, 18, 1, rgb(32, 40, 88));
    int shown = p.item;
    if (rolling) shown = 1 + (int)((tick / 3) % (IT_COUNT - 1));
    if (const PropImg* icon = item_icon(shown)) {
        int bob = (!rolling && tick - s.land_tick < 8) ? -1 : 0; // a little jump as it lands
        draw_prop_img(f, *icon, ix + 4, iy + 4 + bob, 1);
    }
    draw_prop_img(f, k_item_frame, ix, iy, 1);
    if (!rolling && p.item == IT_NITRO3 && p.item_uses > 0) {
        // triple nitro: a pip for each shot left
        for (int i = 0; i < 3; ++i) {
            int px = ix + 26 + i * 6;
            fill_rect(f, px, iy + 16, 5, 6, rgb(16, 16, 24));
            fill_rect(f, px + 1, iy + 17, 3, 4, i < p.item_uses ? rgb(255, 120, 48) : rgb(72, 72, 96));
        }
    }
    if (!rolling && p.item != IT_NONE && p.item == s.land_item && tick - s.land_tick < 100)
        draw_text_c(f, f.w / 2, iy + 27, item_name(p.item), (tick & 4) && tick - s.land_tick < 30 ? rgb(255, 255, 255) : rgb(255, 232, 96), 1);

    // bolts, bottom-left
    if (!compact) {
        draw_prop_img(f, *bolt_frames[(tick / 8) & 3], 6, f.h - 19, 1);
        buf[0] = 'X'; buf[1] = (char)('0' + (p.bolts / 10) % 10); buf[2] = (char)('0' + p.bolts % 10); buf[3] = 0;
        draw_text(f, 20, f.h - 17, buf, p.bolts >= 10 ? rgb(255, 224, 96) : rgb(248, 248, 248), 1, true);
    }

    // a time trial has no places: bottom-right is the gap to the ghost, in meters of road,
    // and the time to beat sits over the bolts
    if (r.time_trial && !compact) {
        if (r.ghost_time > 0) {
            draw_text(f, 6, f.h - 31, "BEST", rgb(160, 216, 255), 1, true);
            ui_time(buf, r.ghost_time);
            draw_text(f, 34, f.h - 31, buf, rgb(248, 248, 248), 1, true);
        }
        int gi = (int)(r.race_time * 60.0f + 0.5f);
        if (r.ghost && r.phase == RacePhase::Racing && !p.finished && gi < r.ghost_n) {
            int gap = (int)(p.total - r.ghost[gi].total);
            bool ahead = gap >= 0;
            buf[0] = ahead ? '+' : '-'; buf[1] = 0;
            char num[12];
            ui_int(num, ahead ? gap : -gap);
            ui_cat(buf, sizeof buf, num);
            ui_cat(buf, sizeof buf, "M");
            draw_text_r(f, f.w - 6, f.h - 31, "GHOST", rgb(160, 216, 255), 1);
            draw_text_r(f, f.w - 6, f.h - 21, buf, ahead ? rgb(120, 255, 144) : rgb(255, 112, 96), 2);
        }
    }
    // place, bottom-right: the badge hops when the place changes
    if (!r.time_trial) {
        const PropImg& badge = place_badge(p.place);
        u32 t = tick - s.place_tick;
        int hop = 0;
        if (t < 20) hop = (int)(9.0f * kv::sin(3.14159f * (f32)t / 20.0f));
        int bx = layout == 3 ? 5 : f.w - badge.w - (compact ? 4 : 6), by = f.h - badge.h - (compact ? 4 : 6) - hop;
        draw_prop_img(f, badge, bx, by, 1);
        if (t < 20 && (t & 2)) {
            // sparks off the corners
            int d = (int)t / 2;
            put_px(f, bx - 2 - d, by + 2 - d, rgb(255, 255, 255)); put_px(f, bx + badge.w + 1 + d, by + 2 - d, rgb(255, 255, 255));
            put_px(f, bx - 3 - d, by + 12, rgb(255, 232, 96)); put_px(f, bx + badge.w + 2 + d, by + 12, rgb(255, 232, 96));
        }
    }

    // minimap, top-right: half size in 4:3, three-quarter size in 16:9 (a split screen shares one)
    if (!compact) {
        int ms = wide ? 48 : 32;
        int mx = f.w - ms - 6, my = 6;
        dim_rect(f, mx - 2, my - 1, ms + 4, ms + 2, 0.62f);
        dim_rect(f, mx - 1, my - 2, ms + 2, ms + 4, 0.62f);
        draw_mini(f, r.track.mini, mx, my, ms, rgb(232, 232, 240), rgb(96, 176, 255));
        mini_racers(f, r, mx, my, ms, tick);
    }

    // 16:9: the running order down the left edge
    if (wide && r.phase != RacePhase::Countdown && !r.time_trial) {
        for (int place = 0; place < RACERS; ++place) {
            int idx = 0;
            for (int i = 0; i < RACERS; ++i) if (r.racers[i].place == place) idx = i;
            const Racer& k = r.racers[idx];
            int x = 6, y = 62 + place * 17;
            u32 rim = k.human ? ((tick & 8) ? rgb(255, 255, 255) : rgb(255, 216, 64)) : rgb(16, 16, 24);
            fill_rect(f, x - 1, y - 1, 18, 17, rim);
            fill_rect(f, x, y, 16, 15, shade(k_chars[k.ch].color[0], 0.45f));
            draw_img_half(f, k_portrait[k.ch], x, y - 1);
            buf[0] = (char)('1' + place); buf[1] = 0;
            draw_text(f, x + 20, y + 4, buf, k.human ? rgb(255, 232, 96) : rgb(232, 232, 240), 1, true);
        }
    }

    // threat warnings: big things closing in from the side, rockets locked on, cannon fire
    {
        f32 fx = kv::cos(view_angle), fy = kv::sin(view_angle);
        bool warn_l = false, warn_r = false, warn_ahead = false;
        const SetPiece& sp = r.track.sp;
        for (u32 i = 0; i < sp.bodies.len; ++i) {
            const SetBody& b = sp.bodies[i];
            if (!b.solid) continue;
            vec2 d = b.pos - p.pos;
            f32 dist2 = length_sq(d);
            if (dist2 > 170.0f * 170.0f) continue;
            vec2 bv{kv::cos(b.heading), kv::sin(b.heading)};
            bool closing = dot(bv, d) < 0.0f || b.img == SPI_DINO;
            if (!closing) continue;
            f32 lat = -d.x * fy + d.y * fx, fwd = d.x * fx + d.y * fy;
            if (fwd > 40.0f && kv::fabs(lat) < 40.0f) warn_ahead = true;
            else if (lat < 0) warn_l = true;
            else warn_r = true;
        }
        bool rocket = false;
        for (u32 i = 0; i < r.bombs.len; ++i) {
            const Projectile& b = r.bombs[i];
            if (b.kind == PK_ROCKET && b.target == pi) rocket = true;
            if ((b.kind == PK_CANNON || b.kind == PK_WAFFLE) && length_sq(b.aim - p.pos) < 50.0f * 50.0f) warn_ahead = true;
        }
        bool on = (tick & 8) != 0;
        u32 wc = on ? rgb(255, 64, 48) : rgb(255, 232, 96);
        int lx = wide ? 36 : (compact ? 3 : 6), wy = f.h / 2 - 4;
        if (warn_l) { draw_text(f, lx, wy, "<", wc, 3, true); draw_text(f, lx + 20, wy + 3, "!", wc, 2, true); }
        if (warn_r) { draw_text(f, f.w - 22, wy, ">", wc, 3, true); draw_text(f, f.w - 36, wy + 3, "!", wc, 2, true); }
        if (warn_ahead && on) draw_text_c(f, f.w / 2, low ? 34 : 76, "!", wc, 3);
        if (rocket && on) draw_text_c(f, f.w / 2, low ? f.h - 30 : f.h - 44, "ROCKET!", rgb(255, 64, 48), compact ? 1 : 2);
    }

    // wrong way, and the tow of a draft
    if (p.wrong_way > 1.0f && r.phase == RacePhase::Racing) {
        int ay = low ? 30 : 78;
        if (tick & 16) draw_prop_img(f, k_arrow_wrongway, f.w / 2 - 12, ay, 1);
        draw_text_c(f, f.w / 2, ay + 28, "WRONG WAY", (tick & 8) ? rgb(255, 64, 48) : rgb(255, 232, 96), compact ? 1 : 2);
    } else if (p.draft > 1.0f && r.phase == RacePhase::Racing && !p.finished) {
        u32 dc = (tick & 4) ? rgb(160, 240, 255) : rgb(255, 255, 255);
        int n = (int)((tick / 4) % 3);
        draw_text_c(f, f.w / 2, f.h - 14, "DRAFT", dc, 1);
        for (int i = 0; i <= n; ++i) {
            draw_text(f, f.w / 2 - 26 - i * 6, f.h - 14, ">", dc, 1, true);
            draw_text(f, f.w / 2 + 21 + i * 6, f.h - 14, "<", dc, 1, true);
        }
    }

    // the start lights, then GO; banners
    if (r.phase == RacePhase::Countdown) {
        int n = (int)r.clock; // 0, 1, 2: one more red each second
        int drop = r.clock < 0.25f ? (int)((0.25f - r.clock) * 160.0f) : 0; // the gantry drops in
        char c[2] = {(char)('3' - (n > 2 ? 2 : n)), 0};
        f32 ph = r.clock - (f32)n;
        if (low) { // a short view: small lights under the item slot, a smaller count
            draw_lights(f, n + 1 > 3 ? 3 : n + 1, f.w / 2 - 20, 32 - drop / 2, 1);
            if (ph < 0.75f) draw_text_c(f, f.w / 2, 48, c, rgb(255, 232, 96), 3);
        } else {
            draw_lights(f, n + 1 > 3 ? 3 : n + 1, f.w / 2 - 40, 40 - drop, 2);
            if (ph < 0.75f) draw_text_c(f, f.w / 2, 86, c, rgb(255, 232, 96), ph < 0.08f ? 5 : 4);
        }
    } else if (r.phase == RacePhase::Racing && r.race_time < 1.0f) {
        int rise = r.race_time > 0.7f ? (int)((r.race_time - 0.7f) * 300.0f) : 0; // and lifts away
        if (low) draw_lights(f, 4, f.w / 2 - 20, 32 - rise / 2, 1); else draw_lights(f, 4, f.w / 2 - 40, 40 - rise, 2);
        if (r.race_time < 0.9f) draw_text_c(f, f.w / 2, low ? 48 : 84, "GO!", (tick & 4) ? rgb(96, 255, 128) : rgb(255, 255, 255), compact ? 3 : 5);
    }
    if (compact) {
        // a split view's banners are its own player's, and small enough for half a screen
        if (r.final_lap_banner > 0 && p.lap == LAPS && !p.finished) {
            u32 c = (tick & 8) ? rgb(255, 96, 96) : rgb(255, 232, 96);
            int bw = text_width("FINAL LAP", 1) + 14, by = low ? 34 : 46;
            dim_rect(f, f.w / 2 - bw / 2, by, bw, 13, 0.45f);
            fill_rect(f, f.w / 2 - bw / 2, by, bw, 1, c);
            fill_rect(f, f.w / 2 - bw / 2, by + 12, bw, 1, c);
            draw_text_c(f, f.w / 2, by + 3, "FINAL LAP", c, 1);
        }
        if (p.finished && r.race_time - p.finish_time < 4.0f && !(r.phase == RacePhase::Done && r.done_timer >= 2.5f))
            draw_text_c(f, f.w / 2, low ? 36 : 56, "FINISH!", (tick & 8) ? rgb(255, 255, 255) : rgb(255, 216, 64), low ? 3 : 2);
        return;
    }
    if (r.final_lap_banner > 0) {
        u32 c = (tick & 8) ? rgb(255, 96, 96) : rgb(255, 232, 96);
        int bw = text_width("FINAL LAP", 2) + 20;
        dim_rect(f, f.w / 2 - bw / 2, 46, bw, 20, 0.45f);
        fill_rect(f, f.w / 2 - bw / 2, 46, bw, 1, c);
        fill_rect(f, f.w / 2 - bw / 2, 65, bw, 1, c);
        draw_text_c(f, f.w / 2, 49, "FINAL LAP", c, 2);
    }
    if (r.phase == RacePhase::Done && r.done_timer < 2.5f) {
        int sc = f.w >= 360 ? 5 : 4;
        draw_text_c(f, f.w / 2, 56, "FINISH!", (tick & 8) ? rgb(255, 255, 255) : rgb(255, 216, 64), sc);
        const PropImg& badge = place_badge(p.place);
        if (r.done_timer > 0.5f) draw_prop_img(f, badge, f.w / 2 - badge.w, 100, 2);
    }
}

void draw_hud_ex(Frame& f, const Race& r, u32 tick, const HudExtra* extra) { hud_impl(f, r, r.player, 0, tick, extra, 0); }
void draw_hud(Frame& f, const Race& r, u32 tick) { draw_hud_ex(f, r, tick, nullptr); }

// The race as it goes to the screen: one view, or with two players a split one. Each half is
// drawn into `scratch` (room for a whole frame) at its own size, with its own small HUD, and
// copied into place; the map is shared, set on the divider where neither view needs the room.
void draw_race_screen(Frame& f, const Race& r, u32 tick, const HudExtra* extra, bool split_rows, u32* scratch) {
    if (r.humans < 2 || !scratch) { draw_world(f, r); draw_hud_ex(f, r, tick, extra); return; }
    // two players share the screen the way the options say; three or four take a quarter each
    const bool grid = r.humans > 2;
    const bool rows = grid || split_rows; // short views: the low camera
    Frame v;
    v.px = scratch;
    v.w = (grid || !split_rows) ? f.w / 2 - 1 : f.w;
    v.h = rows ? f.h / 2 - 1 : f.h;
    for (int i = 0; i < r.humans; ++i) {
        draw_world_view(v, r, i, rows ? 32 : 70, rows ? 20.0f : 40.0f);
        // each view's lap block and place sit on its outer side; the inner corners are the map's
        int layout = grid ? ((i & 1) ? 2 : 3) : (split_rows ? 3 : 1 + i);
        hud_impl(v, r, r.human_idx[i], i, tick, nullptr, layout);
        int ox = grid ? (i & 1) * (f.w / 2 + 1) : (split_rows ? 0 : i * (f.w / 2 + 1));
        int oy = grid ? (i >> 1) * (f.h / 2 + 1) : (split_rows ? i * (f.h / 2 + 1) : 0);
        for (int y = 0; y < v.h; ++y) {
            const u32* src = v.px + y * v.w;
            u32* dst = f.px + (oy + y) * f.w + ox;
            for (int x = 0; x < v.w; ++x) dst[x] = src[x];
        }
    }
    const u32 bar = rgb(8, 8, 16);
    if (grid || split_rows) fill_rect(f, 0, f.h / 2 - 1, f.w, 2, bar);
    if (grid || !split_rows) fill_rect(f, f.w / 2 - 1, 0, 2, f.h, bar);
    if (r.humans == 3) {
        // the spare quarter: the map, large, on a quiet panel
        int qx = f.w / 2 + 1, qy = f.h / 2 + 1, qw = f.w / 2 - 1, qh = f.h / 2 - 1;
        fill_rect(f, qx, qy, qw, qh, rgb(16, 20, 48));
        for (int y = qy; y < qy + qh; y += 2) fill_rect(f, qx, y, qw, 1, rgb(20, 26, 60));
        const int ms = 96;
        int mx = qx + qw / 2 - ms / 2, my = qy + qh / 2 - ms / 2;
        draw_mini(f, r.track.mini, mx, my, ms, rgb(232, 232, 240), rgb(96, 176, 255));
        mini_racers(f, r, mx, my, ms, tick);
    } else {
        const int ms = 32;
        int mx = (r.humans == 2 && split_rows) ? f.w - ms - 5 : f.w / 2 - ms / 2;
        int my = r.humans == 4 ? f.h / 2 - ms / 2 : (split_rows ? f.h / 2 - ms / 2 : 5);
        fill_rect(f, mx - 3, my - 3, ms + 6, ms + 6, bar);
        fill_rect(f, mx - 2, my - 2, ms + 4, ms + 4, rgb(24, 24, 56));
        draw_mini(f, r.track.mini, mx, my, ms, rgb(232, 232, 240), rgb(96, 176, 255));
        mini_racers(f, r, mx, my, ms, tick);
    }
    if (r.phase == RacePhase::Done && r.done_timer >= 2.5f && (!extra || extra->results)) {
        HudExtra def;
        results_panel(f, r, tick, extra ? *extra : def);
    }
}

} // namespace sr
