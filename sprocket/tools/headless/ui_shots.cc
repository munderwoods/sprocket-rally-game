// Renders every menu screen, the HUD's results panel and the screen transitions to PPM
// files, in both aspects, without a window: the menus are drawn from plain view structs
// (src/ui.h), so this fills them with the game's real data and dumps the framebuffer.
//
//   ui_shots <out-dir>        writes <out-dir>/<screen>_43.ppm and <screen>_169.ppm
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

static u32 g_fb[FB_MAXW * FB_H];
static const char* g_dir = ".";

static void save(const Frame& f, const char* name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s_%s.ppm", g_dir, name, f.w == FB_W43 ? "43" : "169");
    FILE* o = fopen(path, "wb");
    if (!o) { perror(path); return; }
    fprintf(o, "P6\n%d %d\n255\n", f.w, f.h);
    for (int i = 0; i < f.w * f.h; ++i) { u8 c[3] = {(u8)(f.px[i] & 255), (u8)((f.px[i] >> 8) & 255), (u8)((f.px[i] >> 16) & 255)}; fwrite(c, 1, 3, o); }
    fclose(o);
}

static u8 g_mini[LIST_MAX][MINI * MINI];
static int g_len[LIST_MAX];
static const char* g_theme[LIST_MAX];
static GenTrack g_gen;
static Records g_rec;

static TrackCard card(int i) {
    TrackCard c;
    const TrackDef& d = i < k_track_count ? k_tracks[i] : g_gen.def;
    c.name = d.name; c.blurb = d.blurb; c.theme = g_theme[i]; c.mini = g_mini[i]; c.length = g_len[i];
    if (i < k_track_count && i < REC_TRACKS) { c.best_time = g_rec.best_time[i]; c.best_lap = g_rec.best_lap[i]; }
    return c;
}

