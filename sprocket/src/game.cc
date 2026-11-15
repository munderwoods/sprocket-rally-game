// Sprocket Rally's module: input, menus, the race, and presentation.
//
// Presentation: the framebuffer (256x224, or 400x224 in 16:9) is uploaded once per tick to
// one texture, drawn as one screen-space sprite letterboxed to the chosen aspect, through
// the sprocket_crt shader (a selection of libretro CRT shaders, or plain sharp pixels).
#include "korvid/sprocket/module.h"
#include "common.h"
#include "ui.h"
#include "korvid/core/arena.h"
#include "korvid/core/engine.h"
#include "korvid/core/log.h"
#include "korvid/frameloop/frameloop.h"
#include "korvid/platform/io.h"
#include "korvid/platform_hid/events.h"
#include "korvid/platform_hid/host_window.h"
#include "korvid/platform_hid/input.h"
#include "korvid/render/material.h"
#include "korvid/render/renderer.h"
#include "korvid/sprite/sprite.h"
#include "korvid/sprite/transform2.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

namespace kv {
namespace {

using namespace sr;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

const char* const k_crt_names[] = {
    "SHARP PIXELS", "LOTTES APERTURE", "LOTTES SHADOW MASK", "LOTTES FAST", "CRT APERTURE", "CRT EASYMODE",
    "CRT-GEOM", "GUEST GDV-MINI", "CRT CONSUMER", "HYLLIAN CRT", "ZFAST-CRT", "CRT-PI", "CRT-CALIGARI", "CRT-MATTIAS",
};
constexpr int CRT_MODES = (int)(sizeof k_crt_names / sizeof k_crt_names[0]);

enum class State : u8 { Title, Mode, Players, Devices, Difficulty, Options, Controls, SelectRacer, SelectTrack, SelectCup, Racing, Paused, PauseOptions, Standings, Ceremony };

// What to do at the dark middle of a screen transition, before the next screen shows.
enum class Act : u8 { None, ToTitle, EnterTrack, EnterCup, StartRace, NextCupRace, Standings, Ceremony };
enum class Fx : u8 { Mosaic, Fade, Iris };
struct Transition {
    bool  active = false, out = true;
    int   t = 0, frames = 12;
    Fx    fx = Fx::Mosaic;
    State to = State::Title;
    Act   act = Act::None;
};

// Minimap slots: the hand-made tracks, then track select's RANDOM, then the random cup's four.
constexpr int MINI_RANDOM = REC_TRACKS;
constexpr int MINI_CUP = REC_TRACKS + 1;
constexpr int MINI_SLOTS = MINI_CUP + CUP_RACES;

// What the menus keep of a generated track once its GenTrack has been reused.
struct GenInfo { char name[24] = {}; char blurb[64] = {}; int theme = 0; };

struct GrandPrix {
    bool active = false;
    bool assisted = false;        // the autopilot drove: no cup record
    int  cup = 0, race = 0;       // race: 0..3, the one being run
    int  points[RACERS] = {};     // by character
    int  last_place[RACERS] = {}; // by character: the placing in the last race
};

struct Game {
    Array<u32> fb;
    Frame      frame;
    Race       race;
    Track      preview;           // scratch: built to take a track's minimap and length
    State      state = State::Title;
    Transition tr;
    int        sel_mode = 0, sel_diff = 1, sel_char = 0, sel_track = 0, sel_cup = 0, sel_opt = 0, sel_pause = 0, sel_result = 1;
    int        attract_track = 0;
    bool       attract_ok = false; // g.race is the attract race
    u32        title_t = 0;
    u32        rand_seed = 1, cup_seed = 0;
    GenTrack   gen{};             // scratch: generated tracks being previewed
    GenTrack   race_gen{};        // the generated track being raced (its TrackDef points in here)
    GenInfo    gen_info[1 + CUP_RACES]; // RANDOM, then the random cup's four
    // minimaps of every track, built once
    u8         mini[MINI_SLOTS][MINI * MINI] = {};
    int        mini_len[MINI_SLOTS] = {};
    bool       mini_ok[MINI_SLOTS] = {};
    u32        mini_seed[MINI_SLOTS] = {};
    // the race in hand
    int        cur_track = -1;    // k_tracks index, or -1 for a generated track
    bool       done_seen = false;
    HudExtra   hud;
    GrandPrix  gp;
    StandingsView sv;
    f32        st_target[RACERS] = {};
    u32        st_t = 0;
    bool       st_sorting = false;
    CeremonyView cv;
    Records    rec;
    int        music_test = -1, music_sel = 0;
    char       opt_music[32] = {};
    int        song = -2;
    bool       wide = false;
    int        crt = 1;
    f32        osd = 0;
    char       osd_text[48] = {};
    bool       fullscreen = false;
    bool       srgb = false;
    bool       started = false;
    u32        tick = 0;
    Handle     tex, mat, screen, backing;
    f32        last_params[8] = {};
    f32        show_w = 0, show_h = 0;  // the picture's size in the window, for the shader
    bool       frame_dirty = false;     // a tick has drawn since the picture was last sent to the GPU
    bool       hit[(u32)Key::Count] = {}; // keys pressed this tick (from KeyDown events, so no tap is missed)
    u32        hit_pad = 0;
    u8         stick_held = 0;   // which ways the left stick was pushed last tick (menu navigation)
    // two players
    int        players = 1;      // how many are playing (1..4)
    int        pick = 0;         // racer select: which player is choosing
    int        sel_chars[4] = {0, 1, 2, 3}; // players two onward ([0] unused: player one's is sel_char)
    int        sel_players = 0, sel_popt = 0, sel_dev = 0;
    char       dev_text[4][28] = {};
    bool       time_trial = false;
    // time trial: the run being driven, and the best one on the track in hand (its ghost)
    Array<GhostSample> ghost_rec, ghost_best;
    int        ghost_n = 0, ghost_ch = 0, ghost_track = -1;
    f32        ghost_time = 0;
    bool       split_rows = false; // the split: side by side, or one above the other
    Array<u32> fb2;              // a view is drawn here, then copied into its half
    u32        hit_pads[KV_MAX_GAMEPADS] = {}; // buttons pressed this tick, by pad
    // settings kept on disk: bindings, the window's mode and monitor (and the picture options above)
    Settings   set;
    char       opt_mon[16] = {};
    int        ctl_row = 0, ctl_col = 0;
    bool       ctl_capture = false, ctl_in_race = false;
    void*      native = nullptr;  // the window's own handle, for the display options
    // the two asset files, and what they looked like when last loaded: a change is reloaded
    char       art_path[512] = {}, bank_path[512] = {};
    i64        art_stamp = 0, bank_stamp = 0;
};

// menu.cc's default bindings are written as numbers: hold them to the engine's enum
static_assert((int)Key::A == 1 && (int)Key::Z == 26 && (int)Key::Space == 50 && (int)Key::Left == 54 && (int)Key::Down == 57 &&
              (int)Key::LShift == 58 && (int)Key::RCtrl == 61, "settings_default_binds: Key numbers moved");
static_assert((int)PadButton::South == 0 && (int)PadButton::West == 2 && (int)PadButton::LShoulder == 9 && (int)PadButton::RShoulder == 10,
              "settings_default_binds: PadButton numbers moved");

// ---- input ----------------------------------------------------------------------------------
struct Keys {
    const Input* in;
    Game* g;
    bool down(Key k) const { return in->sampled.down(k); }
    bool pressed(Key k) const { return g->hit[(u32)k]; }
    const GamepadState* pad() const { return in->sampled.pad(); }
    bool pad_down(PadButton b) const { const GamepadState* p = pad(); return p && p->down(b); }
    bool pad_pressed(PadButton b) const { return (g->hit_pad >> (u32)b) & 1u; }
    bool confirm() const { return pressed(Key::Enter) || pressed(Key::Space) || pad_pressed(PadButton::South) || pad_pressed(PadButton::Start); }
    bool back() const { return pressed(Key::Escape) || pad_pressed(PadButton::East); }
    bool left() const { return pressed(Key::Left) || pad_pressed(PadButton::DpadLeft); }
    bool right() const { return pressed(Key::Right) || pad_pressed(PadButton::DpadRight); }
    bool up() const { return pressed(Key::Up) || pad_pressed(PadButton::DpadUp); }
    bool dn() const { return pressed(Key::Down) || pad_pressed(PadButton::DpadDown); }
};

static void pad_controls(Controls& c, const GamepadState& p, u32 pressed, const u8* bind) {
    auto hit = [&](int i) { return ((pressed >> bind[i]) & 1u) != 0; };
    auto dn = [&](int i) { return ((p.buttons >> bind[i]) & 1u) != 0; };
    f32 ax = p.axes[(u32)PadAxis::LeftX];
    if (kv::fabs(ax) > 0.2f) c.steer = clampf(c.steer + ax, -1.0f, 1.0f);
    if (p.down(PadButton::DpadLeft)) c.steer = -1;
    if (p.down(PadButton::DpadRight)) c.steer = 1;
    c.accel = c.accel || dn(0) || p.axes[(u32)PadAxis::RightTrigger] > 0.3f;
    c.brake = c.brake || dn(1) || p.axes[(u32)PadAxis::LeftTrigger] > 0.3f;
    c.hop_pressed = c.hop_pressed || hit(2);
    c.hop = c.hop || c.hop_pressed || dn(2);
    c.item_pressed = c.item_pressed || hit(3);
}

static void key_controls(Controls& c, const Keys& k, const u16* bind) {
    auto dn = [&](int i) { return bind[i] && bind[i] < (u16)Key::Count && k.down((Key)bind[i]); };
    auto hit = [&](int i) { return bind[i] && bind[i] < (u16)Key::Count && k.pressed((Key)bind[i]); };
    if (dn(0)) c.steer -= 1;
    if (dn(1)) c.steer += 1;
    c.accel = c.accel || dn(2);
    c.brake = c.brake || dn(3);
    c.hop_pressed = c.hop_pressed || hit(4);
    c.hop = c.hop || c.hop_pressed || dn(4);
    c.item_pressed = c.item_pressed || hit(5);
}

// `who`: 0 the only player (their own keys, player one's as well, and every real pad), else
// 1..4 of `of` players sharing the screen, each driving with what the settings say:
//   automatic: keys for players one and two, and a pad if there are enough. Pads go to the
//     last automatic players first, since three and four have nothing else; a keyboard that
//     poses as a gamepad is never handed out this way.
//   keyboard: that player's keys and nothing else.
//   a named pad: that pad and nothing else (this is how an analog keyboard's pad mode is used).
Controls race_controls(const Keys& k, int who, int of = 1) {
    Controls c;
    const Settings& set = k.g->set;
    const GamepadState* pads = k.in->sampled.pads;
    int all[KV_MAX_GAMEPADS], nall = 0; // every connected pad, in slot order: what "pad n" counts
    for (u32 i = 0; i < KV_MAX_GAMEPADS; ++i) if (pads[i].connected) all[nall++] = (int)i;
    if (who == 0) {
        key_controls(c, k, set.keys[0]); key_controls(c, k, set.keys[1]);
        for (int i = 0; i < nall; ++i) if (!pad_is_keyboard(pads[all[i]].native)) pad_controls(c, pads[all[i]], k.g->hit_pads[all[i]], set.pad);
        c.steer = clampf(c.steer, -1.0f, 1.0f);
        return c;
    }
    int dev = set.dev[who - 1];
    if (dev >= 2 && dev - 2 >= nall) dev = 0; // the pad it named is not there: fall back
    if (dev == 1 && who > 2) dev = 0;         // only players one and two have keys
    if (dev >= 2) { int s = all[dev - 2]; pad_controls(c, pads[s], k.g->hit_pads[s], set.pad); return c; }
    if (who <= 2) key_controls(c, k, set.keys[who]);
    c.steer = clampf(c.steer, -1.0f, 1.0f);
    if (dev == 1) return c;
    // automatic: the pads nobody named, among the players who left it to the game
    bool claimed[KV_MAX_GAMEPADS] = {};
    int autos = 0, my_rank = 0;
    for (int p = 0; p < of; ++p) {
        int d = set.dev[p];
        if (d >= 2 && d - 2 < nall) { claimed[d - 2] = true; continue; }
        if (d == 1 && p < 2) continue;
        if (p == who - 1) my_rank = autos;
        ++autos;
    }
    int free_pads[KV_MAX_GAMEPADS], nfree = 0;
    for (int i = 0; i < nall; ++i) if (!claimed[i] && !pad_is_keyboard(pads[all[i]].native)) free_pads[nfree++] = all[i];
    int j = my_rank - (autos - nfree);
    if (j >= 0 && j < nfree) pad_controls(c, pads[free_pads[j]], k.g->hit_pads[free_pads[j]], set.pad);
    return c;
}

// ---- tracks ---------------------------------------------------------------------------------
int hand_tracks() { return k_track_count < REC_TRACKS ? k_track_count : REC_TRACKS; }

// The four seeds of the random cup, from its one seed.
u32 cup_race_seed(u32 cup_seed, int i) { return hash2((i32)cup_seed, i + 1) % 99999u + 1u; }

void start_attract(Game& g, Allocator* alloc) {
    race_init(g.race, k_tracks[g.attract_track % k_track_count], 0, true, alloc);
    g.attract_ok = true;
}

// Builds a track just to keep its minimap and length.
void build_mini(Game& g, int slot, const TrackDef& def, u32 seed, Allocator* alloc) {
    if (g.mini_ok[slot] && g.mini_seed[slot] == seed) return;
    track_build(g.preview, def, alloc);
    for (int i = 0; i < MINI * MINI; ++i) g.mini[slot][i] = g.preview.mini[i];
    g.mini_len[slot] = (int)g.preview.length;
    g.mini_ok[slot] = true;
    g.mini_seed[slot] = seed;
    track_free(g.preview);
}

// A generated track's preview: its minimap, and its name kept aside. True if it had to build.
bool gen_preview(Game& g, int which, u32 seed, Allocator* alloc) {
    int slot = MINI_RANDOM + which;
    if (g.mini_ok[slot] && g.mini_seed[slot] == seed) return false;
    const TrackDef& d = track_generate(seed, g.gen);
    GenInfo& gi = g.gen_info[which];
    gi = GenInfo{};
    ui_cat(gi.name, sizeof gi.name, d.name ? d.name : "");
    ui_cat(gi.blurb, sizeof gi.blurb, d.blurb ? d.blurb : "");
    gi.theme = d.theme;
    build_mini(g, slot, d, seed, alloc);
    return true;
}

TrackCard gen_card(const Game& g, int which) {
    TrackCard c;
    int slot = MINI_RANDOM + which;
    const GenInfo& gi = g.gen_info[which];
    c.name = gi.name;
    c.blurb = gi.blurb;
    c.theme = g.mini_ok[slot] ? k_themes[gi.theme].name : "";
    c.mini = g.mini_ok[slot] ? g.mini[slot] : nullptr;
    c.length = g.mini_len[slot];
    return c;
}

TrackCard track_card(const Game& g, int index) {
    TrackCard c;
    const TrackDef& def = k_tracks[index];
    c.name = def.name;
    c.blurb = def.blurb ? def.blurb : "";
    c.theme = k_themes[def.theme].name;
    if (index < REC_TRACKS) {
        c.mini = g.mini_ok[index] ? g.mini[index] : nullptr;
        c.length = g.mini_len[index];
        c.best_time = g.rec.best_time[index];
        c.best_lap = g.rec.best_lap[index];
    }
    return c;
}

// A cup's race as a k_tracks index, or -1 for a generated track.
int cup_track(int cup, int i) {
    int t = k_cup_tracks[cup][i];
    return t < 0 ? -1 : t % k_track_count;
}

TrackCard cup_track_card(const Game& g, int cup, int i) {
    int t = cup_track(cup, i);
    return t >= 0 ? track_card(g, t) : gen_card(g, 1 + i);
}

void set_osd(Game& g, const char* a, const char* b) {
    int n = 0;
    for (const char* p = a; *p && n < 46; ++p) g.osd_text[n++] = *p;
    for (const char* p = b; *p && n < 46; ++p) g.osd_text[n++] = *p;
    g.osd_text[n] = 0;
    g.osd = 2.5f;
}

// ---- the race in hand -----------------------------------------------------------------------
void start_race(Game& g, Allocator* alloc) {
    // a generated track is made here, into race_gen, which lives as long as the race
    u32 seed = g.rand_seed;
    if (g.gp.active) {
        g.cur_track = cup_track(g.gp.cup, g.gp.race);
        seed = cup_race_seed(g.cup_seed, g.gp.race);
    } else g.cur_track = g.sel_track < k_track_count ? g.sel_track : -1;
    int chars[4] = {g.sel_char, g.sel_chars[1], g.sel_chars[2], g.sel_chars[3]};
    race_init(g.race, g.cur_track >= 0 ? k_tracks[g.cur_track] : track_generate(seed, g.race_gen), g.sel_char, false, alloc, g.players, chars);
    if (g.time_trial && !g.gp.active && g.cur_track >= 0) {
        if (g.ghost_track != g.cur_track) { // the track's ghost, from disk
            g.ghost_track = g.cur_track;
            g.ghost_time = 0;
            g.ghost_n = ghost_load(g.cur_track, &g.ghost_ch, &g.ghost_time, g.ghost_best.data, (int)g.ghost_best.len);
        }
        race_time_trial(g.race, g.ghost_rec.data, (int)g.ghost_rec.len, g.ghost_best.data, g.ghost_n, g.ghost_ch);
        g.race.ghost_time = g.ghost_n > 0 ? g.ghost_time : 0.0f;
    }
    g.race.cpu_level = (f32)g.sel_diff;
    const char* ap = getenv("SPROCKET_AUTOPILOT");
    g.race.autopilot = ap && ap[0] == '1';
    if (g.race.autopilot) g.gp.assisted = true;
    g.attract_ok = false;
    g.done_seen = false;
    g.hud = HudExtra{};
    g.sel_result = 1;
    g.sel_pause = 0;
}

// The moment the player crosses the line: freeze the placing, award points, check records.
void race_finished(Game& g) {
    const Race& r = g.race;
    const Racer& p = r.racers[r.player];
    HudExtra& h = g.hud;
    h = HudExtra{};
    h.frozen = true;
    for (int i = 0; i < RACERS; ++i) {
        h.place[i] = r.racers[i].place;
        h.points[i] = k_points[r.racers[i].place & 7];
    }
    h.gp = g.gp.active;
    if (r.time_trial) {
        // a faster run (or the first) becomes the track's ghost
        h.ghost_time = g.ghost_n > 0 ? g.ghost_time : 0.0f;
        if (!r.autopilot && p.finished && r.rec_n > 0 && g.cur_track >= 0 && (g.ghost_n == 0 || p.finish_time < g.ghost_time)) {
            for (int i = 0; i < r.rec_n; ++i) g.ghost_best[(u32)i] = g.ghost_rec[(u32)i];
            g.ghost_n = r.rec_n; g.ghost_ch = p.ch; g.ghost_time = p.finish_time;
            ghost_save(g.cur_track, g.ghost_ch, g.ghost_time, g.ghost_best.data, g.ghost_n);
        }
    }
    if (!g.gp.active) {
        h.menu[0] = "RETRY"; h.menu[1] = "NEXT"; h.menu[2] = "QUIT";
        h.menu_count = 3;
    }
    int t = g.cur_track;
    if (t >= 0 && t < REC_TRACKS) {
        h.rec_time = g.rec.best_time[t];
        h.rec_lap = g.rec.best_lap[t];
        if (!r.autopilot && p.finished && r.player2 < 0) { // records are for a lone drive
            if (p.finish_time > 0 && (g.rec.best_time[t] <= 0 || p.finish_time < g.rec.best_time[t])) { g.rec.best_time[t] = p.finish_time; h.new_time = true; }
            if (p.best_lap > 0 && (g.rec.best_lap[t] <= 0 || p.best_lap < g.rec.best_lap[t])) { g.rec.best_lap[t] = p.best_lap; h.new_lap = true; }
            if (h.new_time || h.new_lap) records_save(g.rec, nullptr);
        }
    }
}

// ---- grand prix -----------------------------------------------------------------------------
// The order of the table: points, then the better place in the last race.
void gp_order(const GrandPrix& gp, const int* points, int* order) {
    for (int i = 0; i < RACERS; ++i) order[i] = i;
    for (int a = 1; a < RACERS; ++a)
        for (int b = a; b > 0; --b) {
            int x = order[b], y = order[b - 1];
            bool before = points[x] > points[y] || (points[x] == points[y] && gp.last_place[x] < gp.last_place[y]);
            if (!before) break;
            order[b] = y; order[b - 1] = x;
        }
}

void setup_standings(Game& g, Allocator* alloc) {
    GrandPrix& gp = g.gp;
    StandingsView& v = g.sv;
    v = StandingsView{};
    int before[RACERS], gain[RACERS] = {};
    for (int c = 0; c < RACERS; ++c) before[c] = gp.points[c];
    int old_order[RACERS];
    gp_order(gp, before, old_order); // by the points and placing before this race
    for (int i = 0; i < RACERS; ++i) {
        int c = g.race.racers[i].ch;
        gain[c] = g.hud.points[i];
        gp.last_place[c] = g.hud.place[i];
        gp.points[c] += g.hud.points[i];
    }
    int new_order[RACERS];
    gp_order(gp, gp.points, new_order);
    for (int row = 0; row < RACERS; ++row) {
        int c = old_order[row];
        v.ch[row] = c;
        v.shown[row] = before[c];
        v.gain[row] = gain[c];
        v.slot[row] = (f32)row;
        for (int n = 0; n < RACERS; ++n) if (new_order[n] == c) g.st_target[row] = (f32)n;
    }
    v.cup = gp.cup;
    v.race = gp.race + 1;
    v.player_ch = g.sel_char;
    for (int hu = 0; hu < g.race.humans; ++hu) v.human_mask |= 1u << g.race.racers[g.race.human_idx[hu]].ch;
    v.counting = true;
    if (gp.race + 1 < CUP_RACES) {
        if (cup_track(gp.cup, gp.race + 1) < 0) gen_preview(g, 2 + gp.race, cup_race_seed(g.cup_seed, gp.race + 1), alloc);
        v.next = cup_track_card(g, gp.cup, gp.race + 1);
    }
    g.st_t = 0;
    g.st_sorting = false;
}

void setup_ceremony(Game& g) {
    GrandPrix& gp = g.gp;
    CeremonyView& v = g.cv;
    v = CeremonyView{};
    int order[RACERS];
    gp_order(gp, gp.points, order);
    for (int i = 0; i < RACERS; ++i) {
        v.ch[i] = order[i];
        v.pts[i] = gp.points[order[i]];
        if (order[i] == g.sel_char) v.player_rank = i;
    }
    // with several players the ceremony is for whichever of them did best
    for (int i = RACERS - 1; i >= 0; --i)
        for (int hu = 1; hu < g.players; ++hu) if (order[i] == g.race.racers[g.race.human_idx[hu] >= 0 ? g.race.human_idx[hu] : 0].ch && i < v.player_rank) v.player_rank = i;
    v.cup = gp.cup;
    v.diff = g.sel_diff;
    u8& best = g.rec.cup_rank[gp.cup][g.sel_diff];
    if (!gp.assisted && g.players == 1 && (best == 0 || v.player_rank + 1 < (int)best)) { // trophies are won alone
        best = (u8)(v.player_rank + 1);
        records_save(g.rec, nullptr);
    }
    gp.active = false;
}

// ---- transitions ----------------------------------------------------------------------------
void begin(Game& g, State to, Act act = Act::None, Fx fx = Fx::Mosaic, int frames = 12) {
    if (g.tr.active) return;
    g.tr = Transition{};
    g.tr.active = true;
    g.tr.fx = fx;
    g.tr.frames = frames;
    g.tr.to = to;
    g.tr.act = act;
}

void do_act(Game& g, Act act, Allocator* a) {
    switch (act) {
    case Act::None: break;
    case Act::ToTitle:
        if (!g.attract_ok) start_attract(g, a);
        g.title_t = 0;
        break;
    case Act::EnterTrack:
        if (g.sel_track == k_track_count) gen_preview(g, 0, g.rand_seed, a);
        break;
    case Act::EnterCup:
        // the random cup starts from the clock; left/right re-roll it from there
        if (g.cup_seed == 0) g.cup_seed = ((u32)time(nullptr) ^ (g.tick * 2654435761u)) % 9999u + 1u;
        if (g.sel_cup == CUP_RANDOM) for (int i = 0; i < CUP_RACES; ++i) gen_preview(g, 1 + i, cup_race_seed(g.cup_seed, i), a);
        break;
    case Act::StartRace: start_race(g, a); break;
    case Act::NextCupRace: g.gp.race++; start_race(g, a); break;
    case Act::Standings: setup_standings(g, a); break;
    case Act::Ceremony: setup_ceremony(g); break;
    }
}

// Moves a menu cursor; true if it moved.
bool nav(const Keys& k, int& sel, int count) {
    int before = sel;
    if (k.up()) sel = (sel + count - 1) % count;
    if (k.dn()) sel = (sel + 1) % count;
    if (sel != before) snd_sfx(SFX_MENU_MOVE);
    return sel != before;
}

const char* song_name(int song) {
    static const char* const k[] = {"TITLE", "PARK", "DUNE", "FROST", "CASTLE", "PIRATE", "JUNGLE", "CYBER", "RESULTS", "MENU", "FINISH", "TROPHY", "KITCHEN"};
    return song >= 0 && song < (int)(sizeof k / sizeof k[0]) ? k[song] : "SONG";
}

// ---- the tick -------------------------------------------------------------------------------
// ---- assets -----------------------------------------------------------------------------------
// A file's size and time folded into one number: different whenever the file is rewritten.
i64 file_stamp(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (i64)st.st_mtime * 1000003 + (i64)st.st_size + 1;
}

// The sprites and the samples, from the program's cooked asset tree. False if either is missing.
bool load_assets(Game& g) {
    char dir[400], err[700];
    if (!platform_assets_dir(dir, sizeof dir)) { KV_ERROR("sprocket", "no asset tree: neither <exe>.assets nor assets/ beside the executable"); return false; }
    snprintf(g.art_path, sizeof g.art_path, "%s/sprocket/art.bin", dir);
    snprintf(g.bank_path, sizeof g.bank_path, "%s/sprocket/samples.bin", dir);
    if (!art_load(g.art_path, err, sizeof err)) { KV_ERROR("sprocket", "%s", err); return false; }
    if (!snd_load_bank(g.bank_path, err, sizeof err)) { KV_ERROR("sprocket", "%s", err); return false; }
    g.art_stamp = file_stamp(g.art_path);
    g.bank_stamp = file_stamp(g.bank_path);
    return true;
}

// Twice a second: has either file been cooked again? Then it is loaded in place, between
// two ticks, and what is on screen or playing changes with it. A file caught half written
// is refused by its loader and tried again at the next look.
void reload_assets(Game& g) {
    char err[700];
    i64 a = file_stamp(g.art_path), b = file_stamp(g.bank_path);
    if (a && a != g.art_stamp) {
        if (art_load(g.art_path, err, sizeof err)) { g.art_stamp = a; set_osd(g, "ART ", "RELOADED"); KV_INFO("sprocket", "art reloaded"); }
        else KV_WARN("sprocket", "art not reloaded: %s", err);
    }
    if (b && b != g.bank_stamp) {
        if (snd_load_bank(g.bank_path, err, sizeof err)) { g.bank_stamp = b; set_osd(g, "SAMPLES ", "RELOADED"); KV_INFO("sprocket", "samples reloaded"); }
        else KV_WARN("sprocket", "samples not reloaded: %s", err);
    }
}

// ---- options ----------------------------------------------------------------------------------
void store_settings(Game& g) {
    g.set.wide = g.wide; g.set.crt = g.crt; g.set.split_rows = g.split_rows;
    settings_save(g.set);
}

enum { O_ASPECT, O_CRT, O_DISPLAY, O_MONITOR, O_SPLIT, O_CONTROLS, O_VOLUME, O_MUSIC, O_BACK };

// The slider's ten steps to a gain: gentle at the top, quiet quickly at the bottom, 0 silent.
f32 volume_gain(int v) { f32 x = (f32)(v < 0 ? 0 : (v > 10 ? 10 : v)) / 10.0f; return x * kv::sqrt(x); }

// One pass of an options list: the main menu's, or the shorter one over a paused race.
// Returns 0, 1 to leave, 2 to open the controls.
int options_step(Game& g, const Keys& k, ListView& v, int& sel, bool in_race) {
    static const int main_ids[] = {O_ASPECT, O_CRT, O_DISPLAY, O_MONITOR, O_SPLIT, O_CONTROLS, O_VOLUME, O_MUSIC, O_BACK};
    static const int race_ids[] = {O_ASPECT, O_CRT, O_DISPLAY, O_MONITOR, O_SPLIT, O_CONTROLS, O_VOLUME, O_BACK};
    const int* ids = in_race ? race_ids : main_ids;
    const int n = in_race ? 8 : 9;
    nav(k, sel, n);
    int dir = (k.right() ? 1 : 0) - (k.left() ? 1 : 0);
    bool ok = k.pressed(Key::Enter) || k.pressed(Key::Space) || k.pad_pressed(PadButton::South);
    int mons = display_count(g.native);
    if (mons < 1) mons = 1;
    if (g.set.monitor >= mons) g.set.monitor = 0;
    switch (ids[sel]) {
    case O_ASPECT: if (dir || ok) { g.wide = !g.wide; snd_sfx(SFX_MENU_MOVE); } break;
    case O_CRT: if (dir || ok) { g.crt = (g.crt + CRT_MODES + (dir ? dir : 1)) % CRT_MODES; snd_sfx(SFX_MENU_MOVE); } break;
    case O_DISPLAY: if (dir || ok) { g.set.display = (g.set.display + 3 + (dir ? dir : 1)) % 3; display_apply(g.native, g.set.display, g.set.monitor); snd_sfx(SFX_MENU_MOVE); } break;
    case O_MONITOR: if ((dir || ok) && mons > 1) { g.set.monitor = (g.set.monitor + mons + (dir ? dir : 1)) % mons; display_apply(g.native, g.set.display, g.set.monitor); snd_sfx(SFX_MENU_MOVE); } break;
    case O_SPLIT: if (dir || ok) { g.split_rows = !g.split_rows; snd_sfx(SFX_MENU_MOVE); } break;
    case O_VOLUME:
        if (dir && g.set.volume + dir >= 0 && g.set.volume + dir <= 10) { g.set.volume += dir; snd_volume(volume_gain(g.set.volume)); snd_sfx(SFX_MENU_MOVE); }
        break;
    case O_MUSIC:
        if (dir) { g.music_sel = (g.music_sel + SONG_COUNT + dir) % SONG_COUNT; snd_sfx(SFX_MENU_MOVE); if (g.music_test >= 0) g.music_test = g.music_sel; }
        if (ok) g.music_test = g.music_test == g.music_sel ? -1 : g.music_sel;
        break;
    default: break;
    }
    static const char* const modes[3] = {"WINDOW", "BORDERLESS", "FULLSCREEN"};
    g.opt_mon[0] = (char)('1' + g.set.monitor % 9); g.opt_mon[1] = 0;
    ui_cat(g.opt_mon, sizeof g.opt_mon, " OF ");
    char num[2] = {(char)('0' + (mons > 9 ? 9 : mons)), 0};
    ui_cat(g.opt_mon, sizeof g.opt_mon, num);
    g.opt_music[0] = (char)('0' + g.music_sel / 10); g.opt_music[1] = (char)('0' + g.music_sel % 10); g.opt_music[2] = ' '; g.opt_music[3] = 0;
    ui_cat(g.opt_music, sizeof g.opt_music, song_name(g.music_sel));
    if (g.music_test == g.music_sel) ui_cat(g.opt_music, sizeof g.opt_music, " (ON)");
    for (int i = 0; i < n; ++i) {
        switch (ids[i]) {
        case O_ASPECT: v.items[i] = "ASPECT"; v.values[i] = g.wide ? "16:9  400X224" : "4:3  256X224"; v.hints[i] = "TAB OR F1 SWITCHES IT ANYWHERE."; break;
        case O_CRT: v.items[i] = "CRT SHADER"; v.values[i] = k_crt_names[g.crt]; v.hints[i] = "Q AND E (OR F2 AND F3) CHANGE IT ANYWHERE."; break;
        case O_DISPLAY: v.items[i] = "DISPLAY"; v.values[i] = modes[g.set.display]; v.hints[i] = "A WINDOW, A BORDERLESS ONE FILLING THE MONITOR, OR FULLSCREEN. F11 SWITCHES."; break;
        case O_MONITOR: v.items[i] = "MONITOR"; v.values[i] = g.opt_mon; v.hints[i] = mons > 1 ? "WHICH MONITOR THE GAME IS SHOWN ON." : "ONLY ONE MONITOR IS CONNECTED."; break;
        case O_SPLIT: v.items[i] = "SPLIT SCREEN"; v.values[i] = g.split_rows ? "TOP AND BOTTOM" : "SIDE BY SIDE"; v.hints[i] = "FOR 2P VERSUS: HOW TWO PLAYERS SHARE THE SCREEN."; break;
        case O_CONTROLS: v.items[i] = "CONTROLS"; v.values[i] = "CHANGE"; v.hints[i] = "CHOOSE THE KEYS AND PAD BUTTONS."; break;
        case O_VOLUME: v.items[i] = "VOLUME"; v.bars[i] = g.set.volume; v.hints[i] = "LEFT/RIGHT: QUIETER OR LOUDER."; break;
        case O_MUSIC: v.items[i] = "MUSIC TEST"; v.values[i] = g.opt_music; v.hints[i] = "LEFT/RIGHT PICKS A SONG. ENTER PLAYS OR STOPS IT."; break;
        default: v.items[i] = "BACK"; v.hints[i] = in_race ? "BACK TO THE PAUSE MENU." : "BACK TO THE MAIN MENU."; break;
        }
    }
    v.count = n; v.sel = sel; v.style = WIN_TEAL;
    if (ids[sel] == O_BACK && ok) return 1;
    if (ids[sel] == O_CONTROLS && ok) return 2;
    return 0;
}

// A binding's name for the controls table: six letters at most.
void bind_label(char* out, bool pad, int code) {
    static const char* const pads[] = {"A", "B", "X", "Y", "BACK", "GUIDE", "START", "L3", "R3", "L1", "R1", "UP", "DOWN", "LEFT", "RIGHT"};
    const char* s = pad ? (code >= 0 && code < 15 ? pads[code] : "?") : (code > 0 && code < (int)Key::Count ? key_name((Key)code) : "NONE");
    if (!pad && code == (int)Key::Backspace) s = "BKSP";
    int n = 0;
    for (; s && s[n] && n < 6; ++n) out[n] = (s[n] >= 'a' && s[n] <= 'z') ? (char)(s[n] - 32) : s[n];
    out[n] = 0;
}

void sprocket_tick(TickContext& ctx) {
    Game& g = ctx.state_as<Game>();
    f32 dt = (f32)ctx.dt;
    const Input* input = ctx.resource<const Input>("input.state");
    const HostWindow* window = ctx.resource<const HostWindow>("platform_hid.window");
    Table& sprites = *ctx.write_kind("sprite.entity");
    Renderer* r = renderer_get(*ctx.engine);
    Keys k{input, &g};
    ++g.tick;
    if (g.tick % 30 == 0) reload_assets(g);
    for (u32 i = 0; i < (u32)Key::Count; ++i) g.hit[i] = false;
    g.hit_pad = 0;
    for (u32 i = 0; i < KV_MAX_GAMEPADS; ++i) g.hit_pads[i] = 0;
    for (const KeyDown& e : ctx.received<KeyDown>()) if (!e.repeat && (u32)e.key < (u32)Key::Count) g.hit[(u32)e.key] = true;
    for (const PadButtonDown& e : ctx.received<PadButtonDown>()) { g.hit_pad |= 1u << (u32)e.button; if (e.pad < KV_MAX_GAMEPADS) g.hit_pads[e.pad] |= 1u << (u32)e.button; }
    // the left stick also works the menus: a push past half way counts as one D-pad press
    if (input->sampled.pad()) {
        u8 now = 0; // any pad's stick
        for (u32 i = 0; i < KV_MAX_GAMEPADS; ++i) {
            const GamepadState& pad = input->sampled.pads[i];
            if (!pad.connected) continue;
            f32 lx = pad.axes[(u32)PadAxis::LeftX], ly = pad.axes[(u32)PadAxis::LeftY];
            now |= (u8)((lx < -0.55f ? 1 : 0) | (lx > 0.55f ? 2 : 0) | (ly < -0.55f ? 4 : 0) | (ly > 0.55f ? 8 : 0));
        }
        u8 fresh = (u8)(now & ~g.stick_held);
        if (fresh & 1) g.hit_pad |= 1u << (u32)PadButton::DpadLeft;
        if (fresh & 2) g.hit_pad |= 1u << (u32)PadButton::DpadRight;
        if (fresh & 4) g.hit_pad |= 1u << (u32)PadButton::DpadUp;
        if (fresh & 8) g.hit_pad |= 1u << (u32)PadButton::DpadDown;
        g.stick_held = now;
    } else g.stick_held = 0;

    if (!g.started) {
        g.started = true;
        Sprite back;
        back.material = renderer_default_material(r);
        back.tint = color_rgba(0, 0, 0, 1);
        back.space = SpriteSpace::Screen;
        back.layer = 0;
        back.size = vec2{8192, 8192};
        g.backing = sprites.spawn(transform2_at(point2{0, 0}), back);
        Sprite scr;
        scr.material = g.mat;
        scr.space = SpriteSpace::Screen;
        scr.layer = 1;
        scr.size = vec2{256, 224};
        g.screen = sprites.spawn(transform2_at(point2{0, 0}), scr);
        records_load(g.rec, nullptr);
        // the settings from last time: picture options, bindings, and where the window goes
        settings_default_binds(g.set);
        g.set.crt = g.crt; g.set.wide = g.wide;
        if (settings_load(g.set)) {
            g.wide = g.set.wide; g.crt = g.set.crt % CRT_MODES; g.split_rows = g.set.split_rows;
            snd_volume(volume_gain(g.set.volume));
            if (window->native_handle() && (g.set.display != 0 || g.set.monitor != 0)) display_apply(window->native_handle(), g.set.display, g.set.monitor);
        }
        display_set_icon(window->native_handle());
        // every track's minimap, for the track and cup screens
        for (int i = 0; i < hand_tracks(); ++i) build_mini(g, i, k_tracks[i], 0, g.fb.alloc);
        start_attract(g, g.fb.alloc);
        // testing: SPROCKET_TRACK=n races track n straight away (n = track count: random)
        if (const char* tr = getenv("SPROCKET_TRACK")) {
            g.sel_track = clampi(atoi(tr), 0, k_track_count);
            if (const char* ch = getenv("SPROCKET_CHAR")) g.sel_char = clampi(atoi(ch), 0, RACERS - 1);
            if (const char* tp = getenv("SPROCKET_PLAYERS")) { g.players = clampi(atoi(tp), 1, 4); for (int i = 1; i < 4; ++i) g.sel_chars[i] = (g.sel_char + 1 + i * 2) % RACERS; }
            if (const char* sp = getenv("SPROCKET_SPLIT")) g.split_rows = sp[0] == 'h';
            start_race(g, g.fb.alloc);
            g.state = State::Racing;
        }
        KV_INFO("sprocket", "Enter starts; Tab 4:3/16:9; Q/E or F2/F3 cycle CRT shaders; F11 fullscreen; Esc quits from the title");
    }
    Allocator* a = g.fb.alloc;

    // global keys
    // (a key that has been given a job in the controls loses its shortcut; none fire while a binding is being picked)
    auto free_key = [&](Key key) {
        if (g.ctl_capture || !k.pressed(key)) return false;
        for (int p = 0; p < BIND_PROFILES; ++p) for (int a2 = 0; a2 < BIND_ACTIONS; ++a2) if (g.set.keys[p][a2] == (u16)key) return false;
        return true;
    };
    if (free_key(Key::Tab) || free_key(Key::F1) || k.pad_pressed(PadButton::Back)) {
        g.wide = !g.wide;
        set_osd(g, "ASPECT ", g.wide ? "16:9  400X224" : "4:3  256X224");
    }
    if (free_key(Key::E) || free_key(Key::F3) || k.pad_pressed(PadButton::RStick)) {
        g.crt = (g.crt + 1) % CRT_MODES;
        set_osd(g, "", k_crt_names[g.crt]);
    }
    if (free_key(Key::Q) || free_key(Key::F2) || k.pad_pressed(PadButton::LStick)) {
        g.crt = (g.crt + CRT_MODES - 1) % CRT_MODES;
        set_osd(g, "", k_crt_names[g.crt]);
    }
    g.native = window->native_handle();
    if (free_key(Key::F11) && g.native) {
        g.set.display = g.set.display == 0 ? 2 : 0;
        display_apply(g.native, g.set.display, g.set.monitor);
        store_settings(g);
    }
    if (g.osd > 0) g.osd -= dt;
    // a screen that is changing takes no input
    if (g.tr.active) {
        for (u32 i = 0; i < (u32)Key::Count; ++i) g.hit[i] = false;
        g.hit_pad = 0;
    }

    g.frame.w = g.wide ? FB_W169 : FB_W43;
    g.frame.h = FB_H;
    g.frame.px = g.fb.data;
    Frame& f = g.frame;

    switch (g.state) {
    case State::Title: {
        ++g.title_t;
        race_tick(g.race, Controls{}, dt);
        if (g.race.phase == RacePhase::Done && g.race.done_timer > 3.0f) {
            g.attract_track = (g.attract_track + 1) % k_track_count;
            start_attract(g, a);
        }
        draw_world(f, g.race);
        draw_title_overlay(f, g.title_t);
        if (k.confirm()) { snd_sfx(SFX_MENU_OK); begin(g, State::Mode); }
        else if (k.pressed(Key::Escape)) engine_request_quit(*ctx.engine);
    } break;
    case State::Mode: {
        ListView v;
        v.title = nullptr;
        v.items[0] = "GRAND PRIX"; v.items[1] = "SINGLE RACE"; v.items[2] = "TIME TRIAL"; v.items[3] = "OPTIONS";
        v.hints[0] = "FOUR RACES FOR A TROPHY. ONE TO FOUR PLAYERS.";
        v.hints[1] = "ONE RACE ON ANY TRACK. ONE TO FOUR PLAYERS.";
        v.hints[2] = "ALONE AGAINST THE CLOCK, AND THE GHOST OF YOUR BEST RUN.";
        v.hints[3] = "PICTURE, SOUND AND CONTROLS.";
        v.count = 4;
        nav(k, g.sel_mode, 4);
        v.sel = g.sel_mode;
        draw_list_screen(f, g.tick, v, 0);
        if (k.confirm()) {
            snd_sfx(SFX_MENU_OK);
            g.gp.active = false;
            g.pick = 0;
            g.time_trial = g.sel_mode == 2;
            if (g.time_trial) g.players = 1;
            begin(g, g.sel_mode == 3 ? State::Options : (g.time_trial ? State::SelectRacer : State::Players));
        } else if (k.back()) { snd_sfx(SFX_BACK); begin(g, State::Title, Act::ToTitle); }
    } break;
    case State::Players: {
        ListView v;
        v.title = "PLAYERS";
        v.items[0] = "1 PLAYER"; v.items[1] = "2 PLAYERS"; v.items[2] = "3 PLAYERS"; v.items[3] = "4 PLAYERS";
        v.hints[0] = "THE WHOLE SCREEN TO YOURSELF.";
        v.hints[1] = "SPLIT SCREEN. A GAMEPAD EACH, OR SHARE THE KEYBOARD.";
        v.hints[2] = "A QUARTER EACH. PLAYER THREE NEEDS A GAMEPAD.";
        v.hints[3] = "A QUARTER EACH. PLAYERS THREE AND FOUR NEED GAMEPADS.";
        v.count = 4;
        v.style = WIN_PURPLE;
        nav(k, g.sel_players, 4);
        v.sel = g.sel_players;
        draw_list_screen(f, g.tick, v, 1);
        if (k.confirm()) { snd_sfx(SFX_MENU_OK); g.players = g.sel_players + 1; g.pick = 0; g.sel_dev = g.players; begin(g, g.players > 1 ? State::Devices : State::Difficulty); }
        else if (k.back()) { snd_sfx(SFX_BACK); begin(g, State::Mode); }
    } break;
    case State::Devices: {
        // who drives with what: left to the game, the keyboard, or a pad picked by name
        ListView v;
        v.title = "WHO USES WHAT";
        static const char* const who_names[4] = {"PLAYER 1", "PLAYER 2", "PLAYER 3", "PLAYER 4"};
        int npads = 0, slots[KV_MAX_GAMEPADS];
        for (u32 i = 0; i < KV_MAX_GAMEPADS; ++i) if (input->sampled.pads[i].connected) slots[npads++] = (int)i;
        const int rows = g.players + 1;
        nav(k, g.sel_dev, rows);
        int dir = (k.right() ? 1 : 0) - (k.left() ? 1 : 0);
        if (g.sel_dev < g.players && dir) {
            // automatic, keyboard (players one and two only), then each connected pad
            u8& d = g.set.dev[g.sel_dev];
            int choices = 2 + npads, cur = d >= 2 + npads ? 0 : d;
            for (int tries = 0; tries < choices; ++tries) {
                cur = (cur + choices + dir) % choices;
                if (cur == 1 && g.sel_dev >= 2) continue;
                break;
            }
            d = (u8)cur;
            snd_sfx(SFX_MENU_MOVE);
        }
        for (int p = 0; p < g.players; ++p) {
            int d = g.set.dev[p] >= 2 + npads ? 0 : g.set.dev[p];
            char* t = g.dev_text[p];
            t[0] = 0;
            if (d == 0) ui_cat(t, sizeof g.dev_text[p], "AUTOMATIC");
            else if (d == 1) ui_cat(t, sizeof g.dev_text[p], "KEYBOARD");
            else {
                char head[8] = {'P', 'A', 'D', ' ', (char)('1' + d - 2), ' ', 0};
                ui_cat(t, sizeof g.dev_text[p], head);
                char nm[20];
                const char* src = pad_name(input->sampled.pads[slots[d - 2]].native);
                int n = 0;
                for (; src[n] && n < 16; ++n) { char ch = src[n]; nm[n] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-') ? ch : ' '); }
                nm[n] = 0;
                ui_cat(t, sizeof g.dev_text[p], nm);
            }
            v.items[p] = who_names[p];
            v.values[p] = t;
            v.hints[p] = p < 2 ? "AUTOMATIC: YOUR KEYS, PLUS A PAD IF ONE IS SPARE."
                               : "AUTOMATIC: THE GAME HANDS YOU A PAD. OR CHOOSE ONE YOURSELF.";
        }
        v.items[g.players] = "CONTINUE";
        v.hints[g.players] = npads ? "ON TO THE RACE SETUP." : "NO GAMEPADS ARE CONNECTED: PLAYERS ONE AND TWO SHARE THE KEYBOARD.";
        v.count = rows; v.sel = g.sel_dev; v.style = WIN_PURPLE;
        v.footer = "LEFT/RIGHT CHANGE   ENTER OK   ESC BACK";
        draw_list_screen(f, g.tick, v, 1);
        if (k.confirm() && g.sel_dev == g.players) { snd_sfx(SFX_MENU_OK); store_settings(g); begin(g, State::Difficulty); }
        else if (k.confirm()) { g.sel_dev = g.players; snd_sfx(SFX_MENU_MOVE); } // Enter on a player's row: down to CONTINUE
        else if (k.back()) { snd_sfx(SFX_BACK); store_settings(g); begin(g, State::Players); }
    } break;
    case State::Difficulty: {
        ListView v;
        v.title = "DIFFICULTY";
        for (int i = 0; i < DIFFS; ++i) v.items[i] = k_diff_names[i];
        v.hints[0] = "RIVALS ARE SLOWER, BRAKE EARLY, AND SIT ON THEIR ITEMS.";
        v.hints[1] = "RIVALS RACE HARD AND USE THEIR ITEMS WELL.";
        v.hints[2] = "FASTER RIVALS WHO DRIFT, SHOVE, AND FIRE ON SIGHT.";
        v.count = DIFFS;
        v.style = WIN_RED;
        nav(k, g.sel_diff, DIFFS);
        v.sel = g.sel_diff;
        draw_list_screen(f, g.tick, v, 7);
        if (k.confirm()) { snd_sfx(SFX_MENU_OK); begin(g, State::SelectRacer); }
        else if (k.back()) { snd_sfx(SFX_BACK); begin(g, g.players > 1 ? State::Devices : State::Players); }
    } break;
    case State::Options: {
        ListView v;
        v.title = "OPTIONS";
        int act = options_step(g, k, v, g.sel_opt, false);
        v.footer = "LEFT/RIGHT CHANGE   ESC BACK";
        draw_list_screen(f, g.tick, v, 6);
        if (act == 2) { snd_sfx(SFX_MENU_OK); g.ctl_in_race = false; g.ctl_row = g.ctl_col = 0; g.ctl_capture = false; begin(g, State::Controls); }
        else if (act == 1 || k.back()) { snd_sfx(SFX_BACK); g.music_test = -1; store_settings(g); begin(g, State::Mode); }
    } break;
    case State::Controls: {
        // the bindings table. While a cell waits for a key, every other key use is off.
        Settings& set = g.set;
        if (g.ctl_capture) {
            if (k.pressed(Key::Escape)) { g.ctl_capture = false; snd_sfx(SFX_BACK); }
            else if (g.ctl_col < 3) {
                for (u32 i = 1; i < (u32)Key::Count; ++i) {
                    if (!g.hit[i]) continue;
                    u16* row = set.keys[g.ctl_col];
                    for (int a2 = 0; a2 < BIND_ACTIONS; ++a2) if (row[a2] == (u16)i) row[a2] = row[g.ctl_row]; // the key's old job takes this one's key
                    row[g.ctl_row] = (u16)i;
                    g.ctl_capture = false;
                    snd_sfx(SFX_MENU_OK);
                    break;
                }
            } else {
                for (u32 b2 = 0; b2 < (u32)PadButton::DpadUp; ++b2) {
                    if (!((g.hit_pad >> b2) & 1u) || b2 == (u32)PadButton::Start || b2 == (u32)PadButton::Back || b2 == (u32)PadButton::Guide) continue;
                    int slot = g.ctl_row - 2;
                    for (int a2 = 0; a2 < BIND_PAD; ++a2) if (set.pad[a2] == (u8)b2) set.pad[a2] = set.pad[slot];
                    set.pad[slot] = (u8)b2;
                    g.ctl_capture = false;
                    snd_sfx(SFX_MENU_OK);
                    break;
                }
            }
        } else {
            nav(k, g.ctl_row, BIND_ACTIONS + 2);
            if (g.ctl_row < BIND_ACTIONS) {
                int before = g.ctl_col;
                if (k.left()) g.ctl_col = (g.ctl_col + 3) % 4;
                if (k.right()) g.ctl_col = (g.ctl_col + 1) % 4;
                if (g.ctl_col != before) snd_sfx(SFX_MENU_MOVE);
            }
            bool ok = k.pressed(Key::Enter) || k.pressed(Key::Space) || k.pad_pressed(PadButton::South);
            bool leave = k.back() || (ok && g.ctl_row == BIND_ACTIONS + 1);
            if (leave) {
                snd_sfx(SFX_BACK);
                store_settings(g);
                if (g.ctl_in_race) g.state = State::PauseOptions; else begin(g, State::Options);
            } else if (ok && g.ctl_row == BIND_ACTIONS) { settings_default_binds(set); snd_sfx(SFX_MENU_OK); }
            else if (ok) {
                if (g.ctl_col == 3 && g.ctl_row < 2) snd_sfx(SFX_WALL); // a pad steers with its stick
                else { g.ctl_capture = true; snd_sfx(SFX_MENU_MOVE); }
            }
        }
        ControlsView cv;
        for (int a2 = 0; a2 < BIND_ACTIONS; ++a2) {
            for (int p = 0; p < 3; ++p) bind_label(cv.cell[a2][p], false, set.keys[p][a2]);
            if (a2 >= 2) bind_label(cv.cell[a2][3], true, set.pad[a2 - 2]);
        }
        cv.row = g.ctl_row; cv.col = g.ctl_col; cv.capturing = g.ctl_capture; cv.in_race = g.ctl_in_race;
        if (g.ctl_in_race) draw_race_screen(f, g.race, g.tick, &g.hud, g.split_rows, g.fb2.data);
        draw_controls(f, g.tick, cv);
    } break;
    case State::SelectRacer: {
        // each player in turn; a racer already picked is marked and cannot be picked again
        int& sel = g.pick == 0 ? g.sel_char : g.sel_chars[g.pick];
        int picked[4] = {g.sel_char, g.sel_chars[1], g.sel_chars[2], g.sel_chars[3]};
        auto taken = [&](int c) { for (int q = 0; q < g.pick; ++q) if (picked[q] == c) return true; return false; };
        if (k.left() || k.right() || k.up() || k.dn()) snd_sfx(SFX_MENU_MOVE);
        if (k.left()) sel = (sel & 4) | ((sel + 3) & 3);
        if (k.right()) sel = (sel & 4) | ((sel + 1) & 3);
        if (k.up() || k.dn()) sel = (sel + 4) % RACERS;
        draw_racer_select(f, g.tick, sel, g.players > 1 ? g.pick + 1 : 0, picked, g.pick);
        if (k.confirm()) {
            if (taken(sel)) snd_sfx(SFX_WALL);
            else if (g.pick + 1 < g.players) { // the next player picks, starting on the first racer still free
                snd_sfx(SFX_MENU_OK);
                picked[g.pick] = sel;
                ++g.pick;
                int& next = g.sel_chars[g.pick];
                for (int tries = 0; tries < RACERS && taken(next); ++tries) next = (next + 1) % RACERS;
            } else {
                snd_sfx(SFX_MENU_OK);
                if (g.sel_mode == 0) begin(g, State::SelectCup, Act::EnterCup);
                else begin(g, State::SelectTrack, Act::EnterTrack);
            }
        } else if (k.back()) {
            snd_sfx(SFX_BACK);
            if (g.pick > 0) --g.pick; else begin(g, g.time_trial ? State::Mode : State::Difficulty);
        }
    } break;
    case State::SelectTrack: {
        int count = k_track_count + (g.time_trial ? 0 : 1); // a time trial keeps a ghost, so no random tracks
        if (g.sel_track >= count) g.sel_track = 0;
        nav(k, g.sel_track, count);
        bool random = g.sel_track == k_track_count;
        if (random) {
            u32 before = g.rand_seed;
            if (k.right()) g.rand_seed++;
            if (k.left() && g.rand_seed > 1) g.rand_seed--;
            if (g.rand_seed != before) snd_sfx(SFX_MENU_MOVE);
            gen_preview(g, 0, g.rand_seed, a);
        }
        TrackSelectView v;
        for (int i = 0; i < k_track_count && i < LIST_MAX - 1; ++i) v.names[i] = k_tracks[i].name;
        if (!g.time_trial) v.names[k_track_count < LIST_MAX - 1 ? k_track_count : LIST_MAX - 1] = "RANDOM";
        v.count = count < LIST_MAX ? count : LIST_MAX;
        v.sel = g.sel_track;
        v.random = random;
        v.seed = g.rand_seed;
        v.card = random ? gen_card(g, 0) : track_card(g, g.sel_track);
        draw_track_select(f, g.tick, v);
        if (k.confirm()) { snd_sfx(SFX_MENU_OK); begin(g, State::Racing, Act::StartRace, Fx::Iris, 16); }
        else if (k.back()) { snd_sfx(SFX_BACK); begin(g, State::SelectRacer); }
    } break;
    case State::SelectCup: {
        nav(k, g.sel_cup, CUPS);
        if (g.sel_cup == CUP_RANDOM) {
            u32 before = g.cup_seed;
            if (k.right()) g.cup_seed++;
            if (k.left() && g.cup_seed > 1) g.cup_seed--;
            if (g.cup_seed != before) snd_sfx(SFX_MENU_MOVE);
            // its four previews, one a tick: the maps fill in as they are built
            for (int i = 0; i < CUP_RACES; ++i) if (gen_preview(g, 1 + i, cup_race_seed(g.cup_seed, i), a)) break;
        }
        CupSelectView v;
        v.seed = g.cup_seed;
        v.sel = g.sel_cup;
        v.diff = g.sel_diff;
        for (int c = 0; c < CUPS; ++c) {
            for (int i = 0; i < CUP_RACES; ++i) v.tracks[c][i] = cup_track_card(g, c, i);
            v.best_rank[c] = g.rec.cup_rank[c][g.sel_diff];
        }
        draw_cup_select(f, g.tick, v);
        if (k.confirm()) {
            snd_sfx(SFX_MENU_OK);
            g.gp = GrandPrix{};
            g.gp.active = true;
            g.gp.cup = g.sel_cup;
            begin(g, State::Racing, Act::StartRace, Fx::Iris, 16);
        } else if (k.back()) { snd_sfx(SFX_BACK); begin(g, State::SelectRacer); }
    } break;
    case State::Racing: {
        if (g.race.humans > 1) {
            Controls more[3];
            for (int hu = 1; hu < g.race.humans; ++hu) more[hu - 1] = race_controls(k, hu + 1, g.race.humans);
            race_tick(g.race, race_controls(k, 1, g.race.humans), dt, more);
        } else race_tick(g.race, race_controls(k, 0), dt);
        bool done = g.race.phase == RacePhase::Done;
        if (done && !g.done_seen) { g.done_seen = true; race_finished(g); }
        bool results = done && g.race.done_timer >= 2.5f;
        if (results && !g.gp.active) {
            int before = g.sel_result;
            if (k.left()) g.sel_result = (g.sel_result + 2) % 3;
            if (k.right()) g.sel_result = (g.sel_result + 1) % 3;
            if (g.sel_result != before) snd_sfx(SFX_MENU_MOVE);
        }
        g.hud.menu_sel = g.sel_result;
        draw_race_screen(f, g.race, g.tick, &g.hud, g.split_rows, g.fb2.data);
        if (results && k.confirm()) {
            snd_sfx(SFX_MENU_OK);
            if (g.gp.active) begin(g, State::Standings, Act::Standings, Fx::Fade, 14);
            else if (g.sel_result == 0) begin(g, State::Racing, Act::StartRace, Fx::Iris, 16);
            else if (g.sel_result == 1) {
                if (g.sel_track == k_track_count) g.rand_seed++;
                else g.sel_track = (g.sel_track + 1) % k_track_count;
                begin(g, State::Racing, Act::StartRace, Fx::Iris, 16);
            } else begin(g, State::Mode);
        } else if (!done && (k.pressed(Key::Escape) || k.pad_pressed(PadButton::Start))) {
            g.state = State::Paused;
            g.sel_pause = 0;
            snd_sfx(SFX_PAUSE);
        }
    } break;
    case State::Paused: {
        nav(k, g.sel_pause, 4);
        draw_race_screen(f, g.race, g.tick, &g.hud, g.split_rows, g.fb2.data);
        draw_pause(f, g.tick, g.sel_pause, g.gp.active);
        if (k.pressed(Key::Escape) || k.pad_pressed(PadButton::Start) || k.pad_pressed(PadButton::East)) { g.state = State::Racing; snd_sfx(SFX_PAUSE); }
        else if (k.pressed(Key::Enter) || k.pressed(Key::Space) || k.pad_pressed(PadButton::South)) {
            if (g.sel_pause == 0) { g.state = State::Racing; snd_sfx(SFX_PAUSE); }
            else if (g.sel_pause == 1) { snd_sfx(SFX_MENU_OK); begin(g, State::Racing, Act::StartRace, Fx::Iris, 16); }
            else if (g.sel_pause == 2) { snd_sfx(SFX_MENU_OK); g.sel_popt = 0; g.state = State::PauseOptions; }
            else { snd_sfx(SFX_BACK); g.gp.active = false; begin(g, State::Mode); }
        }
    } break;
    case State::PauseOptions: {
        // the options without leaving the race: the frozen race behind shows each change
        ListView v;
        int act = options_step(g, k, v, g.sel_popt, true);
        draw_race_screen(f, g.race, g.tick, &g.hud, g.split_rows, g.fb2.data);
        draw_pause_options(f, g.tick, v);
        if (act == 2) { snd_sfx(SFX_MENU_OK); g.ctl_in_race = true; g.ctl_row = g.ctl_col = 0; g.ctl_capture = false; g.state = State::Controls; }
        else if (act == 1 || k.pressed(Key::Escape) || k.pad_pressed(PadButton::East) || k.pad_pressed(PadButton::Start)) { snd_sfx(SFX_BACK); store_settings(g); g.state = State::Paused; }
    } break;
    case State::Standings: {
        StandingsView& v = g.sv;
        ++g.st_t;
        if (v.counting) {
            bool gains = false;
            for (int i = 0; i < RACERS; ++i) if (v.gain[i] > 0) gains = true;
            if (k.confirm()) { // skip to the end
                for (int i = 0; i < RACERS; ++i) { v.shown[i] += v.gain[i]; v.gain[i] = 0; v.slot[i] = g.st_target[i]; }
                v.counting = false;
                snd_sfx(SFX_MENU_OK);
            } else if (g.st_t > 40 && gains) {
                if (g.st_t % 4 == 0) {
                    for (int i = 0; i < RACERS; ++i) if (v.gain[i] > 0) { v.gain[i]--; v.shown[i]++; }
                    snd_sfx(SFX_BOLT);
                }
            } else if (g.st_t > 40) {
                // the rows slide to their new places
                bool moving = false, settled = true;
                for (int i = 0; i < RACERS; ++i) {
                    f32 d = g.st_target[i] - v.slot[i];
                    if (kv::fabs(d) > 0.5f) moving = true;
                    if (kv::fabs(d) > 0.02f) { v.slot[i] += d * 0.14f; settled = false; } else v.slot[i] = g.st_target[i];
                }
                if (!g.st_sorting) { g.st_sorting = true; if (moving) snd_sfx(SFX_WHOOSH); }
                if (settled) v.counting = false;
            }
        } else if (k.confirm()) {
            snd_sfx(SFX_MENU_OK);
            if (g.gp.race + 1 < CUP_RACES) begin(g, State::Racing, Act::NextCupRace, Fx::Iris, 16);
            else begin(g, State::Ceremony, Act::Ceremony, Fx::Mosaic, 16);
        }
        draw_standings(f, g.tick, v);
    } break;
    case State::Ceremony: {
        ++g.cv.t;
        if (g.cv.t == 150) snd_sfx(SFX_ITEM_GET);
        draw_ceremony(f, g.tick, g.cv);
        if (g.cv.t > 240 && k.confirm()) { snd_sfx(SFX_MENU_OK); begin(g, State::Title, Act::ToTitle, Fx::Fade, 16); }
    } break;
    }

    // music follows the screen; the engine drones while racing
    {
        int song = SONG_MENU;
        switch (g.state) {
        case State::Title: song = SONG_TITLE; break;
        case State::Options: song = g.music_test >= 0 ? g.music_test : SONG_MENU; break;
        case State::Racing:
        case State::Paused:
        case State::Controls:
        case State::PauseOptions:
            if (g.state == State::Controls && !g.ctl_in_race) break;
            if (g.race.phase == RacePhase::Countdown) song = -1;
            else if (g.race.phase == RacePhase::Racing) song = k_themes[g.race.track.def->theme].song;
            else song = g.race.done_timer < 2.5f ? SONG_FINISH : SONG_RESULTS;
            break;
        case State::Standings: song = SONG_RESULTS; break;
        case State::Ceremony: song = SONG_TROPHY; break;
        default: break;
        }
        if (song != g.song) { g.song = song; snd_music(song); }
    }
    if (g.state == State::Racing && g.race.player >= 0 && g.race.phase != RacePhase::Done) {
        const Racer& p = g.race.racers[g.race.player];
        f32 rev = length(p.vel) / 168.0f + (p.boost > 0 ? 0.25f : 0.0f);
        for (int hu = 1; hu < g.race.humans; ++hu) { // one engine voice: the fastest of the players
            const Racer& q = g.race.racers[g.race.human_idx[hu]];
            rev = mxf(rev, length(q.vel) / 168.0f + (q.boost > 0 ? 0.25f : 0.0f));
        }
        snd_engine(true, rev);
    } else snd_engine(false, 0);

    // the transition: cover the picture, switch screens in the dark, uncover
    if (g.tr.active) {
        Transition& tr = g.tr;
        ++tr.t;
        f32 cover = (f32)tr.t / (f32)tr.frames;
        if (!tr.out) cover = 1.0f - cover;
        switch (tr.fx) {
        case Fx::Mosaic: fx_mosaic(f, cover); break;
        case Fx::Fade: fx_fade(f, cover); break;
        case Fx::Iris: fx_iris(f, cover, f.w / 2, 150); break;
        }
        if (tr.t >= tr.frames) {
            if (tr.out) {
                do_act(g, tr.act, a);
                g.state = tr.to;
                tr.out = false;
                tr.t = 0;
            } else tr.active = false;
        }
    }

    for (const ExitRequested& e : ctx.received<ExitRequested>()) { KV_UNUSED(e); engine_request_quit(*ctx.engine); }
    if (g.osd > 0) {
        int w = text_width(g.osd_text) + 10;
        dim_rect(f, f.w / 2 - w / 2, f.h - 46, w, 13, 0.3f);
        draw_text_c(f, f.w / 2, f.h - 43, g.osd_text, rgb(160, 255, 200), 1);
    }

    // ---- the picture is drawn; the screen sprite is letterboxed to the chosen aspect here
    // (it is the simulation's entity), and the Frame cycle shows the picture (sprocket_present)
    f32 ww = (f32)window->width(), wh = (f32)window->height();
    f32 aspect = g.wide ? 16.0f / 9.0f : 4.0f / 3.0f;
    f32 dw = ww, dh = ww / aspect;
    if (dh > wh) { dh = wh; dw = wh * aspect; }
    dw = kv::floor(dw); dh = kv::floor(dh);
    if (Sprite* s = sprites.get<Sprite>(g.screen)) s->size = vec2{dw, dh};
    g.show_w = dw; g.show_h = dh;
    g.frame_dirty = true;
}

// Presentation, once per frame shown (the Frame cycle): the framebuffer the last tick drew
// goes up to its texture if a tick has drawn since the last frame, and the CRT shader gets
// its parameters when they change. Nothing of the simulation is touched here.
void sprocket_present(TickContext& ctx) {
    Game& g = ctx.state_as<Game>();
    Renderer* r = renderer_get(*ctx.engine);
    if (!r || !g.started) return;
    if (g.frame_dirty) {
        g.frame_dirty = false;
        const Frame& f = g.frame;
        renderer_texture_upload(r, g.tex, (u32)f.w, (u32)f.h, f.px, (usize)f.w * (usize)f.h * 4);
    }
    f32 params[8] = {0, 0, g.show_w, g.show_h, (f32)g.crt, g.srgb ? 1.0f : 0.0f, 0, 0};
    bool changed = false;
    for (int i = 0; i < 8; ++i) if (params[i] != g.last_params[i]) changed = true;
    if (changed) {
        MaterialDesc d;
        d.name = "sprocket.screen";
        d.texture = g.tex;
        d.blend = BlendMode::Opaque;
        d.filter = TextureFilter::Nearest;
        d.shader = "sprocket_crt";
        for (int i = 0; i < 8; ++i) { d.params[i] = params[i]; g.last_params[i] = params[i]; }
        renderer_material_set(r, g.mat, d);
    }
}

bool sprocket_startup(Engine& engine, ModuleInstance& self) {
    Game* g = arena_push<Game>(self.arena);
    if (!g) return false;
    self.set_state(g);
    g->fb.alloc = &self.general;
    g->fb.resize_fill(FB_MAXW * FB_H, 0xff000000u);
    g->fb2.alloc = &self.general;
    g->fb2.resize_fill(FB_MAXW * FB_H, 0xff000000u);
    g->ghost_rec.alloc = g->ghost_best.alloc = &self.general;
    g->ghost_rec.resize_fill(60 * 300, GhostSample{}); // five minutes a run
    g->ghost_best.resize_fill(60 * 300, GhostSample{});
    Renderer* r = renderer_get(engine);
    if (!r) return false;
    g->srgb = renderer_caps(r).srgb_output;
    if (!load_assets(*g)) return false;
    snd_start(engine);
    TextureDesc td;
    td.format = TextureFormat::RGBA8_UNORM; // the shaders expect gamma-encoded texels, as libretro's do
    td.name = "sprocket.frame";
    g->tex = renderer_texture_create(r, td);
    renderer_texture_upload(r, g->tex, FB_W43, FB_H, g->fb.data, (usize)FB_W43 * FB_H * 4);
    MaterialDesc d;
    d.name = "sprocket.screen";
    d.texture = g->tex;
    d.blend = BlendMode::Opaque;
    d.filter = TextureFilter::Nearest;
    d.shader = "sprocket_crt";
    g->mat = renderer_material_create(r, d);
    if (g->mat.is_null()) return false;
    return true;
}

void sprocket_shutdown(Engine& engine, ModuleInstance& self) {
    Game* g = self.state_as<Game>();
    if (!g) return;
    snd_stop(engine);
    race_free(g->race);
    track_free(g->preview);
    g->fb.free();
    g->fb2.free();
    g->ghost_rec.free(); g->ghost_best.free();
    if (Renderer* r = renderer_get(engine)) {
        renderer_material_destroy(r, g->mat);
        renderer_texture_destroy(r, g->tex);
    }
}

constinit const SystemDesc k_systems[] = {
    {   .name = ".tick",
        .phase = Phase::Sim,
        .fn = sprocket_tick,
        .reads = "input.state, platform_hid.window, ExitRequested, KeyDown, PadButtonDown",
        .writes = "sprite.entity"
    },
    {   .name = ".present",
        .phase = Phase::Frame,
        .fn = sprocket_present,
        .main_thread = true,
    },
};

} // namespace

KV_MODULE_DEFINE(sprocket_module) = {
    .name     = "sprocket",
    .systems  = k_systems,
    .startup  = sprocket_startup,
    .shutdown = sprocket_shutdown,
    .memory   = {.reserve = 1024 * 1024},
};

} // namespace kv
