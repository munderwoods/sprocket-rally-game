// Sprocket Rally: an SNES-style kart racer, software-rendered at 256x224 (4:3) or
// 400x224 (16:9) and scaled up to the window through a CRT shader.
//
// Everything visible is drawn by the CPU into a small framebuffer (Mode 7 floor, sky,
// sprites, HUD, menus), uploaded as one texture per tick and shown as one screen sprite.
#pragma once
#include "korvid/core/array.h"
#include "korvid/core/engine.h"
#include "korvid/core/random.h"
#include "korvid/core/types.h"
#include "korvid/math/math.h"
#include "korvid/math/scalar.h"
#include "korvid/math/vec2.h"
#include "art.h"

namespace sr {
using namespace kv;

// ---- screen --------------------------------------------------------------------------------
constexpr int FB_H = 224, FB_W43 = 256, FB_W169 = 400, FB_MAXW = 400;
constexpr int HORIZON = 70;          // first floor row is HORIZON + 1
constexpr f32 FOCAL = 163.0f;        // pixels; 1024 / (2 pi), so the 1024-wide sky layer turns 1:1 with the floor
constexpr f32 CAM_H = 40.0f;         // camera height, texels
constexpr f32 CAM_DIST = 57.0f;      // camera distance behind the player
constexpr f32 SPRITE_WORLD = CAM_DIST / FOCAL; // world texels per sprite pixel (player kart at scale 1)

struct Frame {
    u32* px = nullptr;
    int  w = FB_W43, h = FB_H;
};

inline u32 rgb(u32 r, u32 g, u32 b) {
    // SNES 15-bit color: 5 bits per channel.
    r = (r >> 3) * 255 / 31; g = (g >> 3) * 255 / 31; b = (b >> 3) * 255 / 31;
    return 0xff000000u | (b << 16) | (g << 8) | r;
}
inline u32 shade(u32 c, f32 k) {
    u32 r = (u32)((c & 0xff) * k), g = (u32)(((c >> 8) & 0xff) * k), b = (u32)(((c >> 16) & 0xff) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xff000000u | (b << 16) | (g << 8) | r;
}
inline u32 blend(u32 a, u32 b, f32 t) {
    auto ch = [&](int s) { f32 x = (f32)((a >> s) & 0xff), y = (f32)((b >> s) & 0xff); return (u32)(x + (y - x) * t) << s; };
    return 0xff000000u | ch(0) | ch(8) | ch(16);
}
inline void put_px(Frame& f, int x, int y, u32 c) {
    if ((unsigned)x < (unsigned)f.w && (unsigned)y < (unsigned)f.h) f.px[y * f.w + x] = c;
}
inline f32 mnf(f32 a, f32 b) { return a < b ? a : b; }
inline f32 mxf(f32 a, f32 b) { return a > b ? a : b; }
inline f32 wrap_pi(f32 a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}
inline u32 hash2(i32 x, i32 y) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// ---- track ---------------------------------------------------------------------------------
constexpr int MAP = 1024;
constexpr int MINI = 64;

// S_PIT: a drop off the track; S_WATER: deep water. Both send the kart for a rescue.
// S_MUD, S_ICE and S_SHALLOW are patches on the road: slow, slippery, and a ford.
// S_RAMP launches a kart into the air (over a gap, usually).
enum Surface : u8 { S_ROAD, S_OFF, S_WALL, S_BOOST, S_OUT, S_PIT, S_WATER, S_MUD, S_ICE, S_RAMP, S_SHALLOW };

enum MapColor : u8 {
    C_ROAD0, C_ROAD1, C_ROAD2, C_LINE, C_CURB_A, C_CURB_B,
    C_OFF0, C_OFF1, C_OFF2, C_WALL0, C_WALL1, C_OUT0, C_OUT1,
    C_START_A, C_START_B, C_BOOST0, C_BOOST1,
    C_WATER0, C_WATER1, C_WATER2, C_PIT0, C_PIT1, C_MUD0, C_MUD1, C_ICE0, C_ICE1,
    C_RAIL, C_TIE, C_LANE, C_RAMP0, C_RAMP1, C_SHAL0, C_SHAL1,
    C_DEC0, C_DEC1, C_DEC2, C_DEC3, C_DEC4, C_DEC5, // a theme's own colors for its decor stamps
    C_COUNT
};

enum PropKind : u8 { P_TREE, P_CACTUS, P_PINE, P_TOWER, P_BANNER, P_PALM, P_BARREL, P_FERN, P_JTREE, P_LAMP, P_NEON,
                     P_LOG, P_BONES, P_CRATE, P_BUOY, P_SNOWMAN, P_CONE, P_PILLAR, P_TORCH, P_BARRIER, P_ROCK, P_SHRUB,
                     P_SUGAR, P_SALT, P_PEPPER, P_EGGCUP,
                     P_TOXIC, P_PIPE, P_RAT, P_DUCK, P_SLAMP, P_LADDER, P_LED_R, P_LED_G, P_JUMPER, P_TRIMPOT, P_ASTRONAUT, P_BEACON,
                     P_TOMB0, P_TOMB1, P_DEADTREE, P_FENCE, P_CANDLE, P_PUMPKIN, P_ARMOR, P_COFFIN, P_BAT, P_CLOCK, P_NONE };

enum RoadStyle : u8 { RS_ASPHALT, RS_COBBLE, RS_PLANKS, RS_DIRT, RS_NEON, RS_TABLE, RS_SEWER, RS_PCB, RS_SPACE, RS_HAUNT };
enum BgStyle : u8 { BG_MEADOW, BG_DESERT, BG_FROST, BG_CASTLE, BG_PIRATE, BG_JUNGLE, BG_CYBER, BG_KITCHEN, BG_SEWER, BG_PC, BG_SPACE, BG_HAUNT };

enum SetPieceKind : u8 { SPK_NONE, SPK_BLIMP, SPK_TRAIN, SPK_SNOWBALL, SPK_DRAGON, SPK_SHIP, SPK_DINO, SPK_TRAFFIC, SPK_TOASTER, SPK_WAVE, SPK_BUG, SPK_COMET, SPK_PENDULUM };
// Set-piece images (render.cc maps them to the baked sprites).
enum SpImg : u8 { SPI_TRAIN_LOCO, SPI_TRAIN_CAR, SPI_SNOWBALL, SPI_DRAGON, SPI_SHIP, SPI_DINO, SPI_BLIMP, SPI_HOVERCAR, SPI_HOVERBUS,
                  SPI_GANTRY, SPI_CHEVRON_L, SPI_CHEVRON_R,
                  SPI_CEREAL, SPI_MILK, SPI_JUICE, SPI_SYRUP, SPI_TOASTER, SPI_MUG, SPI_ORANGE, SPI_BUTTER,
                  SPI_WAVE, SPI_CAPACITOR, SPI_CHIP, SPI_RESISTOR, SPI_HEATSINK, SPI_CRYSTAL, SPI_CERAMIC, SPI_MOTH,
                  SPI_SUN, SPI_JUPITER, SPI_SATURN, SPI_EARTH, SPI_MOON, SPI_MARS, SPI_NEPTUNE, SPI_VENUS,
                  SPI_ASTEROID, SPI_COMET, SPI_SATELLITE, SPI_GHOST, SPI_CARRIAGE, SPI_COUNT };

struct Theme {
    const char* name;
    u32 pal[C_COUNT];
    u32 sky_top, sky_bottom;
    u32 far_a, far_b, near_a, near_b;   // background layer colors
    BgStyle bg;
    RoadStyle road;
    f32 grip;                            // road grip multiplier
    PropKind decor, obstacle;            // scenery beyond the barrier; solid things in the off-road
    u8  edge;                            // the theme's natural drop past an open edge: S_PIT or S_WATER
    SetPieceKind setpiece;
    bool stars;                          // night sky
    PropKind extra;                      // a second scenery prop, mixed in
    int song;                            // SongId for races on this theme
    f32 gravity;                         // 1 normal; less and jumps hang (space)
    u8  fire_look;                       // how a line of fire across the road is drawn: 0 flames, 1 electric arcs, 2 solar flares
};

// ---- track authoring -----------------------------------------------------------------------
// A track is a closed spline through nodes (each may set the road's half-width there), a
// list of features placed along it, and terrain stamps under and around it. Positions along
// the track are in node units: 3.5 is halfway between node 3 and node 4. Lateral positions
// are fractions of the half-width: -1 the left edge, 0 the middle, +1 the right edge.
struct Node { f32 x, y, w; };          // w: half-width here (0: the track's default)

enum FeatKind : u8 {
    F_SHOULDER,  // n0..n1, sides by l0 < 0 (left) and l1 > 0 (right): shoulder width `a`, beyond it `b` (0 wall, S_PIT, S_WATER)
    F_SURFACE,   // n0..n1 across l0..l1: surface `b` (S_ICE, S_MUD, S_SHALLOW); a: edge wobble 0..1
    F_BOOST,     // a boost pad at n0, lateral l0
    F_RAMP,      // a launch ramp at n0 across l0..l1
    F_GAP,       // n0..n1: the road is gone (surface `b`), to be jumped; a: how far the gap reaches past the road's edge
    F_BOXES,     // a row of `a` item crystals at n0 across l0..l1
    F_BOLTS,     // bolts from n0 to n1, sweeping from lateral l0 to l1
    F_POSTS,     // solid props (kind `b`) on the road from n0 to n1, alternating between laterals l0 and l1, every `a` texels
    F_ISLAND,    // n0..n1 across l0..l1: a divider wall in the road
    F_PAINT,     // n0..n1: the road is painted as something else (b: 1 a waffle, 2 a water channel, 3 floorboards with a carpet runner); looks only
    F_GATE,      // at n0: a line of fire (arcs, flares) across the road every `a` seconds, with a warning first
};
struct Feat { u8 kind; f32 n0, n1, l0, l1, a; u8 b; };

enum StampKind : u8 { ST_DISC, ST_RING, ST_STRIP, ST_BOX }; // a box is the rectangle x0,y0 .. x1,y1
// Terrain painted where there is no road: a disc (x0,y0,r0), a ring (r0..r1) or a strip from
// (x0,y0) to (x1,y1) of half-width r0. `keep` leaves that much shoulder beside the road.
// `color`: 0 the surface's own; 1-6 the theme's decor colors; 7 a waffle; 8 a placemat.
struct Stamp { u8 kind, surf; f32 x0, y0, x1, y1, r0, r1, keep; u8 color; };
// A big object standing on the map (a cereal box, a toaster): drawn as a set-piece image,
// solid over its footprint (half-extents hx along its heading, hy across).
struct Obj { u8 img; f32 x, y, heading, hx, hy; f32 z, scale; }; // z: how high it floats; scale: 0 = 1

struct TrackDef {
    const char* name;
    const char* blurb;       // one line for the track select
    int theme;
    const Node* nodes; int nnodes;
    f32 half_width;
    const Feat* feats; int nfeats;
    const Stamp* stamps; int nstamps;
    f32 shoulder;            // default off-road band (0: 36)
    u8  edge;                // beyond the shoulder by default: 0 a barrier wall, or S_PIT / S_WATER
    f32 sp[8];               // the set piece's path: ax, ay, bx, by (and a second line); all 0: through the middle
    u8  sp_flags;            // SPF_*
    u32 seed;
    const Obj* objs; int nobjs;
};
enum : u8 { SPF_ONE_WAY = 1, SPF_WEAVE = 2, SPF_SEGMENT = 4 };

struct Prop    { vec2 pos; u8 kind; bool solid; };
struct ItemBox { vec2 pos; f32 respawn; f32 z, vz; f32 life; bool fake; }; // life > 0: a temporary one (dropped)
struct Bolt    { vec2 pos; f32 respawn; f32 life = 0; vec2 vel{}; f32 z = 0, vz = 0; }; // life > 0: one knocked out of a kart, bouncing away

// One moving body of a set piece: a train car, a hover car, the dinosaur...
struct SetBody {
    u8   img;          // which set-piece image (render.cc's table)
    vec2 pos;
    f32  z, heading;
    f32  length, radius; // collision: circles of `radius` spaced along `length`
    f32  t;            // its own parameter along its path
    i8   lane;         // traffic: which lane / direction
    f32  scale;        // drawing scale (0: 1)
    bool solid;
};

struct SetPiece {
    SetPieceKind kind = SPK_NONE;
    vec2 a{}, b{};       // the line it travels (crossing the map through the center)
    vec2 a2{}, b2{};     // a second line (traffic)
    vec2 center{};
    f32  radius = 0;     // flyers and the ship: a circle around the center
    f32  t = 0, timer = 0, aux = 0, aux2 = 0;
    int  dir = 1;
    Array<SetBody> bodies;
    vec2 cross[8];       // where the train line crosses the road (warning lights)
    f32  ring[64];       // the ship: its distance from the center on each bearing, out in open water
    int  ncross = 0;
};

struct Track {
    const TrackDef* def = nullptr;
    const Theme*    theme = nullptr;
    Array<u8>   color;     // MAP*MAP palette indices
    Array<u8>   surf;      // MAP*MAP Surface
    Array<vec2> line;      // centerline, evenly spaced
    Array<f32>  dist;      // distance along the line at each point
    f32 length = 0;
    u8  mini[MINI * MINI]; // 0 nothing, 1 road, 2 hazard
    Array<Prop>    props;
    Array<ItemBox> boxes;
    Array<Bolt>    bolts;
    Array<u32> far_layer;  // 512 x 64
    Array<u32> near_layer; // 1024 x 64
    SetPiece   sp;         // its path; the race moves its bodies
    Array<f32> hw;         // half-width at each line point
    Array<f32> node_s;     // distance along the line at each node (and the length at the end)
    Array<SetBody> statics; // the start gantry and corner boards: drawn like set-piece bodies, never moving
    f32 gate_s[8];         // where the F_GATE hazards are, and how often they fire
    f32 gate_period[8];
    int ngates = 0;
};

extern const TrackDef k_tracks[];
extern const int k_track_count;
extern const Theme k_themes[];
extern const int k_theme_count;

void track_build(Track& t, const TrackDef& def, Allocator* alloc);
void track_free(Track& t);
// A random track with designed features: fills `out` and returns its definition.
struct GenTrack { Node nodes[64]; Feat feats[64]; Stamp stamps[8]; TrackDef def; char name[24]; };
const TrackDef& track_generate(u32 seed, GenTrack& out);
f32  track_node_s(const Track& t, f32 node);                  // node units -> distance along the line
f32  track_half_width(const Track& t, f32 s);
u8   track_surface(const Track& t, f32 x, f32 y);
vec2 track_point(const Track& t, f32 s, vec2* tangent);      // position (and tangent) at distance s
f32  track_project(const Track& t, vec2 p, int* seg, int window); // distance along the line nearest p

// ---- racers --------------------------------------------------------------------------------
constexpr int RACERS = 8;
constexpr int LAPS = 3;

enum Item : u8 { IT_NONE, IT_NITRO, IT_NITRO3, IT_BOMB, IT_ROCKET, IT_OIL, IT_FAKE, IT_SHIELD, IT_EMP, IT_MAGNET, IT_COUNT };
const char* item_name(int item);

struct Character {
    const char* name;
    const char* vehicle;
    const char* bio[2];
    u32 color[2];          // UI: main and accent
    f32 top, accel, turn;  // stat multipliers
    f32 weight;            // bumps: heavier pushes lighter
    f32 offroad;           // how much of the off-road slowdown it ignores (0..1)
    f32 turbo, charge;     // mini-turbo length and drift charge rate multipliers
    // how the CPU drives it
    f32 aggression;        // 0..1: item timing, and nudging others
    f32 drift_bias;        // 0..1: how readily it drifts
    int favorite;          // the item it rolls more often
};
extern const Character k_chars[RACERS];

struct Controls {
    f32  steer = 0;            // -1 left .. 1 right
    bool accel = false, brake = false;
    bool hop = false, hop_pressed = false;
    bool item_pressed = false;
};

struct Racer {
    int  ch = 0;
    bool human = false;
    vec2 pos{}, vel{};
    f32  heading = 0;
    f32  z = 0, vz = 0;        // hop
    bool airborne = false;
    // drift
    bool drifting = false;
    int  drift_dir = 0;
    f32  drift_charge = 0;
    f32  drift_ramp = 0;       // 0..1: how far the body has swung into the drift
    f32  move_angle = 0;       // the direction the kart is pushed (body heading less the drift angle)
    f32  boost = 0;
    f32  spin = 0;             // > 0: spinning out
    f32  shield = 0;
    f32  invuln = 0;           // after a hit: a moment where nothing can hit again
    f32  stun = 0;             // EMP: sluggish
    f32  magnet = 0;           // pulled toward `magnet_target`
    int  magnet_target = -1;
    f32  fall = 0;             // > 0: falling or sinking, then rescued
    u8   fall_kind = 0;        // S_PIT or S_WATER
    f32  rescue = 0;           // > 0: being lowered back onto the track
    f32  last_safe_s = 0;
    int  stat_falls = 0, stat_hits = 0; // for the results and the test harness
    bool absent = false;     // time trial: not in this race at all
    f32  lap_times[LAPS] = {};
    f32  lap_start = 0, last_lap = 0, best_lap = 0; // race_time at the lap's start; the last and best lap times (0: none yet)
    f32  wrong_way = 0;        // seconds spent driving against the track
    bool ramp_air = false;     // airborne off a ramp (not a hop)
    f32  rev = 0;              // how long the throttle has been held on the grid
    f32  draft = 0;            // seconds tucked in behind someone; past a second, a tow
    f32  air_time = 0;
    f32  surf = 0;             // riding the sewer's wave
    // race
    int  lap = 0;
    f32  s = 0;
    int  seg = 0;
    bool passed_half = true;
    f32  total = 0;
    int  place = 0;
    bool finished = false;
    f32  finish_time = 0;
    int  item = IT_NONE;
    int  item_uses = 0;        // triple nitro
    f32  roulette = 0;
    int  bolts = 0;
    // AI
    f32  lane = 0, skill = 1, item_wait = 0, ai_t = 0;
    bool ai_drift = false;
    Controls in;
    bool prev_hop = false;
    f32  yaw_avg = 0, yaw_prev = 0; // recent turning, for choosing a drift's side
};

enum ProjKind : u8 { PK_BOMB, PK_ROCKET, PK_CANNON, PK_WAFFLE, PK_FLARE };
struct Projectile { vec2 pos, vel; f32 life, z, vz; int owner; u8 kind; int target; vec2 aim; };
enum HazKind : u8 { HZ_OIL, HZ_FIRE };
struct Hazard     { vec2 pos; f32 life; int owner; u8 kind; f32 warn; }; // warn > 0: not burning yet
constexpr f32 FIRE_WARN = 1.0f, FIRE_BURN = 2.0f; // dragon fire: scorch marks, then flame
struct Particle   { vec2 pos, vel; f32 z, vz, life, max_life; u32 color; f32 size; bool ground; };

enum class RacePhase : u8 { Countdown, Racing, Done };

// One tick of a recorded run, for a time trial's ghost.
struct GhostSample { f32 x, y, heading, total; u32 flags; }; // total: distance raced so far; flags bit 0: drifting

struct Race {
    // time trial: the player alone; the run is recorded, and an earlier one drives beside it
    bool time_trial = false;
    GhostSample* rec = nullptr;
    int  rec_n = 0, rec_cap = 0;
    const GhostSample* ghost = nullptr;
    int  ghost_n = 0, ghost_ch = 0;
    f32  ghost_time = 0;   // the ghost's finishing time, for the HUD (0: unknown)
    f32  moon = 0;        // the graveyard: 0 the moon is hidden, 1 it is out and the dead reach up
    vec2 hands[48];       // where they come up
    int  nhands = 0;
    Track track;
    Racer racers[RACERS];
    Array<Projectile> bombs;
    Array<Hazard>     oils;
    Array<Particle>   parts;
    RacePhase phase = RacePhase::Countdown;
    f32   clock = 0;         // since the countdown began
    f32   race_time = 0;     // since GO
    f32   done_timer = 0;
    f32   final_lap_banner = 0;
    f32   emp_flash = 0;     // the screen flash of an EMP
    int   player = 0;        // index of the human racer, -1 in attract mode
    bool  autopilot = false; // the player's kart drives itself (SPROCKET_AUTOPILOT=1, for testing)
    f32   cpu_level = 1.0f;  // CPU strength: 0 easy, 1 normal, 2 hard
    int   finish_count = 0;
    Rng   rng;
    // camera
    vec2 cam{};
    f32  cam_angle = 0;
    f32  shake = 0;          // camera shake from the set piece
    f32  cam_pull = 0;       // the camera drops back under boost
    // up to four players: their racers (the first is `player`) and a camera each (the first is `cam`)
    int  humans = 1;
    int  human_idx[4] = {-1, -1, -1, -1};
    int  player2 = -1;       // human_idx[1]: asked wherever the question is only "is this multiplayer"
    vec2 cams[4] = {};
    f32  cam_angles[4] = {}, cam_pulls[4] = {};
    f32  gate_t[8] = {};     // timers for the track's fire gates
};

// `players` 2..4 with `chars` (one character each, the first being player_char) makes it multiplayer
void race_init(Race& r, const TrackDef& def, int player_char, bool attract, Allocator* alloc, int players = 1, const int* chars = nullptr);
void race_free(Race& r);
void setpiece_init(Race& r);
void setpiece_tick(Race& r, f32 dt);
void race_tick(Race& r, const Controls& human, f32 dt, const Controls* more = nullptr); // more: players two onward
// Call straight after race_init: the rivals leave, the item boxes go, the player gets three
// nitros, the run is recorded into `rec`, and `ghost` (if any) is shown driving its own run.
void race_time_trial(Race& r, GhostSample* rec, int rec_cap, const GhostSample* ghost, int ghost_n, int ghost_ch);
void race_camera(Race& r, f32 dt, bool snap);

// ---- drawing -------------------------------------------------------------------------------
void draw_world(Frame& f, const Race& r);
// one player's view (0 or 1) into a frame of any size: `horizon` is the row of the skyline,
// `cam_h` the camera's height (lower for a short frame, so the kart stays in the picture)
void draw_world_view(Frame& f, const Race& r, int view, int horizon, f32 cam_h);
void draw_hud(Frame& f, const Race& r, u32 tick);
void draw_text(Frame& f, int x, int y, const char* s, u32 color, int scale = 1, bool shadow = true);
int  text_width(const char* s, int scale = 1);
void draw_text_c(Frame& f, int cx, int y, const char* s, u32 color, int scale = 1);
void fill_rect(Frame& f, int x, int y, int w, int h, u32 color);
void dim_rect(Frame& f, int x, int y, int w, int h, f32 k);
void draw_kart_icon(Frame& f, int ch, int angle, int x, int y, int scale);
void draw_prop_img(Frame& f, const PropImg& img, int x, int y, int scale);
void draw_minimap(Frame& f, const Race& r, int x, int y, bool racers);
void draw_minimap_track(Frame& f, const Track& t, int x, int y);
const PropImg* item_icon(int item);

// ---- sound (snes_audio.cc) ----------------------------------------------------------------
// A track's song is its theme's: k_themes[theme].song (not every race song is in theme order).
enum SongId { SONG_TITLE, SONG_PARK, SONG_DUNE, SONG_FROST, SONG_CASTLE, SONG_PIRATE, SONG_JUNGLE, SONG_CYBER, SONG_RESULTS,
              SONG_MENU, SONG_FINISH, SONG_TROPHY, SONG_KITCHEN, SONG_SEWER, SONG_CIRCUIT, SONG_SPACE, SONG_HAUNT, SONG_COUNT };
enum SfxId {
    SFX_COUNT_BEEP, SFX_GO, SFX_MENU_MOVE, SFX_MENU_OK, SFX_ITEM_BOX, SFX_ROULETTE, SFX_ITEM_GET, SFX_BOLT,
    SFX_HOP, SFX_BOOST, SFX_MINITURBO, SFX_THROW, SFX_EXPLODE, SFX_HIT, SFX_OIL, SFX_WALL, SFX_LAP,
    SFX_FINAL_LAP, SFX_SHIELD, SFX_CHARGE, SFX_SKID,
    // the quality pass
    SFX_SPLASH, SFX_FALL, SFX_RESCUE, SFX_TRAIN_HORN, SFX_CANNON, SFX_ROAR, SFX_FIRE, SFX_STOMP, SFX_ZAP,
    SFX_ROCKET, SFX_RAMP, SFX_LAND, SFX_WHOOSH, SFX_RUMBLE, SFX_DROP, SFX_MAGNET, SFX_START_BOOST,
    SFX_BEST_LAP, SFX_PAUSE, SFX_BACK, SFX_BUMP, SFX_WRONG_WAY, SFX_STALL, SFX_DRAFT, SFX_BELL,
    SFX_COUNT
};
bool snd_start(kv::Engine& engine);
void snd_stop(kv::Engine& engine);
void snd_music(int song);     // -1 stops
void snd_sfx(int id);
void snd_engine(bool on, f32 speed);
void snd_final_lap(bool on);  // the music picks up for the last lap
void snd_volume(f32 gain);    // the master volume, 0..1
// The instrument samples, from assets/samples.bin. Before the first load the synth is silent;
// a later load swaps the samples under the music as it plays. False (with why in `err`) if
// the file cannot be used, leaving what was loaded in place.
bool snd_load_bank(const char* path, char* err, int cap);
// The sprites, from assets/art.bin, into the names art.h declares; again to reload them.
bool art_load(const char* path, char* err, int cap);

} // namespace sr