int main(int argc, char** argv) {
    if (argc > 1) g_dir = argv[1];
    Allocator* a = mem_general();
    track_generate(3, g_gen);
    for (int i = 0; i <= k_track_count; ++i) {
        static Track t;
        track_build(t, i < k_track_count ? k_tracks[i] : g_gen.def, a);
        memcpy(g_mini[i], t.mini, sizeof t.mini);
        g_len[i] = (int)t.length; g_theme[i] = t.theme->name;
        track_free(t);
    }
    // records: a round trip through a file in the output directory
    {
        Records w;
        w.best_time[0] = 95.32f; w.best_lap[0] = 30.11f; w.best_time[4] = 121.5f; w.best_lap[4] = 38.02f;
        w.cup_rank[0][1] = 1; w.cup_rank[1][1] = 4;
        char path[512];
        snprintf(path, sizeof path, "%s/records_test.txt", g_dir);
        bool ok = records_save(w, path) && records_load(g_rec, path);
        ok = ok && g_rec.best_time[0] > 95.3f && g_rec.best_time[0] < 95.34f && g_rec.cup_rank[1][1] == 4 && g_rec.best_lap[4] > 38.0f;
        printf("records round trip: %s\n", ok ? "ok" : "FAILED");
    }
    // a race run to its end, for the panels over the world
    static Race race;
    race_init(race, k_tracks[2], 3, false, a);
    race.autopilot = true;
    for (int i = 0; i < 60 * 200 && !(race.phase == RacePhase::Done && race.done_timer > 3.0f); ++i) race_tick(race, Controls{}, 1.0f / 60.0f);

    for (int wide = 0; wide < 2; ++wide) {
        Frame f; f.px = g_fb; f.w = wide ? FB_W169 : FB_W43; f.h = FB_H;
        u32 tick = 200;
        // title
        draw_world(f, race); draw_title_overlay(f, tick + 40); save(f, "01_title");
        // mode select
        {
            ListView v;
            v.items[0] = "GRAND PRIX"; v.items[1] = "SINGLE RACE"; v.items[2] = "OPTIONS"; v.count = 3; v.sel = 0;
            v.hints[0] = "FOUR RACES FOR A TROPHY. POINTS FOR EVERY PLACE."; v.hints[1] = "ONE RACE ON ANY TRACK, OR A BRAND NEW ONE."; v.hints[2] = "PICTURE AND SOUND.";
            v.title = nullptr;
            draw_list_screen(f, tick, v, 0); save(f, "02_mode");
        }
        {
            ListView v;
            v.title = "DIFFICULTY"; v.items[0] = "EASY"; v.items[1] = "NORMAL"; v.items[2] = "HARD"; v.count = 3; v.sel = 1; v.style = WIN_RED;
            v.hints[1] = "RIVALS RACE HARD AND USE THEIR ITEMS WELL.";
            draw_list_screen(f, tick, v, 7); save(f, "03_difficulty");
        }
        {
            ListView v;
            v.title = "OPTIONS"; v.items[0] = "ASPECT"; v.items[1] = "CRT SHADER"; v.items[2] = "MUSIC TEST"; v.items[3] = "BACK"; v.count = 4; v.sel = 1;
            v.values[0] = "4:3  256X224"; v.values[1] = "LOTTES SHADOW MASK"; v.values[2] = "03 FROST"; v.style = WIN_TEAL;
            v.hints[1] = "LEFT/RIGHT CHANGES IT. Q/E DO THE SAME ANYWHERE.";
            v.footer = "LEFT/RIGHT CHANGE   ESC BACK";
            draw_list_screen(f, tick, v, 6); save(f, "04_options");
        }
        draw_racer_select(f, tick, 3); save(f, "05_racer");
        draw_racer_select(f, tick + 37, 7); save(f, "05_racer_b");
        {
            TrackSelectView v;
            for (int i = 0; i < k_track_count; ++i) v.names[i] = k_tracks[i].name;
            v.names[k_track_count] = "RANDOM"; v.count = k_track_count + 1;
            v.sel = 4; v.card = card(4);
            draw_track_select(f, tick, v); save(f, "06_track");
            v.sel = k_track_count; v.card = card(k_track_count); v.random = true; v.seed = 1234;
            draw_track_select(f, tick, v); save(f, "06_track_random");
        }
        {
            CupSelectView v;
            for (int c = 0; c < CUPS; ++c)
                for (int i = 0; i < CUP_RACES; ++i) v.tracks[c][i] = card(k_cup_tracks[c][i] < 0 ? k_track_count : k_cup_tracks[c][i] % k_track_count);
            v.tracks[2][3].mini = nullptr; // one still being built
            v.best_rank[0] = 1; v.best_rank[1] = 0; v.best_rank[2] = 5; v.seed = 4821;
            v.sel = 0; draw_cup_select(f, tick, v); save(f, "07_cup");
            v.sel = 2; draw_cup_select(f, tick + 9, v); save(f, "07_cup_b");
        }
        {
            StandingsView v;
            v.cup = 0; v.race = 2; v.player_ch = 3;
            int pts[8] = {18, 14, 10, 9, 8, 6, 5, 2};
            for (int i = 0; i < 8; ++i) { v.ch[i] = (i * 3) % 8; v.shown[i] = pts[i]; v.gain[i] = k_points[(i + 3) % 8]; v.slot[i] = (f32)i; }
            v.counting = true; v.next = card(4);
            draw_standings(f, tick, v); save(f, "08_standings");
            for (int i = 0; i < 8; ++i) { v.shown[i] += v.gain[i]; v.gain[i] = 0; }
            v.slot[3] = 1.4f; v.slot[1] = 2.6f; v.counting = false;
            draw_standings(f, tick + 32, v); save(f, "08_standings_b");
        }
        {
            CeremonyView v;
            v.cup = 1; v.diff = 2;
            int pts[8] = {36, 31, 27, 22, 18, 12, 9, 5};
            for (int i = 0; i < 8; ++i) { v.ch[i] = (i * 5 + 3) % 8; v.pts[i] = pts[i]; }
            v.player_rank = 1; v.t = 70; draw_ceremony(f, tick, v); save(f, "09_ceremony_a");
            v.t = 400; draw_ceremony(f, tick + 40, v); save(f, "09_ceremony");
            v.player_rank = 5; v.t = 520; draw_ceremony(f, tick, v); save(f, "09_ceremony_b");
        }
        // pause and results over the race
        draw_world(f, race); draw_pause(f, tick, 1, false); save(f, "10_pause");
        {
            HudExtra ex;
            ex.menu[0] = "RETRY"; ex.menu[1] = "NEXT"; ex.menu[2] = "QUIT"; ex.menu_count = 3; ex.menu_sel = 1;
            ex.new_lap = true; ex.rec_time = 61.2f; ex.rec_lap = 19.5f;
            draw_world(f, race); draw_hud_ex(f, race, tick + 8, &ex); save(f, "11_results");
            HudExtra gp;
            gp.gp = true;
            for (int i = 0; i < RACERS; ++i) gp.points[i] = k_points[race.racers[i].place];
            draw_world(f, race); draw_hud_ex(f, race, tick + 32, &gp); save(f, "11_results_gp");
        }
        // transitions
        draw_racer_select(f, tick, 2); fx_mosaic(f, 0.35f); save(f, "12_mosaic_a");
        draw_racer_select(f, tick, 2); fx_mosaic(f, 0.7f); save(f, "12_mosaic_b");
        draw_world(f, race); fx_iris(f, 0.55f, f.w / 2, 150); save(f, "12_iris");
        draw_world(f, race); fx_fade(f, 0.5f); save(f, "12_fade");
    }
    return 0;
}
