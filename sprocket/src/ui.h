// Sprocket Rally's UI layer: the shared widgets (hud.cc), and the menu screens, screen
// transitions and records (menu.cc). All of it is engine-free: plain structs in, pixels out
// into the low-resolution framebuffer, so tools/headless/ui_shots.cc can render every screen
// to an image file without a window.
#pragma once
#include "common.h"

namespace sr {

// ---- widgets (hud.cc) -----------------------------------------------------------------------
enum WinStyle : u8 { WIN_BLUE, WIN_GREEN, WIN_PURPLE, WIN_RED, WIN_TEAL, WIN_GLASS };
// A framed SNES menu window: dark outline, bright bevel, dithered gradient fill.
// WIN_GLASS darkens what is under it instead (for panels over the race).
void draw_window(Frame& f, int x, int y, int w, int h, int style);
void draw_text_r(Frame& f, int xr, int y, const char* s, u32 color, int scale = 1); // right-aligned at xr
void draw_img_half(Frame& f, const PropImg& img, int x, int y);                      // at half size
// A track's 64x64 minimap mask drawn `size` pixels square (size <= 64), with a drop shadow.
void draw_mini(Frame& f, const u8* mini, int x, int y, int size, u32 road, u32 hazard);
void draw_lights(Frame& f, int stage, int x, int y, int scale); // start lights: 0 dark, 1..3 reds, 4 green
const PropImg& place_badge(int place);                          // 0-based
const char* ui_ordinal(int place);                              // 0-based: "1ST"
void ui_time(char* buf, f32 t);                                 // 0'00"00 (8 bytes); t <= 0: dashes
void ui_int(char* buf, int v);
void ui_cat(char* buf, int cap, const char* s);                 // bounded strcat

// What the game adds to the race HUD's results panel.
struct HudExtra {
    bool        results = true;       // draw the results panel once the race is done
    const char* menu[3] = {};         // the options under the results (menu_count 0: "PRESS ENTER")
    int         menu_count = 0, menu_sel = 0;
    bool        new_time = false, new_lap = false; // records broken in this race
    f32         rec_time = 0, rec_lap = 0;         // the track's records (0: none)
    bool        gp = false;           // grand prix: show the points column
    int         points[RACERS] = {};  // by racer index: points won in this race
    int         place[RACERS] = {};   // by racer index: the placing when the player finished
    bool        frozen = false;       // use `place` rather than the live placing
    f32         ghost_time = 0;       // time trial: the ghost's time before this run (0: there was none)
};
void draw_hud_ex(Frame& f, const Race& r, u32 tick, const HudExtra* extra);
void draw_race_screen(Frame& f, const Race& r, u32 tick, const HudExtra* extra, bool split_rows, u32* scratch);

// ---- records (menu.cc) ----------------------------------------------------------------------
constexpr int REC_TRACKS = 16, CUPS = 3, CUP_RACES = 4, DIFFS = 3; // REC_TRACKS: hand-made tracks with records
constexpr int CUP_RANDOM = -1;      // no cup is made of generated tracks now (random tracks live in SINGLE RACE)
constexpr int LIST_MAX = 16;
struct Records {
    f32 best_time[REC_TRACKS] = {};   // by k_tracks index; 0: none
    f32 best_lap[REC_TRACKS] = {};
    u8  cup_rank[CUPS][DIFFS] = {};   // best final rank, 1..8; 0: never finished
};
// path null: $HOME/.local/share/sprocket-rally/records.txt
bool records_load(Records& rec, const char* path);
bool records_save(const Records& rec, const char* path);

// ---- screen transitions (menu.cc) -----------------------------------------------------------
// t runs 0 (clear picture) .. 1 (fully covered).
void fx_fade(Frame& f, f32 t);
void fx_mosaic(Frame& f, f32 t);                 // SNES mosaic: blocks grow to 16 pixels, with a fade
void fx_iris(Frame& f, f32 t, int cx, int cy);   // a closing circle

// ---- menu screens (menu.cc) -----------------------------------------------------------------
extern const char* const k_cup_names[CUPS];
extern const int k_cup_tracks[CUPS][CUP_RACES]; // k_tracks indices; -1: a generated track
extern const char* const k_diff_names[DIFFS];
extern const int k_points[RACERS];
const PropImg& cup_icon(int cup);

// What a screen needs to show a track.
struct TrackCard {
    const char* name = "";
    const char* blurb = "";
    const char* theme = "";
    const u8*   mini = nullptr;   // 64x64 mask, or null while it is being built
    int         length = 0;
    f32         best_time = 0, best_lap = 0;
};

void menu_backdrop(Frame& f, u32 tick, int variant);

void draw_title_overlay(Frame& f, u32 tick);     // over the attract race

struct ListView {                // mode select, difficulty, pause, options
    const char* title = "";
    const char* items[10] = {};
    const char* values[10] = {}; // an option's current value, or null
    const char* hints[10] = {};  // one line about the highlighted item
    int bars[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // 0..10: the item shows a slider at that level instead of a value
    int count = 0, sel = 0;
    int style = WIN_BLUE;
    const char* footer = "";
};
void draw_list_screen(Frame& f, u32 tick, const ListView& v, int backdrop);
void draw_pause(Frame& f, u32 tick, int sel, bool gp);

// player 1..4: a multiplayer pick, with the earlier players' racers (`picked`, `npicked`) marked and not to be had twice
void draw_racer_select(Frame& f, u32 tick, int sel, int player = 0, const int* picked = nullptr, int npicked = 0);
void draw_pause_options(Frame& f, u32 tick, const ListView& v);

// ---- settings (menu.cc) ---------------------------------------------------------------------
// What the options screens change, kept in settings.txt beside the records.
constexpr int BIND_PROFILES = 3, BIND_ACTIONS = 6, BIND_PAD = 4;
struct Settings {
    bool wide = false;
    int  crt = 1;
    bool split_rows = false;
    int  display = 0;                 // 0 window, 1 borderless, 2 fullscreen
    int  monitor = 0;
    int  volume = 10;                 // 0..10
    // keys by profile (alone, player one of two, player two) and action (steer left, steer
    // right, gas, brake, hop, item), as the engine's key numbers; 0: none
    u16  keys[BIND_PROFILES][BIND_ACTIONS] = {};
    u8   pad[BIND_PAD] = {};          // pad buttons for gas, brake, hop, item (steering is the stick and D-pad)
    // multiplayer: what each player drives with. 0 automatic (pads to the last players first,
    // keys as well for players one and two), 1 keyboard only, 2 + n the nth connected pad only
    u8   dev[4] = {};
};
void settings_default_binds(Settings& s);
bool settings_load(Settings& s);
bool settings_save(const Settings& s);

// A time trial's best run on a hand-made track, kept beside the records as ghost_<track>.bin.
// Load returns how many samples it read (0: none).
int  ghost_load(int track, int* ch, f32* time, GhostSample* out, int cap);
bool ghost_save(int track, int ch, f32 time, const GhostSample* s, int n);

struct ControlsView {
    char cell[BIND_ACTIONS][4][8] = {}; // columns: ALONE, 2P P1, 2P P2, PAD
    int  row = 0, col = 0;              // rows past the actions: RESET, BACK
    bool capturing = false;
    bool in_race = false;               // over the paused race rather than a menu backdrop
};
void draw_controls(Frame& f, u32 tick, const ControlsView& v);

// ---- the window (zz_display.cc) -------------------------------------------------------------
// `native` is the engine window's own handle. Modes: 0 window, 1 borderless, 2 fullscreen.
int  display_count(void* native);
void display_apply(void* native, int mode, int monitor);
void display_set_icon(void* native);
bool pad_is_keyboard(void* sdl_gamepad); // an analog keyboard posing as a gamepad: not handed out automatically
const char* pad_name(void* sdl_gamepad);  // never null

struct TrackSelectView {
    const char* names[LIST_MAX] = {};
    int count = 0, sel = 0;
    TrackCard card;
    bool random = false;
    u32  seed = 0;
};
void draw_track_select(Frame& f, u32 tick, const TrackSelectView& v);

struct CupSelectView {
    int sel = 0, diff = 1;
    TrackCard tracks[CUPS][CUP_RACES];
    u8 best_rank[CUPS] = {};     // at this difficulty
    u32 seed = 0;                // the random cup's seed
};
void draw_cup_select(Frame& f, u32 tick, const CupSelectView& v);

struct StandingsView {
    int  cup = 0, race = 0;      // race: how many have been run (1..4)
    int  ch[RACERS] = {};        // rows: character index
    int  shown[RACERS] = {};     // points shown so far
    int  gain[RACERS] = {};      // points still to count up
    f32  slot[RACERS] = {};      // row position (0 top .. 7), eased toward its rank
    int  player_ch = 0;
    u32  human_mask = 0;         // a bit per character driven by a player (0: just player_ch)
    TrackCard next;              // the next race (name empty after the last)
    bool counting = false;
};
void draw_standings(Frame& f, u32 tick, const StandingsView& v);

struct CeremonyView {
    int cup = 0, diff = 1;
    int ch[RACERS] = {};         // final order: character index
    int pts[RACERS] = {};
    int player_rank = 0;         // 0-based
    u32 t = 0;                   // ticks since the ceremony began
};
void draw_ceremony(Frame& f, u32 tick, const CeremonyView& v);

} // namespace sr
