// Tracks: a closed spline through hand-placed nodes becomes a 1024x1024 Mode 7 map (palette
// indices plus a surface type per texel). The road's width varies node to node; features
// placed along it (shoulders and open edges, ice and mud and fords, boost pads, launch ramps
// and the gaps they clear, dividers, crystals, bolts) and terrain stamped around it (lakes,
// rivers, moats, canyons, walls) make each layout. Seven themes bring palette, road surface,
// sky, scenery and set piece.
#include "common.h"

namespace sr {

#define RGB(r, g, b) ((u32)(0xff000000u | (((u32)(b) >> 3) * 255 / 31) << 16 | (((u32)(g) >> 3) * 255 / 31) << 8 | (((u32)(r) >> 3) * 255 / 31)))
#define RGB(r, g, b) ((u32)(0xff000000u | (((u32)(b) >> 3) * 255 / 31) << 16 | (((u32)(g) >> 3) * 255 / 31) << 8 | (((u32)(r) >> 3) * 255 / 31)))

// ---- themes ---------------------------------------------------------------------------------
static void common_pal(Theme& t) {
    t.pal[C_LINE] = RGB(232, 232, 232);
    t.pal[C_START_A] = RGB(248, 248, 248); t.pal[C_START_B] = RGB(24, 24, 32);
    t.pal[C_BOOST0] = RGB(248, 200, 40); t.pal[C_BOOST1] = RGB(248, 120, 24);
    t.pal[C_WATER0] = RGB(40, 96, 200); t.pal[C_WATER1] = RGB(64, 128, 224); t.pal[C_WATER2] = RGB(112, 176, 248);
    t.pal[C_PIT0] = RGB(16, 12, 20); t.pal[C_PIT1] = RGB(40, 32, 44);
    t.pal[C_MUD0] = RGB(112, 80, 48); t.pal[C_MUD1] = RGB(88, 60, 36);
    t.pal[C_ICE0] = RGB(200, 232, 248); t.pal[C_ICE1] = RGB(160, 208, 240);
    t.pal[C_RAMP0] = RGB(248, 248, 248); t.pal[C_RAMP1] = RGB(48, 120, 232);
    t.pal[C_SHAL0] = RGB(96, 168, 232); t.pal[C_SHAL1] = RGB(144, 200, 248);
    t.pal[C_RAIL] = RGB(176, 176, 184); t.pal[C_TIE] = RGB(96, 64, 40); t.pal[C_LANE] = RGB(248, 232, 96);
}

static Theme theme_meadow() {
    Theme t{}; t.gravity = 1.0f; t.name = "MEADOW"; common_pal(t);
    t.pal[C_ROAD0] = RGB(112, 112, 120); t.pal[C_ROAD1] = RGB(104, 104, 112); t.pal[C_ROAD2] = RGB(120, 120, 128);
    t.pal[C_CURB_A] = RGB(224, 40, 40); t.pal[C_CURB_B] = RGB(248, 248, 248);
    t.pal[C_OFF0] = RGB(72, 168, 64); t.pal[C_OFF1] = RGB(64, 152, 56); t.pal[C_OFF2] = RGB(88, 184, 72);
    t.pal[C_WALL0] = RGB(200, 160, 72); t.pal[C_WALL1] = RGB(152, 104, 40);
    t.pal[C_OUT0] = RGB(56, 136, 48); t.pal[C_OUT1] = RGB(48, 120, 40);
    t.sky_top = RGB(64, 120, 232); t.sky_bottom = RGB(176, 216, 248);
    t.far_a = RGB(120, 144, 200); t.far_b = RGB(160, 184, 224); t.near_a = RGB(48, 128, 56); t.near_b = RGB(32, 96, 48);
    t.song = SONG_PARK + 0; t.bg = BG_MEADOW; t.road = RS_ASPHALT; t.grip = 1.0f; t.decor = P_TREE; t.obstacle = P_TREE;
    t.edge = S_WATER; t.setpiece = SPK_BLIMP; t.extra = P_SHRUB;
    return t;
}
static Theme theme_desert() {
    Theme t{}; t.gravity = 1.0f; t.name = "DESERT"; common_pal(t);
    t.pal[C_ROAD0] = RGB(136, 112, 96); t.pal[C_ROAD1] = RGB(128, 104, 88); t.pal[C_ROAD2] = RGB(144, 120, 104);
    t.pal[C_LINE] = RGB(240, 232, 216);
    t.pal[C_CURB_A] = RGB(56, 88, 200); t.pal[C_CURB_B] = RGB(240, 240, 240);
    t.pal[C_OFF0] = RGB(232, 192, 112); t.pal[C_OFF1] = RGB(216, 176, 96); t.pal[C_OFF2] = RGB(240, 208, 136);
    t.pal[C_WALL0] = RGB(168, 88, 48); t.pal[C_WALL1] = RGB(120, 56, 32);
    t.pal[C_OUT0] = RGB(224, 176, 96); t.pal[C_OUT1] = RGB(208, 160, 80);
    t.pal[C_PIT0] = RGB(72, 32, 24); t.pal[C_PIT1] = RGB(112, 56, 32);    // the canyon below
    t.pal[C_MUD0] = RGB(248, 216, 152); t.pal[C_MUD1] = RGB(232, 200, 136); // drifted sand
    t.sky_top = RGB(232, 120, 72); t.sky_bottom = RGB(248, 216, 144);
    t.far_a = RGB(184, 96, 80); t.far_b = RGB(208, 128, 96); t.near_a = RGB(176, 104, 56); t.near_b = RGB(144, 80, 40);
    t.song = SONG_PARK + 1; t.bg = BG_DESERT; t.road = RS_ASPHALT; t.grip = 0.95f; t.decor = P_CACTUS; t.obstacle = P_CACTUS;
    t.edge = S_PIT; t.setpiece = SPK_TRAIN; t.extra = P_ROCK;
    return t;
}
static Theme theme_frost() {
    Theme t{}; t.gravity = 1.0f; t.name = "FROST"; common_pal(t);
    t.pal[C_ROAD0] = RGB(144, 160, 184); t.pal[C_ROAD1] = RGB(136, 152, 176); t.pal[C_ROAD2] = RGB(160, 176, 200);
    t.pal[C_LINE] = RGB(248, 248, 248);
    t.pal[C_CURB_A] = RGB(40, 104, 216); t.pal[C_CURB_B] = RGB(248, 248, 248);
    t.pal[C_OFF0] = RGB(232, 240, 248); t.pal[C_OFF1] = RGB(208, 224, 240); t.pal[C_OFF2] = RGB(248, 248, 248);
    t.pal[C_WALL0] = RGB(96, 120, 160); t.pal[C_WALL1] = RGB(64, 80, 120);
    t.pal[C_OUT0] = RGB(40, 80, 160); t.pal[C_OUT1] = RGB(32, 64, 136);
    t.pal[C_WATER0] = RGB(24, 56, 128); t.pal[C_WATER1] = RGB(40, 80, 160); t.pal[C_WATER2] = RGB(96, 144, 208);
    t.sky_top = RGB(40, 48, 112); t.sky_bottom = RGB(160, 176, 224);
    t.far_a = RGB(72, 84, 144); t.far_b = RGB(120, 136, 192); t.near_a = RGB(48, 88, 96); t.near_b = RGB(32, 64, 72);
    t.song = SONG_PARK + 2; t.bg = BG_FROST; t.road = RS_ASPHALT; t.grip = 0.6f; t.decor = P_PINE; t.obstacle = P_PINE;
    t.edge = S_WATER; t.setpiece = SPK_SNOWBALL; t.extra = P_SNOWMAN;
    return t;
}
static Theme theme_castle() {
    Theme t{}; t.gravity = 1.0f; t.name = "CASTLE"; common_pal(t);
    t.pal[C_ROAD0] = RGB(128, 120, 112); t.pal[C_ROAD1] = RGB(104, 96, 92); t.pal[C_ROAD2] = RGB(152, 144, 132);
    t.pal[C_LINE] = RGB(216, 192, 120);
    t.pal[C_CURB_A] = RGB(176, 32, 40); t.pal[C_CURB_B] = RGB(232, 192, 64);
    t.pal[C_OFF0] = RGB(64, 120, 56); t.pal[C_OFF1] = RGB(56, 104, 48); t.pal[C_OFF2] = RGB(88, 136, 64);
    t.pal[C_WALL0] = RGB(168, 160, 152); t.pal[C_WALL1] = RGB(112, 104, 104);
    t.pal[C_OUT0] = RGB(48, 96, 48); t.pal[C_OUT1] = RGB(40, 84, 44);
    t.pal[C_WATER0] = RGB(32, 64, 96); t.pal[C_WATER1] = RGB(48, 88, 120); t.pal[C_WATER2] = RGB(88, 136, 160); // the moat
    t.sky_top = RGB(56, 32, 96); t.sky_bottom = RGB(232, 136, 96);
    t.far_a = RGB(72, 56, 104); t.far_b = RGB(104, 80, 128); t.near_a = RGB(32, 56, 48); t.near_b = RGB(24, 40, 36);
    t.song = SONG_PARK + 3; t.bg = BG_CASTLE; t.road = RS_COBBLE; t.grip = 0.97f; t.decor = P_TOWER; t.obstacle = P_BANNER;
    t.edge = S_WATER; t.setpiece = SPK_DRAGON; t.extra = P_TORCH;
    return t;
}
static Theme theme_pirate() {
    Theme t{}; t.gravity = 1.0f; t.name = "PIRATE COVE"; common_pal(t);
    t.pal[C_ROAD0] = RGB(168, 112, 64); t.pal[C_ROAD1] = RGB(144, 96, 56); t.pal[C_ROAD2] = RGB(104, 64, 40); // planks
    t.pal[C_LINE] = RGB(248, 232, 200);
    t.pal[C_CURB_A] = RGB(232, 216, 176); t.pal[C_CURB_B] = RGB(120, 80, 48); // rope
    t.pal[C_OFF0] = RGB(248, 224, 160); t.pal[C_OFF1] = RGB(240, 208, 144); t.pal[C_OFF2] = RGB(255, 240, 200);
    t.pal[C_WALL0] = RGB(120, 80, 48); t.pal[C_WALL1] = RGB(88, 56, 32);
    t.pal[C_OUT0] = RGB(40, 120, 200); t.pal[C_OUT1] = RGB(48, 136, 216); // open sea
    t.pal[C_WATER0] = RGB(32, 144, 192); t.pal[C_WATER1] = RGB(56, 168, 208); t.pal[C_WATER2] = RGB(144, 216, 240);
    t.sky_top = RGB(48, 128, 232); t.sky_bottom = RGB(184, 232, 248);
    t.far_a = RGB(64, 136, 200); t.far_b = RGB(72, 152, 96); t.near_a = RGB(120, 80, 48); t.near_b = RGB(56, 144, 64);
    t.song = SONG_PARK + 4; t.bg = BG_PIRATE; t.road = RS_PLANKS; t.grip = 1.0f; t.decor = P_PALM; t.obstacle = P_BARREL;
    t.edge = S_WATER; t.setpiece = SPK_SHIP; t.extra = P_CRATE;
    return t;
}
static Theme theme_jungle() {
    Theme t{}; t.gravity = 1.0f; t.name = "JUNGLE"; common_pal(t);
    t.pal[C_ROAD0] = RGB(160, 96, 56); t.pal[C_ROAD1] = RGB(144, 84, 48); t.pal[C_ROAD2] = RGB(176, 112, 64); // red clay
    t.pal[C_LINE] = RGB(216, 176, 112);
    t.pal[C_CURB_A] = RGB(120, 72, 40); t.pal[C_CURB_B] = RGB(200, 160, 104); // logs
    t.pal[C_OFF0] = RGB(48, 128, 48); t.pal[C_OFF1] = RGB(40, 112, 40); t.pal[C_OFF2] = RGB(80, 160, 56);
    t.pal[C_WALL0] = RGB(112, 104, 88); t.pal[C_WALL1] = RGB(80, 72, 64);
    t.pal[C_OUT0] = RGB(24, 80, 32); t.pal[C_OUT1] = RGB(32, 96, 40);
    t.pal[C_WATER0] = RGB(40, 104, 112); t.pal[C_WATER1] = RGB(56, 128, 128); t.pal[C_WATER2] = RGB(120, 184, 168); // river
    t.pal[C_MUD0] = RGB(56, 40, 32); t.pal[C_MUD1] = RGB(32, 24, 24); // tar
    t.sky_top = RGB(88, 168, 152); t.sky_bottom = RGB(232, 224, 160);
    t.far_a = RGB(88, 112, 104); t.far_b = RGB(64, 72, 72); t.near_a = RGB(32, 104, 40); t.near_b = RGB(24, 80, 32);
    t.song = SONG_PARK + 5; t.bg = BG_JUNGLE; t.road = RS_DIRT; t.grip = 0.95f; t.decor = P_JTREE; t.obstacle = P_FERN;
    t.edge = S_WATER; t.setpiece = SPK_DINO; t.extra = P_BONES;
    return t;
}
static Theme theme_cyber() {
    Theme t{}; t.gravity = 1.0f; t.name = "NEON CITY"; common_pal(t);
    t.pal[C_ROAD0] = RGB(40, 36, 56); t.pal[C_ROAD1] = RGB(48, 44, 64); t.pal[C_ROAD2] = RGB(64, 56, 88);
    t.pal[C_LINE] = RGB(64, 248, 248);
    t.pal[C_CURB_A] = RGB(248, 48, 200); t.pal[C_CURB_B] = RGB(40, 24, 64);
    t.pal[C_OFF0] = RGB(56, 56, 72); t.pal[C_OFF1] = RGB(48, 48, 64); t.pal[C_OFF2] = RGB(96, 248, 248); // rooftop grid
    t.pal[C_WALL0] = RGB(248, 48, 200); t.pal[C_WALL1] = RGB(64, 248, 248);
    t.pal[C_OUT0] = RGB(16, 16, 32); t.pal[C_OUT1] = RGB(24, 20, 40);
    t.pal[C_PIT0] = RGB(8, 8, 20); t.pal[C_PIT1] = RGB(248, 200, 80); // the street far below, and its lights
    t.pal[C_LANE] = RGB(248, 48, 200);
    t.sky_top = RGB(16, 8, 40); t.sky_bottom = RGB(160, 40, 128);
    t.far_a = RGB(40, 32, 72); t.far_b = RGB(248, 216, 96); t.near_a = RGB(24, 20, 48); t.near_b = RGB(64, 248, 248);
    t.song = SONG_PARK + 6; t.bg = BG_CYBER; t.road = RS_NEON; t.grip = 1.0f; t.decor = P_NEON; t.obstacle = P_LAMP;
    t.edge = S_PIT; t.setpiece = SPK_TRAFFIC; t.stars = true; t.extra = P_BARRIER;
    return t;
}

static Theme theme_kitchen() {
    Theme t{}; t.gravity = 1.0f; t.name = "KITCHEN"; common_pal(t);
    t.pal[C_ROAD0] = RGB(200, 160, 112); t.pal[C_ROAD1] = RGB(184, 144, 96); t.pal[C_ROAD2] = RGB(216, 176, 128); // the table's wood
    t.pal[C_LINE] = RGB(248, 248, 240);
    t.pal[C_CURB_A] = RGB(240, 80, 120); t.pal[C_CURB_B] = RGB(248, 208, 64);                                      // cereal loops
    t.pal[C_OFF0] = RGB(216, 56, 56); t.pal[C_OFF1] = RGB(248, 244, 236); t.pal[C_OFF2] = RGB(236, 150, 146);     // gingham
    t.pal[C_WALL0] = RGB(216, 220, 232); t.pal[C_WALL1] = RGB(152, 160, 176);                                     // cutlery
    t.pal[C_OUT0] = RGB(56, 112, 112); t.pal[C_OUT1] = RGB(160, 152, 128);                                        // the floor, far below
    t.pal[C_PIT0] = RGB(56, 112, 112); t.pal[C_PIT1] = RGB(160, 152, 128);
    t.pal[C_WATER0] = RGB(240, 244, 248); t.pal[C_WATER1] = RGB(224, 232, 244); t.pal[C_WATER2] = RGB(248, 248, 255); // milk
    t.pal[C_MUD0] = RGB(184, 104, 24); t.pal[C_MUD1] = RGB(144, 72, 16);                                          // syrup
    t.pal[C_ICE0] = RGB(252, 236, 128); t.pal[C_ICE1] = RGB(244, 216, 96);                                        // butter
    t.pal[C_SHAL0] = RGB(236, 240, 248); t.pal[C_SHAL1] = RGB(212, 224, 244);                                     // spilt milk
    t.pal[C_RAMP0] = RGB(232, 236, 244); t.pal[C_RAMP1] = RGB(144, 152, 176);                                     // a spoon
    t.pal[C_DEC0] = RGB(244, 244, 248); t.pal[C_DEC1] = RGB(72, 120, 216);                                        // a plate and its rim
    t.pal[C_DEC2] = RGB(232, 176, 80); t.pal[C_DEC3] = RGB(184, 120, 40);                                         // waffle
    t.pal[C_DEC4] = RGB(40, 168, 168); t.pal[C_DEC5] = RGB(136, 72, 184);                                         // 90s teal and purple
    t.sky_top = RGB(248, 232, 184); t.sky_bottom = RGB(240, 200, 160);
    t.far_a = RGB(152, 104, 64); t.far_b = RGB(200, 152, 96); t.near_a = RGB(72, 136, 136); t.near_b = RGB(224, 216, 184);
    t.song = SONG_KITCHEN; t.bg = BG_KITCHEN; t.road = RS_TABLE; t.grip = 1.0f; t.decor = P_NONE; t.obstacle = P_NONE;
    t.edge = S_WATER; t.setpiece = SPK_TOASTER; t.extra = P_NONE;
    return t;
}

static Theme theme_sewer() {
    Theme t{}; t.gravity = 1.0f; t.name = "SEWER"; common_pal(t);
    t.pal[C_ROAD0] = RGB(104, 112, 104); t.pal[C_ROAD1] = RGB(88, 96, 92); t.pal[C_ROAD2] = RGB(120, 128, 116); // wet concrete
    t.pal[C_LINE] = RGB(200, 208, 160);
    t.pal[C_CURB_A] = RGB(232, 200, 48); t.pal[C_CURB_B] = RGB(40, 40, 40);                                       // hazard stripes
    t.pal[C_OFF0] = RGB(72, 80, 76); t.pal[C_OFF1] = RGB(64, 72, 68); t.pal[C_OFF2] = RGB(96, 104, 88);
    t.pal[C_WALL0] = RGB(136, 72, 56); t.pal[C_WALL1] = RGB(96, 48, 40);                                          // brick
    t.pal[C_OUT0] = RGB(40, 32, 32); t.pal[C_OUT1] = RGB(32, 28, 28);
    t.pal[C_WATER0] = RGB(48, 120, 88); t.pal[C_WATER1] = RGB(64, 144, 96); t.pal[C_WATER2] = RGB(112, 184, 120);  // the flow
    t.pal[C_MUD0] = RGB(120, 216, 64); t.pal[C_MUD1] = RGB(80, 168, 48);                                          // slime
    t.pal[C_SHAL0] = RGB(72, 136, 120); t.pal[C_SHAL1] = RGB(96, 160, 136);
    t.sky_top = RGB(16, 20, 24); t.sky_bottom = RGB(48, 56, 56);
    t.far_a = RGB(96, 48, 40); t.far_b = RGB(136, 72, 56); t.near_a = RGB(56, 64, 60); t.near_b = RGB(40, 44, 44);
    t.song = SONG_SEWER; t.bg = BG_SEWER; t.road = RS_SEWER; t.grip = 0.92f; t.decor = P_NONE; t.obstacle = P_TOXIC;
    t.edge = S_WATER; t.setpiece = SPK_WAVE; t.extra = P_NONE;
    return t;
}
static Theme theme_pcb() {
    Theme t{}; t.gravity = 1.0f; t.name = "CIRCUIT BOARD"; common_pal(t);
    t.pal[C_ROAD0] = RGB(208, 152, 72); t.pal[C_ROAD1] = RGB(184, 128, 56); t.pal[C_ROAD2] = RGB(232, 184, 104);   // a copper trace
    t.pal[C_LINE] = RGB(224, 228, 236);
    t.pal[C_CURB_A] = RGB(208, 212, 220); t.pal[C_CURB_B] = RGB(232, 192, 72);                                     // solder pads
    t.pal[C_OFF0] = RGB(24, 112, 64); t.pal[C_OFF1] = RGB(28, 124, 72); t.pal[C_OFF2] = RGB(56, 152, 88);          // the board and its traces
    t.pal[C_WALL0] = RGB(40, 40, 48); t.pal[C_WALL1] = RGB(64, 64, 72);
    t.pal[C_OUT0] = RGB(40, 36, 44); t.pal[C_OUT1] = RGB(56, 52, 60);
    t.pal[C_PIT0] = RGB(40, 36, 44); t.pal[C_PIT1] = RGB(56, 52, 60);
    t.pal[C_MUD0] = RGB(220, 220, 228); t.pal[C_MUD1] = RGB(190, 190, 200);                                        // thermal paste
    t.pal[C_DEC0] = RGB(96, 192, 128); t.pal[C_DEC1] = RGB(232, 192, 72); t.pal[C_DEC2] = RGB(232, 236, 232); t.pal[C_DEC3] = RGB(16, 72, 40); t.pal[C_DEC4] = RGB(216, 184, 136); t.pal[C_DEC5] = RGB(176, 56, 40);
    t.sky_top = RGB(40, 36, 32); t.sky_bottom = RGB(112, 104, 88);
    t.far_a = RGB(168, 160, 136); t.far_b = RGB(200, 192, 168); t.near_a = RGB(96, 92, 84); t.near_b = RGB(56, 52, 48);
    t.song = SONG_CIRCUIT; t.bg = BG_PC; t.road = RS_PCB; t.grip = 1.0f; t.decor = P_NONE; t.obstacle = P_NONE;
    t.edge = S_PIT; t.setpiece = SPK_BUG; t.extra = P_NONE; t.fire_look = 1;
    return t;
}
static Theme theme_space() {
    Theme t{}; t.gravity = 0.55f; t.name = "DEEP SPACE"; common_pal(t);
    t.pal[C_ROAD0] = RGB(40, 32, 96); t.pal[C_ROAD1] = RGB(56, 44, 128); t.pal[C_ROAD2] = RGB(96, 200, 248);
    t.pal[C_LINE] = RGB(248, 248, 255);
    t.pal[C_CURB_A] = RGB(248, 64, 200); t.pal[C_CURB_B] = RGB(64, 232, 248);
    t.pal[C_OFF0] = RGB(24, 20, 56); t.pal[C_OFF1] = RGB(24, 20, 56); t.pal[C_OFF2] = RGB(64, 56, 120);
    t.pal[C_WALL0] = RGB(120, 128, 160); t.pal[C_WALL1] = RGB(72, 80, 112);
    t.pal[C_OUT0] = RGB(8, 8, 24); t.pal[C_OUT1] = RGB(8, 8, 24);
    t.pal[C_PIT0] = RGB(8, 8, 24); t.pal[C_PIT1] = RGB(255, 255, 255);
    t.pal[C_DEC0] = RGB(40, 16, 64); t.pal[C_DEC1] = RGB(255, 232, 160); t.pal[C_DEC2] = RGB(24, 24, 72);
    t.sky_top = RGB(4, 4, 16); t.sky_bottom = RGB(24, 16, 56);
    t.far_a = RGB(72, 56, 136); t.far_b = RGB(200, 200, 248); t.near_a = RGB(255, 255, 255); t.near_b = RGB(160, 176, 255);
    t.song = SONG_SPACE; t.bg = BG_SPACE; t.road = RS_SPACE; t.grip = 1.0f; t.decor = P_NONE; t.obstacle = P_NONE;
    t.edge = S_PIT; t.setpiece = SPK_COMET; t.extra = P_NONE; t.stars = true; t.fire_look = 2;
    return t;
}
static Theme theme_haunt() {
    Theme t{}; t.gravity = 1.0f; t.name = "HAUNTED"; common_pal(t);
    t.pal[C_ROAD0] = RGB(88, 80, 72); t.pal[C_ROAD1] = RGB(72, 64, 60); t.pal[C_ROAD2] = RGB(104, 96, 84);        // a graveyard path
    t.pal[C_LINE] = RGB(144, 136, 120);
    t.pal[C_CURB_A] = RGB(56, 48, 64); t.pal[C_CURB_B] = RGB(112, 104, 112);
    t.pal[C_OFF0] = RGB(48, 72, 56); t.pal[C_OFF1] = RGB(40, 60, 50); t.pal[C_OFF2] = RGB(72, 96, 64);
    t.pal[C_WALL0] = RGB(72, 64, 88); t.pal[C_WALL1] = RGB(40, 36, 56);                                          // iron and stone
    t.pal[C_OUT0] = RGB(28, 40, 40); t.pal[C_OUT1] = RGB(24, 32, 36);
    t.pal[C_MUD0] = RGB(56, 40, 32); t.pal[C_MUD1] = RGB(40, 28, 24);
    t.pal[C_WATER0] = RGB(24, 56, 64); t.pal[C_WATER1] = RGB(32, 72, 80); t.pal[C_WATER2] = RGB(64, 112, 112);
    t.pal[C_DEC0] = RGB(112, 72, 44); t.pal[C_DEC1] = RGB(84, 52, 34);                                           // floorboards
    t.pal[C_DEC2] = RGB(152, 32, 48); t.pal[C_DEC3] = RGB(216, 168, 64);                                         // carpet and its trim
    t.pal[C_DEC4] = RGB(88, 48, 104); t.pal[C_DEC5] = RGB(56, 32, 72);                                           // wallpaper
    t.sky_top = RGB(16, 12, 40); t.sky_bottom = RGB(72, 48, 104);
    t.far_a = RGB(32, 24, 56); t.far_b = RGB(248, 216, 120); t.near_a = RGB(20, 28, 32); t.near_b = RGB(32, 40, 44);
    t.song = SONG_HAUNT; t.bg = BG_HAUNT; t.road = RS_HAUNT; t.grip = 0.97f; t.decor = P_TOMB1; t.obstacle = P_DEADTREE;
    t.edge = S_WATER; t.setpiece = SPK_PENDULUM; t.extra = P_TOMB0; t.stars = true;
    return t;
}

const Theme k_themes[] = {theme_meadow(), theme_desert(), theme_frost(), theme_castle(), theme_pirate(), theme_jungle(), theme_cyber(), theme_kitchen(),
                          theme_sewer(), theme_pcb(), theme_space(), theme_haunt()};
const int k_theme_count = (int)(sizeof k_themes / sizeof k_themes[0]);



// ---- the seven tracks -----------------------------------------------------------------------
// Each is laid out around one idea, written at its top. Lateral signs: every loop runs
// east along the bottom and north up the right-hand side unless noted, so left turns
// dominate and "inside" is usually the left (negative) side.
#define N(a) a, (int)(sizeof a / sizeof a[0])

// SPROCKET PARK: the warm-up. A fast lakeside sweeper where the bolts hug the water, a
// hairpin with a grass infield worth cutting on a nitro, and a ramp over the creek.
static const Node park_nodes[] = {
    {300, 892, 0}, {560, 894, 0}, {760, 884, 0}, {872, 838, 0}, {908, 740, 0}, {880, 640, 0}, {912, 540, 0},
    {880, 430, 0}, {790, 330, 0}, {650, 292, 0}, {520, 330, 0}, {450, 440, 0}, {452, 560, 0}, {400, 640, 0},
    {310, 650, 0}, {262, 590, 0}, {262, 460, 0}, {240, 330, 0}, {170, 290, 0}, {128, 350, 0}, {130, 520, 0},
    {132, 700, 0}, {160, 820, 0}, {215, 875, 0}};
static const Feat park_feats[] = {
    {F_BOOST, 0.45f, 0, 0, 0, 0, 0},
    {F_BOXES, 4.4f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOOST, 6.6f, 0, 0.3f, 0, 0, 0},
    {F_SHOULDER, 7.2f, 11.8f, -1, 0, 12, S_WATER},
    {F_BOLTS, 8.0f, 10.8f, -0.7f, -0.7f, 0, 0},
    {F_BOXES, 12.4f, 0, -0.6f, 0.6f, 3, 0},
    {F_SURFACE, 13.3f, 14.3f, 0.25f, 1.0f, 0.6f, S_MUD},
    {F_BOLTS, 16.4f, 18.6f, -0.65f, -0.65f, 0, 0},
    {F_BOOST, 19.25f, 0, 0, 0, 0, 0},
    {F_RAMP, 20.0f, 0, -1, 1, 0, 0},
    {F_GAP, 20.1f, 20.42f, 0, 0, 40, S_WATER},
    {F_BOLTS, 20.0f, 20.5f, 0, 0, 0, 0},
    {F_BOXES, 21.5f, 0, -0.7f, 0.7f, 4, 0},
};
static const Stamp park_stamps[] = {
    {ST_DISC, S_WATER, 668, 498, 0, 0, 172, 0, 8, 0},
    {ST_STRIP, S_WATER, 20, 548, 218, 552, 22, 0, 0, 0},
    {ST_DISC, S_WATER, 224, 552, 0, 0, 27, 0, 6, 0},
};

// DUNE DRIFT: race the train. Two level crossings on straights, a run flat out beside the
// rails, a canyon rim with no barrier, and a launch over the arroyo.
static const Node dune_nodes[] = {
    {240, 892, 0}, {520, 896, 0}, {760, 888, 0}, {868, 840, 0}, {900, 740, 0}, {896, 620, 0}, {894, 500, 0},
    {890, 380, 0}, {872, 250, 0}, {790, 160, 0}, {680, 160, 0}, {628, 240, 0}, {600, 340, 0}, {520, 392, 0},
    {440, 340, 0}, {420, 240, 0}, {360, 165, 0}, {260, 170, 0}, {190, 240, 0}, {180, 360, 0}, {184, 500, 0},
    {200, 600, 0}, {290, 638, 0}, {460, 632, 0}, {610, 640, 0}, {690, 668, 0}, {716, 720, 0}, {680, 770, 0},
    {590, 780, 0}, {440, 776, 0}, {300, 776, 0}, {205, 790, 0}, {152, 838, 0}, {178, 886, 0}};
static const Feat dune_feats[] = {
    {F_BOOST, 0.5f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.6f, 0, -0.7f, 0.7f, 4, 0},
    {F_SHOULDER, 7.6f, 9.7f, 0, 1, 8, S_PIT},
    {F_BOLTS, 8.0f, 9.4f, 0.7f, 0.7f, 0, 0},
    {F_SURFACE, 12.5f, 13.6f, 0.25f, 1.0f, 0.7f, S_MUD},
    {F_BOXES, 15.5f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOOST, 17.3f, 0, 0, 0, 0, 0},
    {F_BOXES, 21.7f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOOST, 22.6f, 0, 0, 0, 0, 0},
    {F_BOLTS, 23.0f, 24.0f, -0.55f, -0.55f, 0, 0},
    {F_SURFACE, 25.2f, 26.5f, 0.25f, 1.0f, 0.7f, S_MUD},
    {F_RAMP, 28.25f, 0, -1, 1, 0, 0},
    {F_GAP, 28.38f, 28.78f, 0, 0, 40, S_PIT},
    {F_BOLTS, 28.3f, 28.8f, 0, 0, 0, 0},
    {F_BOXES, 30.0f, 0, -0.7f, 0.7f, 4, 0},
};
static const Stamp dune_stamps[] = {
    {ST_DISC, S_PIT, 975, 95, 0, 0, 172, 0, 5, 0},
    {ST_STRIP, S_PIT, 498, 718, 498, 852, 30, 0, 0, 0},
};

// FROST RING: Avalanche Alley, where the giant snowball rolls down the straight at you;
// a hairpin of sheet ice; and a frozen lake, wide open, with nothing between you and the water.
static const Node frost_nodes[] = {
    {200, 892, 0}, {500, 896, 0}, {760, 886, 0}, {872, 836, 0}, {906, 730, 0}, {900, 560, 0}, {896, 400, 0},
    {872, 270, 0}, {790, 186, 0}, {660, 170, 42}, {460, 168, 42}, {290, 172, 42}, {180, 214, 0}, {136, 310, 0},
    {132, 440, 0}, {150, 560, 0}, {215, 630, 0}, {300, 610, 0}, {330, 520, 0}, {350, 420, 0}, {430, 350, 0},
    {540, 340, 46}, {650, 380, 46}, {730, 470, 46}, {740, 590, 46}, {670, 690, 46}, {540, 730, 0}, {400, 728, 0},
    {270, 736, 0}, {170, 770, 0}, {126, 836, 0}};
static const Feat frost_feats[] = {
    {F_BOOST, 1.0f, 0, 0, 0, 0, 0},
    {F_BOXES, 4.4f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOOST, 8.5f, 0, 0, 0, 0, 0},
    {F_BOLTS, 9.2f, 10.0f, -0.62f, -0.62f, 0, 0},
    {F_BOLTS, 10.2f, 11.0f, 0.62f, 0.62f, 0, 0},
    {F_BOXES, 12.5f, 0, -0.6f, 0.6f, 3, 0},
    {F_SURFACE, 14.7f, 18.3f, -1, 1, 0, S_ICE},
    {F_BOLTS, 15.4f, 17.6f, -0.6f, -0.6f, 0, 0},
    {F_BOXES, 19.6f, 0, -0.6f, 0.6f, 3, 0},
    {F_SHOULDER, 20.8f, 25.8f, -1, 1, 8, S_WATER},
    {F_SURFACE, 20.9f, 25.7f, -1, 1, 0, S_ICE},
    {F_BOLTS, 21.2f, 22.2f, -0.6f, 0.6f, 0, 0},
    {F_BOLTS, 22.6f, 23.6f, 0.6f, -0.6f, 0, 0},
    {F_BOLTS, 24.0f, 25.0f, -0.6f, 0.6f, 0, 0},
    {F_BOOST, 26.4f, 0, 0, 0, 0, 0},
    {F_BOXES, 27.3f, 0, -0.7f, 0.7f, 4, 0},
};
static const Stamp frost_stamps[] = {
    {ST_DISC, S_WATER, 615, 545, 0, 0, 236, 0, 6, 0},
};

// CROWN KEEP: a fast run round the outside of the moat, then over a drawbridge with no
// rails into the courtyard, a lap of the keep, and out by the north bridge. The loop runs
// west along the bottom, so the outer road turns right.
static const Node castle_nodes[] = {
    {560, 908, 34}, {400, 902, 34}, {250, 850, 34}, {155, 740, 34}, {122, 625, 34}, {146, 545, 30}, {200, 506, 17},
    {262, 500, 17}, {320, 500, 26}, {378, 522, 26}, {425, 585, 26}, {500, 630, 26}, {585, 612, 26}, {640, 545, 26},
    {642, 455, 26}, {590, 388, 26}, {515, 368, 26}, {470, 330, 24}, {462, 262, 17}, {462, 180, 17}, {478, 122, 30},
    {560, 100, 34}, {700, 118, 34}, {830, 196, 34}, {900, 320, 36}, {922, 470, 36}, {906, 620, 36}, {840, 760, 34},
    {720, 862, 34}};
static const Feat castle_feats[] = {
    {F_BOOST, 0.6f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.5f, 0, -0.7f, 0.7f, 4, 0},
    {F_SHOULDER, 5.55f, 6.95f, -1, 1, 0, S_WATER},
    {F_SHOULDER, 6.95f, 18.15f, -1, 1, 130, 0},
    {F_BOXES, 9.4f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOLTS, 10.6f, 15.4f, -0.62f, -0.62f, 0, 0},
    {F_SHOULDER, 18.15f, 19.25f, -1, 1, 0, S_WATER},
    {F_BOOST, 21.0f, 0, 0, 0, 0, 0},
    {F_ISLAND, 24.3f, 26.2f, -0.1f, 0.1f, 0, 0},
    {F_BOXES, 24.9f, 0, -0.8f, -0.3f, 2, 0},
    {F_BOXES, 25.6f, 0, -0.8f, -0.3f, 2, 0},
    {F_BOOST, 24.8f, 0, 0.55f, 0, 0, 0},
    {F_BOOST, 25.6f, 0, 0.55f, 0, 0, 0},
    {F_BOXES, 27.6f, 0, -0.7f, 0.7f, 4, 0},
};
static const Stamp castle_stamps[] = {
    {ST_RING, S_WATER, 512, 500, 0, 0, 255, 335, 0, 0},
    {ST_RING, S_WALL, 512, 500, 0, 0, 246, 255, 0, 0},
    {ST_DISC, S_WALL, 512, 500, 0, 0, 62, 0, 0, 0},
};

// GULLWATER COVE: an atoll. Round the outside on the sand, then straight across the
// lagoon on a pier with no rails and a gap in the middle, under the galleon's guns, and
// home along the beach through the barrels.
static const Node pirate_nodes[] = {
    {260, 890, 0}, {520, 894, 0}, {740, 886, 0}, {862, 838, 0}, {904, 740, 0}, {908, 600, 0}, {900, 460, 0},
    {880, 320, 0}, {820, 210, 0}, {700, 150, 0}, {540, 128, 0}, {380, 140, 0}, {250, 196, 0}, {176, 300, 0},
    {170, 410, 0}, {220, 476, 26}, {310, 486, 18}, {420, 486, 18}, {500, 486, 18}, {600, 486, 18}, {690, 500, 28},
    {748, 560, 0}, {752, 650, 0}, {700, 730, 44}, {580, 768, 44}, {430, 772, 44}, {300, 764, 44}, {200, 790, 0},
    {158, 842, 0}, {190, 884, 0}};
static const Feat pirate_feats[] = {
    {F_BOOST, 0.5f, 0, 0, 0, 0, 0},
    {F_BOXES, 3.5f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOLTS, 5.0f, 7.0f, 0.62f, 0.62f, 0, 0},
    {F_BOOST, 8.6f, 0, 0, 0, 0, 0},
    {F_BOXES, 10.5f, 0, -0.7f, 0.7f, 4, 0},
    {F_SURFACE, 12.3f, 13.4f, -1.0f, -0.25f, 0.7f, S_MUD},
    {F_SHOULDER, 15.5f, 20.3f, -1, 1, 0, S_WATER},
    {F_RAMP, 17.05f, 0, -1, 1, 0, 0},
    {F_GAP, 17.22f, 17.82f, 0, 0, 30, S_WATER},
    {F_BOLTS, 17.1f, 17.9f, 0, 0, 0, 0},
    {F_BOXES, 19.2f, 0, -0.4f, 0.4f, 2, 0},
    {F_POSTS, 23.5f, 26.5f, -0.45f, 0.45f, 40, P_BARREL},
    {F_BOXES, 25.0f, 0, -0.75f, 0.75f, 4, 0},
    {F_BOOST, 27.2f, 0, 0, 0, 0, 0},
};

// FOSSIL FALLS: a river down the middle of the jungle. Leap it at the falls, ford it at
// the bottom, weave the tar pits, and cross the clearing where the long-neck walks: the
// bolts are under its belly.
static const Node jungle_nodes[] = {
    {640, 892, 0}, {780, 884, 0}, {872, 830, 0}, {906, 720, 0}, {880, 600, 0}, {820, 500, 0}, {850, 380, 0},
    {900, 280, 0}, {860, 170, 0}, {760, 128, 0}, {640, 150, 0}, {500, 150, 0}, {360, 150, 0}, {230, 190, 0},
    {160, 290, 0}, {150, 400, 0}, {200, 490, 46}, {300, 540, 46}, {330, 640, 0}, {270, 730, 0}, {170, 760, 0},
    {130, 830, 0}, {180, 888, 0}, {300, 894, 0}, {500, 894, 0}};
static const Feat jungle_feats[] = {
    {F_BOOST, 0.4f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.6f, 0, -0.7f, 0.7f, 4, 0},
    {F_SURFACE, 4.15f, 4.75f, -0.95f, -0.1f, 1, S_MUD},
    {F_SURFACE, 5.2f, 5.8f, 0.1f, 0.95f, 1, S_MUD},
    {F_SURFACE, 6.3f, 6.9f, -0.95f, -0.1f, 1, S_MUD},
    {F_BOXES, 8.5f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOOST, 9.4f, 0, 0, 0, 0, 0},
    {F_RAMP, 10.02f, 0, -1, 1, 0, 0},
    {F_GAP, 10.16f, 10.6f, 0, 0, 36, S_WATER},
    {F_BOLTS, 10.0f, 10.7f, 0, 0, 0, 0},
    {F_BOXES, 12.5f, 0, -0.6f, 0.6f, 3, 0},
    {F_SURFACE, 14.3f, 14.9f, 0.1f, 0.95f, 1, S_MUD},
    {F_BOLTS, 16.15f, 16.85f, 0, 0, 0, 0},
    {F_BOXES, 19.4f, 0, -0.7f, 0.7f, 4, 0},
    {F_SHOULDER, 23.26f, 23.72f, -1, 1, 4, S_WATER},
    {F_SURFACE, 23.28f, 23.7f, -1, 1, 0, S_SHALLOW},
};
static const Stamp jungle_stamps[] = {
    {ST_STRIP, S_WATER, 600, -30, 585, 200, 30, 0, 6, 0},
    {ST_STRIP, S_WATER, 585, 200, 470, 420, 30, 0, 6, 0},
    {ST_STRIP, S_WATER, 470, 420, 440, 640, 30, 0, 6, 0},
    {ST_STRIP, S_WATER, 440, 640, 398, 870, 30, 0, 6, 0},
    {ST_STRIP, S_WATER, 398, 870, 392, 1060, 30, 0, 6, 0},
};

// NEON SPRAWL: city blocks. Square corners that want a drift, a freeway to wind it up on,
// two gaps between rooftops to clear, and four junctions where the traffic does not stop.
static const Node cyber_nodes[] = {
    {220, 896, 0}, {500, 898, 0}, {760, 896, 0}, {862, 880, 0}, {902, 836, 0}, {906, 740, 0}, {904, 640, 0},
    {880, 580, 0}, {820, 560, 0}, {740, 556, 0}, {690, 530, 0}, {668, 470, 0}, {666, 380, 0}, {690, 318, 0},
    {750, 300, 0}, {830, 296, 0}, {884, 270, 0}, {904, 210, 0}, {880, 150, 0}, {810, 128, 0}, {660, 126, 0},
    {480, 126, 0}, {360, 128, 0}, {310, 160, 0}, {296, 230, 0}, {290, 300, 0}, {262, 350, 0}, {200, 362, 0},
    {150, 380, 0}, {130, 440, 0}, {128, 560, 0}, {128, 700, 0}, {132, 800, 0}, {160, 868, 0}};
static const Feat cyber_feats[] = {
    {F_BOOST, 0.4f, 0, 0, 0, 0, 0},
    {F_BOOST, 1.5f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.3f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOXES, 8.6f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOOST, 11.4f, 0, 0, 0, 0, 0},
    {F_BOXES, 14.5f, 0, -0.6f, 0.6f, 3, 0},
    {F_RAMP, 20.06f, 0, -1, 1, 0, 0},
    {F_GAP, 20.18f, 20.5f, 0, 0, 30, S_PIT},
    {F_BOLTS, 20.08f, 20.55f, 0, 0, 0, 0},
    {F_BOXES, 22.7f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOOST, 29.3f, 0, 0, 0, 0, 0},
    {F_RAMP, 31.0f, 0, -1, 1, 0, 0},
    {F_GAP, 31.14f, 31.6f, 0, 0, 30, S_PIT},
    {F_BOLTS, 31.0f, 31.65f, 0, 0, 0, 0},
    {F_BOXES, 32.4f, 0, -0.7f, 0.7f, 4, 0},
};

// BREAKFAST RUN: the karts are tiny and the table is set. Off the placemat, through the
// sugar cubes, round the cereal box, along the top under the toaster's fire, across the
// plate over a waffle (syrup one side, butter the other), off the spoon and over the
// cereal bowl, round the butter, through the spilt milk and home. The tablecloth is slow
// going and the floor is a long way down.
static const Node kitchen_nodes[] = {
    {300, 884, 0}, {560, 886, 0}, {760, 880, 0}, {866, 836, 0}, {900, 740, 0}, {902, 620, 0}, {900, 480, 0},
    {896, 340, 0}, {870, 220, 0}, {790, 150, 0}, {660, 136, 0}, {460, 136, 0}, {280, 142, 0}, {180, 190, 0},
    {148, 290, 0}, {150, 390, 0}, {200, 455, 0}, {300, 470, 0}, {430, 470, 0}, {560, 476, 0}, {720, 490, 0},
    {790, 540, 0}, {806, 630, 0}, {760, 710, 0}, {660, 734, 0}, {500, 730, 0}, {340, 728, 22}, {240, 750, 0},
    {200, 820, 0}, {232, 872, 0}};
static const Feat kitchen_feats[] = {
    {F_BOOST, 0.5f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.4f, 0, -0.7f, 0.7f, 4, 0},
    {F_POSTS, 4.3f, 6.3f, -0.5f, 0.5f, 36, P_SUGAR},
    {F_BOLTS, 7.6f, 9.6f, 0.6f, 0.6f, 0, 0},
    {F_BOOST, 10.3f, 0, 0, 0, 0, 0},
    {F_BOXES, 11.5f, 0, -0.7f, 0.7f, 4, 0},
    {F_SURFACE, 13.4f, 14.5f, -1.0f, -0.2f, 0.8f, S_MUD},
    {F_BOXES, 15.6f, 0, -0.6f, 0.6f, 3, 0},
    {F_PAINT, 17.25f, 18.75f, 0, 0, 0, 1},
    {F_SURFACE, 17.5f, 17.95f, -0.9f, -0.15f, 1, S_MUD},
    {F_SURFACE, 18.15f, 18.6f, 0.15f, 0.9f, 1, S_ICE},
    {F_RAMP, 19.05f, 0, -1, 1, 0, 0},
    {F_GAP, 19.2f, 19.56f, 0, 0, 22, S_WATER},
    {F_BOLTS, 19.05f, 19.62f, 0, 0, 0, 0},
    {F_SURFACE, 22.1f, 23.5f, -0.2f, 1.0f, 0.6f, S_ICE},
    {F_BOXES, 23.9f, 0, -0.6f, 0.6f, 3, 0},
    {F_SURFACE, 24.55f, 25.45f, -1, 1, 0.5f, S_SHALLOW},
    {F_BOLTS, 26.0f, 27.0f, 0, 0, 0, 0},
};
static const Stamp kitchen_stamps[] = {
    {ST_STRIP, S_OFF, 300, 884, 720, 884, 54, 0, 0, 8},      // the placemat under the grid
    {ST_DISC, S_OFF, 365, 470, 0, 0, 104, 0, 0, 2},          // a plate: rim...
    {ST_DISC, S_OFF, 365, 470, 0, 0, 94, 0, 0, 1},           // ...and face
    {ST_STRIP, S_OFF, 318, 470, 412, 470, 47, 0, 0, 7},      // the waffle on it
    {ST_STRIP, S_WALL, 290, 356, 430, 356, 5, 0, 0, 0},      // fork, above the plate
    {ST_STRIP, S_WALL, 430, 346, 452, 346, 2, 0, 0, 0},
    {ST_STRIP, S_WALL, 430, 356, 452, 356, 2, 0, 0, 0},
    {ST_STRIP, S_WALL, 430, 366, 452, 366, 2, 0, 0, 0},
    {ST_STRIP, S_WALL, 290, 580, 440, 580, 5, 0, 0, 0},      // knife, below it
    {ST_STRIP, S_WALL, 300, 596, 392, 596, 3, 0, 0, 0},      // and a teaspoon
    {ST_DISC, S_WALL, 404, 596, 0, 0, 10, 0, 0, 0},
    {ST_DISC, S_WALL, 626, 484, 0, 0, 58, 0, 0, 2},          // the cereal bowl: its rim...
    {ST_DISC, S_WATER, 626, 484, 0, 0, 48, 0, 0, 0},         // ...and the milk in it
    {ST_DISC, S_SHALLOW, 532, 708, 0, 0, 56, 0, 0, 0},       // spilt milk
    {ST_DISC, S_SHALLOW, 590, 690, 0, 0, 30, 0, 0, 0},
    {ST_STRIP, S_OFF, 180, 142, 180, 300, 30, 0, 0, 5},      // a folded napkin
};
static const Obj kitchen_objs[] = {
    {SPI_CEREAL, 748, 312, 0.6f, 24, 69, 0, 0},
    {SPI_TOASTER, 470, 292, 1.5708f, 30, 95, 0, 0},
    {SPI_MUG, 700, 332, 2.4f, 26, 26, 0, 0},
    {SPI_SYRUP, 252, 330, 1.0f, 18, 18, 0, 0},
    {SPI_JUICE, 222, 562, 0, 22, 22, 0, 0},
    {SPI_MILK, 566, 668, 0.3f, 24, 24, 0, 0},
    {SPI_BUTTER, 654, 574, 1.5708f, 15, 42, 0, 0},
};

// SEWER SURF: the main channel is long, wide and straight, and every so often the flush
// comes down it: catch the wave and it carries you. Then slime on the bends, a stretch
// inside a pipe, and a ramp over the sludge.
static const Node sewer_nodes[] = {
    {240, 892, 0}, {520, 896, 0}, {760, 890, 0}, {868, 846, 0}, {906, 750, 40}, {908, 600, 40}, {908, 450, 40},
    {904, 300, 40}, {870, 200, 36}, {790, 140, 0}, {640, 128, 0}, {480, 140, 0}, {360, 150, 0}, {240, 180, 0},
    {160, 260, 0}, {150, 380, 0}, {215, 465, 0}, {330, 486, 0}, {460, 500, 20}, {600, 506, 20}, {700, 530, 0},
    {748, 610, 0}, {720, 700, 0}, {620, 760, 0}, {460, 772, 0}, {300, 764, 0}, {195, 790, 0}, {150, 842, 0},
    {180, 888, 0}};
static const Feat sewer_feats[] = {
    {F_BOOST, 0.5f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.5f, 0, -0.7f, 0.7f, 4, 0},
    {F_PAINT, 3.8f, 9.3f, 0, 0, 0, 2},
    {F_BOLTS, 5.0f, 7.6f, 0, 0, 0, 0},
    {F_BOXES, 10.0f, 0, -0.7f, 0.7f, 4, 0},
    {F_SURFACE, 12.4f, 13.3f, -1.0f, -0.25f, 0.8f, S_MUD},
    {F_SHOULDER, 13.0f, 16.2f, -1, 0, 10, S_WATER},
    {F_BOLTS, 14.0f, 16.0f, -0.6f, -0.6f, 0, 0},
    {F_BOXES, 16.6f, 0, -0.6f, 0.6f, 3, 0},
    {F_SHOULDER, 17.5f, 19.6f, -1, 1, 0, 0},
    {F_BOLTS, 18.0f, 19.0f, 0, 0, 0, 0},
    {F_BOOST, 19.7f, 0, 0, 0, 0, 0},
    {F_SURFACE, 20.4f, 21.3f, 0.25f, 1.0f, 0.8f, S_MUD},
    {F_RAMP, 22.08f, 0, -1, 1, 0, 0},
    {F_GAP, 22.26f, 22.74f, 0, 0, 30, S_WATER},
    {F_BOLTS, 22.1f, 22.8f, 0, 0, 0, 0},
    {F_BOXES, 23.6f, 0, -0.7f, 0.7f, 4, 0},
    {F_POSTS, 24.2f, 25.6f, -0.5f, 0.5f, 44, P_TOXIC},
};
static const Stamp sewer_stamps[] = {
    {ST_DISC, S_WATER, 300, 330, 0, 0, 78, 0, 8, 0},
    {ST_DISC, S_WATER, 672, 742, 0, 0, 62, 0, 6, 0},
};

// CIRCUIT BREAKER: tiny karts on a 1980s motherboard. The road is a copper trace, so its
// corners are forty-five degrees. Up the alley between two rows of chips, through the
// LEDs, past the jumper pins that arc, off the ribbon cable over the expansion slot.
// There is a bug in the system: it is a moth, and it is large.
static const Node pcb_nodes[] = {
    {300, 900, 0}, {560, 900, 0}, {780, 900, 0}, {860, 870, 0}, {910, 790, 0}, {910, 640, 21}, {910, 420, 21},
    {910, 290, 0}, {880, 200, 0}, {800, 160, 0}, {700, 160, 0}, {640, 165, 0}, {585, 215, 0}, {570, 290, 0},
    {570, 380, 0}, {545, 445, 0}, {480, 470, 0}, {330, 470, 0}, {210, 470, 0}, {145, 500, 0}, {120, 570, 0},
    {120, 690, 0}, {120, 800, 0}, {150, 870, 0}, {220, 900, 0}};
static const Feat pcb_feats[] = {
    {F_BOOST, 0.5f, 0, 0, 0, 0, 0},
    {F_POSTS, 0.9f, 1.7f, -0.45f, 0.45f, 40, P_LED_G},
    {F_BOXES, 1.8f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOLTS, 4.4f, 6.6f, 0, 0, 0, 0},
    {F_GATE, 5.5f, 0, 0, 0, 3.6f, 0},
    {F_POSTS, 9.1f, 10.9f, -0.5f, 0.5f, 40, P_LED_R},
    {F_BOXES, 9.8f, 0, -0.6f, 0.6f, 3, 0},
    {F_GATE, 13.5f, 0, 0, 0, 4.4f, 0},
    {F_SURFACE, 16.3f, 16.9f, -1.0f, -0.2f, 0.8f, S_MUD},
    {F_BOXES, 17.2f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOOST, 17.6f, 0, 0, 0, 0, 0},
    {F_RAMP, 20.25f, 0, -1, 1, 0, 0},
    {F_GAP, 20.4f, 20.9f, 0, 0, 26, S_PIT},
    {F_BOLTS, 20.27f, 20.95f, 0, 0, 0, 0},
    {F_BOXES, 21.7f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOOST, 24.3f, 0, 0, 0, 0, 0},
};
// only what stands tall is a sprite; the chips, resistors and sockets are painted flat (pcb_paint_board)
static const Obj pcb_objs[] = {
    {SPI_HEATSINK, 128, 122, 0, 30, 40, 0, 0},
    {SPI_CAPACITOR, 230, 112, 0, 28, 28, 0, 0}, {SPI_CAPACITOR, 300, 112, 0, 28, 28, 0, 0}, {SPI_CAPACITOR, 370, 112, 0, 28, 28, 0, 0},
    {SPI_CAPACITOR, 440, 112, 0, 28, 28, 0, 0},
    {SPI_CERAMIC, 200, 220, 0, 3, 12, 0, 0}, {SPI_CERAMIC, 260, 220, 0, 3, 12, 0, 0}, {SPI_CERAMIC, 320, 220, 0, 3, 12, 0, 0}, {SPI_CERAMIC, 380, 220, 0, 3, 12, 0, 0},
};

// ORBIT RUN: a road through the solar system, with no edges and not much gravity. Round
// the outside of Saturn's rings, leap to the Sun's orbit, slingshot three-quarters of the
// way round it under its spitting flames, and home through the asteroid belt.
static const Node space_nodes[] = {
    {260, 900, 0}, {560, 904, 0}, {780, 890, 0}, {876, 836, 0}, {908, 740, 0}, {910, 560, 0}, {908, 400, 0},
    {904, 290, 0}, {880, 170, 0}, {790, 123, 0}, {700, 160, 0}, {676, 240, 0}, {668, 300, 0}, {600, 318, 0},
    {520, 272, 0}, {400, 205, 0}, {265, 250, 0}, {175, 360, 0}, {165, 470, 0}, {200, 590, 0}, {215, 700, 0},
    {190, 800, 0}, {205, 872, 0}};
static const Feat space_feats[] = {
    {F_BOOST, 0.5f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.4f, 0, -0.7f, 0.7f, 4, 0},
    {F_RAMP, 5.08f, 0, -1, 1, 0, 0},
    {F_GAP, 5.22f, 5.72f, 0, 0, 30, S_PIT},
    {F_BOLTS, 5.1f, 5.8f, 0, 0, 0, 0},
    {F_BOXES, 7.3f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOLTS, 8.0f, 11.0f, -0.6f, -0.6f, 0, 0},
    {F_RAMP, 13.02f, 0, -1, 1, 0, 0},
    {F_GAP, 13.2f, 13.8f, 0, 0, 30, S_PIT},
    {F_BOLTS, 13.05f, 13.85f, 0, 0, 0, 0},
    {F_BOXES, 14.7f, 0, -0.7f, 0.7f, 4, 0},
    {F_BOLTS, 16.0f, 18.0f, 0.62f, 0.62f, 0, 0},
    {F_BOOST, 19.3f, 0, 0, 0, 0, 0},
    {F_BOXES, 20.5f, 0, -0.7f, 0.7f, 4, 0},
};
static const Obj space_objs[] = {
    {SPI_SUN, 400, 440, 0, 0, 0, 110, 0},
    {SPI_SATURN, 790, 238, 0, 0, 0, 46, 0.6f},
    {SPI_JUPITER, 640, 745, 0, 0, 0, 80, 0.8f},
    {SPI_EARTH, 96, 956, 0, 0, 0, 40, 0}, {SPI_MOON, 150, 990, 0, 0, 0, 30, 0},
    {SPI_MARS, 980, 960, 0, 0, 0, 36, 0},
    {SPI_NEPTUNE, 70, 90, 0, 0, 0, 50, 0},
    {SPI_VENUS, 970, 70, 0, 0, 0, 40, 0},
    {SPI_SATELLITE, 520, 520, 0.8f, 0, 0, 30, 0},
};

// GRAVEYARD SHIFT: round the cemetery. A ghost carriage keeps its
// own slow lap; ghosts
// take what you carry while the moon is hidden, and when it comes out the dead reach up.
static const Node haunt_nodes[] = {
    {260, 896, 0}, {540, 898, 0}, {760, 890, 0}, {870, 840, 0}, {906, 740, 0}, {900, 610, 0}, {880, 520, 0},
    {820, 440, 0}, {800, 340, 0}, {760, 240, 0}, {680, 180, 0}, {520, 172, 0}, {360, 180, 0}, {270, 230, 0},
    {240, 330, 0}, {300, 410, 0}, {420, 420, 0}, {540, 410, 0}, {588, 520, 0}, {560, 620, 0}, {460, 680, 0},
    {340, 660, 0}, {240, 700, 0}, {172, 780, 0}, {186, 866, 0}};
static const Feat haunt_feats[] = {
    {F_BOOST, 0.5f, 0, 0, 0, 0, 0},
    {F_BOXES, 2.5f, 0, -0.7f, 0.7f, 4, 0},
    {F_POSTS, 4.2f, 5.6f, -0.5f, 0.5f, 44, P_TOMB0},
    {F_BOXES, 8.6f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOLTS, 10.2f, 11.8f, 0, 0, 0, 0},
    {F_BOXES, 12.6f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOLTS, 14.2f, 15.4f, -0.6f, -0.6f, 0, 0},
    {F_BOXES, 16.5f, 0, -0.6f, 0.6f, 3, 0},
    {F_BOOST, 18.2f, 0, 0, 0, 0, 0},
    {F_SURFACE, 19.5f, 20.4f, 0.25f, 1.0f, 0.8f, S_MUD},
    {F_BOLTS, 21.0f, 22.4f, 0, 0, 0, 0},
    {F_BOXES, 22.8f, 0, -0.7f, 0.7f, 4, 0},
};
const TrackDef k_tracks[] = {
    {"SPROCKET PARK", "LAKESIDE SWEEPERS AND A CREEK TO JUMP", 0, N(park_nodes), 30, N(park_feats), N(park_stamps), 36, 0, {}, 0, 0, nullptr, 0},
    {"DUNE DRIFT", "RACE THE TRAIN TO THE CROSSING", 1, N(dune_nodes), 32, N(dune_feats), N(dune_stamps), 36, 0,
     {-220, 520, 1244, 520}, 0, 0, nullptr, 0},
    {"FROST RING", "SHEET ICE AND ONE VERY LARGE SNOWBALL", 2, N(frost_nodes), 30, N(frost_feats), N(frost_stamps), 36, 0,
     {250, 170, 820, 172}, SPF_ONE_WAY | SPF_WEAVE, 0, nullptr, 0},
    {"CROWN KEEP", "OVER THE DRAWBRIDGE, ROUND THE KEEP", 3, N(castle_nodes), 34, N(castle_feats), N(castle_stamps), 36, 0,
     {512, 500, 215, 0}, 0, 0, nullptr, 0},
    {"GULLWATER COVE", "A PIER WITH NO RAILS AND A SHIP WITH CANNONS", 4, N(pirate_nodes), 31, N(pirate_feats), nullptr, 0, 30, S_WATER, {}, 0, 0, nullptr, 0},
    {"FOSSIL FALLS", "LEAP THE FALLS, MIND THE LEGS", 5, N(jungle_nodes), 30, N(jungle_feats), N(jungle_stamps), 36, 0,
     {110, 600, 400, 440}, SPF_SEGMENT, 0, nullptr, 0},
    {"NEON SPRAWL", "ROOFTOP GAPS AND CROSS TRAFFIC", 6, N(cyber_nodes), 32, N(cyber_feats), nullptr, 0, 12, S_PIT,
     {400, -220, 400, 1244, -220, 470, 1244, 470}, 0, 0, nullptr, 0},
    {"BREAKFAST RUN", "TINY KARTS, ONE VERY LARGE BREAKFAST", 7, N(kitchen_nodes), 30, N(kitchen_feats), N(kitchen_stamps), 36, S_PIT,
     {84, 625, 940, 625}, 0, 0, N(kitchen_objs)},
    {"SEWER SURF", "CATCH THE FLUSH AND RIDE IT", 8, N(sewer_nodes), 30, N(sewer_feats), N(sewer_stamps), 14, 0,
     {4.0f, 9.2f}, 0, 0, nullptr, 0},
    {"CIRCUIT BREAKER", "THERE IS A BUG IN THE SYSTEM", 9, N(pcb_nodes), 30, N(pcb_feats), nullptr, 0, 36, S_PIT,
     {480, 345, 985, 345}, SPF_SEGMENT, 0, N(pcb_objs)},
    {"ORBIT RUN", "NO EDGES AND NOT MUCH GRAVITY", 10, N(space_nodes), 39, N(space_feats), nullptr, 0, 0, S_PIT,
     {20, 110, 330, 660, 430, 1080, 1080, 430}, SPF_ONE_WAY, 0, N(space_objs)},
    {"GRAVEYARD SHIFT", "WHEN THE MOON COMES OUT SO DO THEY", 11, N(haunt_nodes), 30, N(haunt_feats), nullptr, 0, 36, 0,
     {520, 122, 520, 232, 110, 640, 770, 650}, SPF_SEGMENT, 0, nullptr, 0},
};
const int k_track_count = (int)(sizeof k_tracks / sizeof k_tracks[0]);
#undef N

// ---- backgrounds ----------------------------------------------------------------------------
static f32 ridge(f32 x, u32 seed, f32 period) {
    // a smooth periodic height in [0,1]
    f32 h = 0, amp = 0.5f, total = 0;
    for (int o = 0; o < 4; ++o) {
        f32 f = (f32)(1 << o);
        f32 ph = (f32)(hash2((i32)seed, o) % 1000) * 0.00628f;
        h += amp * (0.5f + 0.5f * kv::sin(x * 6.2831853f * f / period + ph));
        total += amp; amp *= 0.5f;
    }
    return h / total;
}

static void layer_put(Array<u32>& L, int w, int x, int y, u32 c) {
    if (y < 0 || y >= 64) return;
    L[(u32)(y * w + (x & (w - 1)))] = c;
}

static void far_mountains(Track& t, u32 seed, bool snow, bool mesa) {
    const Theme& th = *t.theme;
    for (int x = 0; x < 512; ++x) {
        f32 h = ridge((f32)x, seed, 512.0f);
        if (mesa) h = kv::floor(h * 4.0f) / 4.0f;
        int top = 64 - (int)(14 + h * 40);
        for (int y = top; y < 64; ++y) {
            u32 c = (y - top) < 3 ? th.far_b : th.far_a;
            if (snow && y - top < 6 && ((x + y) & 1) == 0) c = rgb(240, 240, 248);
            if (snow && y - top < 3) c = rgb(248, 248, 255);
            if (mesa && ((y - top) % 9) == 4) c = shade(th.far_a, 0.85f);
            layer_put(t.far_layer, 512, x, y, c);
        }
    }
}

// Jagged peaks with snow on their upper slopes: lit on the side facing the light, in
// shadow on the other.
static void far_peaks(Track& t, u32 seed) {
    const Theme& th = *t.theme;
    auto height = [&](int x) {
        f32 h = 0, amp = 0.55f, total = 0;
        for (int o = 0; o < 3; ++o) {
            f32 period = 512.0f / (f32)(3 << o);
            f32 ph = (f32)(hash2((i32)seed, o) % 512);
            f32 f = kv::fmod(((f32)x + ph) / period, 1.0f);
            h += amp * (1.0f - kv::fabs(2.0f * f - 1.0f)); // a triangle wave: straight slopes, sharp tops
            total += amp; amp *= 0.5f;
        }
        return h / total;
    };
    for (int x = 0; x < 512; ++x) {
        f32 h = height(x), hl = height(x - 1);
        int top = 64 - (int)(12.0f + h * 42.0f);
        bool lit = h >= hl; // rising toward the right: the lit face
        int snow = top + 3 + (int)(h * 9.0f) + ((x * 7) % 3);
        for (int y = top; y < 64; ++y) {
            u32 c = y < snow ? (lit ? rgb(240, 244, 255) : rgb(184, 200, 232)) : (lit ? th.far_b : th.far_a);
            layer_put(t.far_layer, 512, x, y, c);
        }
    }
}

static void far_clouds(Track& t, int n, u32 lit, u32 dark) {
    for (int c = 0; c < n; ++c) {
        int cx = (int)(hash2(c, 99) % 512), cy = 6 + (int)(hash2(c, 7) % 20), r = 5 + (int)(hash2(c, 3) % 6);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r * 3; dx <= r * 3; ++dx) {
                f32 e = (f32)(dx * dx) / (f32)(9 * r * r) + (f32)(dy * dy) / (f32)(r * r);
                if (e > 1.0f) continue;
                int x = (cx + dx) & 511, y = cy + dy;
                if (y < 0 || y >= 64 || t.far_layer[(u32)(y * 512 + x)]) continue;
                t.far_layer[(u32)(y * 512 + x)] = dy > r / 3 ? dark : lit;
            }
    }
}

static void near_treeline(Track& t, u32 seed, bool pointy) {
    const Theme& th = *t.theme;
    for (int x = 0; x < 1024; ++x) {
        f32 h = ridge((f32)x, seed, 256.0f);
        int top = 64 - (int)(4 + h * 14);
        int bump = (int)(hash2(x / 6, 5) % 6), local = x % 6;
        top -= pointy ? (3 - (local < 3 ? local : 5 - local)) + bump : (local == 0 || local == 5 ? 0 : 2) + bump / 2;
        for (int y = top; y < 64; ++y) {
            u32 c = ((x / 2 + y) % 5 == 0) ? th.near_b : th.near_a;
            if (y - top < 2) c = shade(th.near_a, 1.2f);
            layer_put(t.near_layer, 1024, x, y, c);
        }
    }
}

static void build_layers(Track& t) {
    const Theme& th = *t.theme;
    t.far_layer.resize_fill(512 * 64, 0u);
    t.near_layer.resize_fill(1024 * 64, 0u);
    switch (th.bg) {
    case BG_MEADOW: far_mountains(t, 11, false, false); far_clouds(t, 9, rgb(248, 248, 255), rgb(208, 224, 248)); near_treeline(t, 77, false); break;
    case BG_DESERT: far_mountains(t, 12, false, true); near_treeline(t, 78, false); break;
    case BG_FROST:  far_peaks(t, 13); near_treeline(t, 79, true); break;
    case BG_CASTLE: {
        // dusk hills, and on them a castle seen far off: a curtain wall with battlements,
        // round towers at its ends, a taller keep with a pennant. Stone a shade off the
        // hills so it sits in the distance.
        far_mountains(t, 14, false, false);
        const u32 stone = shade(th.far_a, 0.78f), dark = shade(th.far_a, 0.6f), lit = rgb(232, 184, 96);
        for (int c = 0; c < 2; ++c) {
            int cx = 110 + c * 262, base = 64, wall_top = 46;
            auto block = [&](int x0, int x1, int top) {
                for (int x = x0; x <= x1; ++x) {
                    bool merlon = ((x - x0) % 4) < 2; // battlements: two up, two down
                    for (int y = top - (merlon ? 2 : 0); y < base; ++y) layer_put(t.far_layer, 512, x, y, (x == x0 || x == x1) ? dark : stone);
                }
            };
            block(cx - 34, cx + 34, wall_top);            // the curtain wall
            block(cx - 40, cx - 31, wall_top - 8);        // end towers
            block(cx + 31, cx + 40, wall_top - 8);
            block(cx - 9, cx + 9, wall_top - 16);         // the keep
            for (int y = wall_top - 24; y < wall_top - 18; ++y) layer_put(t.far_layer, 512, cx, y, dark); // a pole
            for (int y = 0; y < 3; ++y) for (int x = 1; x <= 4 - y; ++x) layer_put(t.far_layer, 512, cx + x, wall_top - 24 + y, rgb(168, 48, 56)); // and its pennant
            layer_put(t.far_layer, 512, cx - 3, wall_top - 9, lit); layer_put(t.far_layer, 512, cx + 3, wall_top - 9, lit); // windows
            layer_put(t.far_layer, 512, cx - 36, wall_top - 2, lit); layer_put(t.far_layer, 512, cx + 36, wall_top - 2, lit);
            for (int y = wall_top + 6; y < base; ++y) for (int x = -3; x <= 3; ++x) if (y > wall_top + 7 || (x > -3 && x < 3)) layer_put(t.far_layer, 512, cx + x, y, dark); // the gate
        }
        near_treeline(t, 80, true);
    } break;
    case BG_PIRATE: {
        // open sea to the horizon and hazy islands; nearer, a low sand spit with a few palms
        for (int x = 0; x < 512; ++x)
            for (int y = 56; y < 64; ++y) layer_put(t.far_layer, 512, x, y, ((x / 5 + y * 3) % 11 == 0) ? rgb(152, 208, 240) : th.far_a);
        const u32 haze = blend(th.far_b, th.sky_bottom, 0.45f);
        for (int k = 0; k < 4; ++k) {
            int cx = 40 + k * 128 + (int)(hash2(k, 21) % 50), w = 26 + (int)(hash2(k, 22) % 26), hgt = 5 + (int)(hash2(k, 23) % 6);
            for (int x = -w; x <= w; ++x) {
                f32 u = (f32)x / (f32)w;
                int top = 56 - (int)((f32)hgt * (1.0f - u * u) * (0.8f + 0.2f * kv::sin((f32)x * 0.4f)));
                for (int y = top; y < 57; ++y) layer_put(t.far_layer, 512, cx + x, y, haze);
            }
        }
        far_clouds(t, 6, rgb(248, 248, 255), rgb(208, 232, 248));
        const u32 sand = rgb(216, 192, 136), trunk = rgb(104, 76, 52), frond = rgb(48, 112, 64), frond_lit = rgb(72, 144, 80);
        for (int x = 0; x < 1024; ++x) { // the spit: low, long swells
            int top = 61 - (int)(3.0f * (0.5f + 0.5f * kv::sin((f32)x * 6.2831853f / 256.0f)));
            for (int y = top; y < 64; ++y) layer_put(t.near_layer, 1024, x, y, y == top ? rgb(240, 220, 168) : sand);
        }
        for (int p = 0; p < 9; ++p) {
            int px = 50 + p * 113 + (int)(hash2(p, 31) % 40), hgt = 22 + (int)(hash2(p, 32) % 12), lean = (hash2(p, 33) & 1) ? 1 : -1;
            int cx = px, cy = 61;
            for (int i = 0; i <= hgt; ++i) { // a trunk that leans and curves
                f32 u = (f32)i / (f32)hgt;
                cx = px + (int)((f32)lean * 7.0f * u * u);
                cy = 61 - i;
                layer_put(t.near_layer, 1024, cx, cy, trunk);
                if (i < hgt * 2 / 3) layer_put(t.near_layer, 1024, cx + 1, cy, trunk);
            }
            for (int f = 0; f < 7; ++f) { // fronds: arcs that rise a little and droop
                f32 ang = -2.9f + (f32)f * 0.45f;
                f32 len = 9.0f + (f32)(hash2(p, 40 + f) % 4);
                for (int i = 1; i <= (int)len; ++i) {
                    f32 u = (f32)i / len;
                    int fx = cx + (int)(kv::cos(ang) * (f32)i);
                    int fy = cy + (int)(kv::sin(ang) * (f32)i * 0.55f + 5.0f * u * u);
                    layer_put(t.near_layer, 1024, fx, fy, (f & 1) ? frond : frond_lit);
                    if (u < 0.6f) layer_put(t.near_layer, 1024, fx, fy + 1, frond);
                }
            }
        }
    } break;
    case BG_SEWER: {
        // a brick tunnel wall with dark arches, a run of pipe and the odd grating
        const u32 brick = th.far_a, mortar = shade(th.far_a, 0.72f), arch = rgb(16, 18, 20), pipe = rgb(88, 96, 92), pipe_lit = rgb(120, 128, 120);
        for (int x = 0; x < 512; ++x)
            for (int y = 0; y < 64; ++y) {
                int row = y / 5;
                bool m = (y % 5 == 0) || ((x + (row & 1) * 5) % 10 == 0);
                layer_put(t.far_layer, 512, x, y, m ? mortar : (hash2(x / 10, row) % 9 == 0 ? shade(brick, 0.88f) : brick));
            }
        for (int a2 = 0; a2 < 5; ++a2) {
            int cx = 52 + a2 * 102;
            for (int y = 24; y < 64; ++y)
                for (int x = -20; x <= 20; ++x) {
                    int dy = 44 - y;
                    bool in = y >= 44 ? true : (x * x + dy * dy <= 400);
                    if (in) layer_put(t.far_layer, 512, cx + x, y, (x * x + (dy > 0 ? dy * dy : 0) > 300 && y < 46) ? mortar : arch);
                }
        }
        for (int x = 0; x < 512; ++x) { // a pipe along the wall, with collars
            for (int y = 10; y < 15; ++y) layer_put(t.far_layer, 512, x, y, (y == 10) ? pipe_lit : pipe);
            if (x % 64 < 3) for (int y = 9; y < 16; ++y) layer_put(t.far_layer, 512, x, y, pipe_lit);
        }
    } break;
    case BG_PC: {
        // the inside of a beige computer case: vent slots, the power supply and its fan, a
        // drive bay, and a ribbon cable sagging across
        const u32 metal = th.far_a, lit = th.far_b, dark = th.near_b, mid = th.near_a;
        for (int x = 0; x < 512; ++x)
            for (int y = 0; y < 64; ++y) layer_put(t.far_layer, 512, x, y, (y % 16 == 15) ? shade(metal, 0.9f) : metal);
        for (int v = 0; v < 3; ++v) { // vents
            int x0 = 30 + v * 170;
            // (slot by slot, not one run with a modulo test: MSVC's optimizer turned that loop
            // into a write far outside the layer, and the ship build died here on Windows)
            for (int row = 0; row < 6; ++row)
                for (int slot = 0; slot < 6; ++slot)
                    for (int i = 0; i < 6 && slot * 8 + i < 44; ++i) layer_put(t.far_layer, 512, x0 + slot * 8 + i, 8 + row * 4, dark);
        }
        for (int x = 96; x < 170; ++x) for (int y = 6; y < 44; ++y) layer_put(t.far_layer, 512, x, y, (x == 96 || x == 169 || y == 6 || y == 43) ? dark : mid); // the PSU
        for (int dy = -13; dy <= 13; ++dy) for (int dx = -13; dx <= 13; ++dx) { int r2 = dx * dx + dy * dy; if (r2 <= 169) layer_put(t.far_layer, 512, 133 + dx, 25 + dy, (r2 > 130 || (dx * dy) % 5 == 0) ? dark : shade(mid, 0.8f)); }
        for (int x = 290; x < 370; ++x) for (int y = 10; y < 34; ++y) layer_put(t.far_layer, 512, x, y, (x == 290 || x == 369 || y == 10 || y == 33) ? dark : lit); // a drive bay
        for (int x = 300; x < 350; ++x) layer_put(t.far_layer, 512, x, 22, dark);
        layer_put(t.far_layer, 512, 358, 28, rgb(64, 232, 96)); layer_put(t.far_layer, 512, 359, 28, rgb(64, 232, 96));
        for (int x = 0; x < 512; ++x) { // the ribbon cable
            int sag = 48 + (int)(5.0f * kv::sin((f32)x * 6.2831853f / 256.0f));
            for (int y = sag; y < sag + 7; ++y) layer_put(t.far_layer, 512, x, y, y == sag ? rgb(200, 56, 56) : ((y - sag) % 2 ? rgb(176, 176, 184) : rgb(144, 144, 152)));
        }
    } break;
    case BG_SPACE: {
        // mostly nothing: a thin band of the galaxy, and two far-off smudges of nebula
        for (int x = 0; x < 512; ++x) {
            int mid = 30 + (int)(10.0f * kv::sin((f32)x * 6.2831853f / 512.0f));
            for (int y = mid - 5; y <= mid + 5; ++y) {
                u32 h = hash2(x, y);
                int d = y - mid; if (d < 0) d = -d;
                if ((int)(h % 7) > d + 1) layer_put(t.far_layer, 512, x, y, (h % 23 == 0) ? th.far_b : th.far_a);
            }
        }
        for (int k = 0; k < 2; ++k) {
            int cx = 120 + k * 260, cy = 14 + k * 30;
            for (int dy = -5; dy <= 5; ++dy) for (int dx = -12; dx <= 12; ++dx)
                if (dx * dx + dy * dy * 5 <= 140 && ((dx + dy) & 1)) layer_put(t.far_layer, 512, cx + dx, cy + dy, k ? rgb(96, 40, 112) : rgb(40, 72, 128));
        }
    } break;
    case BG_HAUNT: {
        // a low moon, bare hills, and the house on one of them: gabled roofs, a square
        // tower, a few lit windows. Close by, a graveyard rail and bare trees.
        for (int dy = -11; dy <= 11; ++dy) for (int dx = -11; dx <= 11; ++dx) {
            int r2 = dx * dx + dy * dy;
            if (r2 <= 121) layer_put(t.far_layer, 512, 372 + dx, 20 + dy, ((dx + 3) * (dx + 3) + (dy - 2) * (dy - 2) <= 9 || (dx - 4) * (dx - 4) + (dy + 4) * (dy + 4) <= 4) ? rgb(208, 192, 136) : rgb(240, 228, 176));
        }
        const u32 hill = shade(th.far_a, 1.25f), house = th.far_a, lit = th.far_b;
        for (int x = 0; x < 512; ++x) {
            int top = 64 - (int)(8.0f + 9.0f * ridge((f32)x, 31, 512.0f));
            for (int y = top; y < 64; ++y) layer_put(t.far_layer, 512, x, y, hill);
        }
        {
            int hx = 150, base = 56;
            auto rect = [&](int x0, int x1, int y0) { for (int x = x0; x <= x1; ++x) for (int y = y0; y < base; ++y) layer_put(t.far_layer, 512, x, y, house); };
            auto gable = [&](int x0, int x1, int y0) { // a pitched roof over a wall: one pixel in per row
                int half = (x1 - x0) / 2;
                for (int i = 0; i <= half; ++i) for (int x = x0 + i; x <= x1 - i; ++x) layer_put(t.far_layer, 512, x, y0 - 1 - i / 1, house);
            };
            rect(hx - 30, hx + 30, 38); gable(hx - 32, hx + 32, 38);  // the main house
            rect(hx - 44, hx - 30, 44); gable(hx - 46, hx - 28, 44);  // a wing
            rect(hx + 16, hx + 26, 18); gable(hx + 15, hx + 27, 18);  // the tower
            static const int win[][2] = {{-20, 44}, {-8, 44}, {6, 44}, {21, 26}, {21, 34}, {-37, 49}, {-14, 50}};
            for (const auto& w : win) { layer_put(t.far_layer, 512, hx + w[0], w[1], lit); layer_put(t.far_layer, 512, hx + w[0], w[1] + 1, lit); }
        }
        for (int x = 0; x < 1024; ++x) { // the rail and the ground under it
            for (int y = 60; y < 64; ++y) layer_put(t.near_layer, 1024, x, y, th.near_a);
            if (x % 4 == 0) for (int y = 53; y < 60; ++y) layer_put(t.near_layer, 1024, x, y, th.near_b);
            layer_put(t.near_layer, 1024, x, 55, th.near_b);
        }
        for (int k = 0; k < 7; ++k) { // bare trees: a trunk and a few forking branches
            int tx = 70 + k * 146 + (int)(hash2(k, 71) % 50);
            for (int y = 34; y < 60; ++y) { layer_put(t.near_layer, 1024, tx, y, th.near_a); if (y > 44) layer_put(t.near_layer, 1024, tx + 1, y, th.near_a); }
            for (int br = 0; br < 5; ++br) {
                int by = 36 + br * 4, dir2 = (br & 1) ? 1 : -1, len = 6 + (int)(hash2(k, 80 + br) % 6);
                for (int i = 1; i <= len; ++i) layer_put(t.near_layer, 1024, tx + dir2 * i, by - i * 2 / 3, th.near_a);
            }
        }
    } break;
    case BG_JUNGLE: {
        far_mountains(t, 15, false, false);
        // a volcano with a smoking crater
        int vx = 300;
        for (int x = -70; x <= 70; ++x) {
            int top = 64 - (int)(52.0f * (1.0f - (f32)kv::fabs((f32)x) / 70.0f));
            if (top < 14) top = 14;
            for (int y = top; y < 64; ++y) layer_put(t.far_layer, 512, vx + x, y, (top == 14 && y < 17) ? rgb(232, 96, 40) : ((x + y) % 9 == 0 ? rgb(60, 48, 48) : rgb(72, 56, 56))); // a glow only at the crater
        }
        for (int p = 0; p < 6; ++p) { // a thin plume leaning downwind
            int cx = vx + p * 6, cy = 11 - p * 2, r = 2 + p / 2;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r * 2; dx <= r * 2; ++dx)
                    if (dx * dx + dy * dy * 4 <= r * r * 4 && cy + dy >= 0) layer_put(t.far_layer, 512, cx + dx, cy + dy, blend(rgb(168, 160, 152), th.sky_top, (f32)p / 8.0f));
        }
        // canopy
        for (int b = 0; b < 140; ++b) {
            int cx = (int)(hash2(b, 41) % 1024), cy = 44 + (int)(hash2(b, 42) % 14), r = 6 + (int)(hash2(b, 43) % 8);
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if (dx * dx + dy * dy <= r * r) layer_put(t.near_layer, 1024, cx + dx, cy + dy, (dy < -r / 2) ? shade(th.near_a, 1.25f) : ((dx + dy) % 4 == 0 ? th.near_b : th.near_a));
        }
        for (int x = 0; x < 1024; ++x) for (int y = 56; y < 64; ++y) layer_put(t.near_layer, 1024, x, y, th.near_b);
    } break;
    case BG_KITCHEN: {
        // a 1990s kitchen: wall cupboards, a tiled splashback, a counter and its cupboards,
        // a window with the sun in it, a fridge covered in magnets, a wall clock
        for (int x = 0; x < 512; ++x) {
            for (int y = 34; y < 46; ++y) layer_put(t.far_layer, 512, x, y, (((x / 6) + (y / 6)) & 1) ? th.near_a : th.near_b); // tiles
            for (int y = 46; y < 50; ++y) layer_put(t.far_layer, 512, x, y, rgb(232, 224, 208));                              // worktop
            for (int y = 50; y < 64; ++y) layer_put(t.far_layer, 512, x, y, (x % 42 < 2 || y == 50) ? shade(th.far_a, 0.7f) : th.far_a);
            if (x % 42 == 21) for (int y = 55; y < 58; ++y) layer_put(t.far_layer, 512, x, y, rgb(240, 216, 96));           // knobs
        }
        for (int c = 0; c < 9; ++c) { // wall cupboards, with gaps for the window and the fridge
            int x0 = c * 56 + 6;
            if ((x0 > 150 && x0 < 250) || (x0 > 350 && x0 < 420)) continue;
            for (int x = x0; x < x0 + 50; ++x)
                for (int y = 4; y < 28; ++y) layer_put(t.far_layer, 512, x, y, (x == x0 || x == x0 + 49 || y == 4 || y == 27 || x == x0 + 25) ? shade(th.far_a, 0.7f) : th.far_b);
            layer_put(t.far_layer, 512, x0 + 22, 20, rgb(240, 216, 96)); layer_put(t.far_layer, 512, x0 + 28, 20, rgb(240, 216, 96));
        }
        for (int x = 168; x < 240; ++x) // the window
            for (int y = 4; y < 32; ++y) {
                bool frame = x < 171 || x >= 237 || y < 6 || y >= 30 || x == 204 || y == 18;
                bool curtain = (x < 180 || x >= 228) && ((x + y / 3) % 4 != 0);
                layer_put(t.far_layer, 512, x, y, frame ? rgb(248, 248, 248) : (curtain ? rgb(40, 168, 168) : rgb(136, 200, 248)));
            }
        for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) if (dx * dx + dy * dy <= 16) layer_put(t.far_layer, 512, 216 + dx, 12 + dy, rgb(255, 232, 96));
        for (int x = 362; x < 408; ++x) // the fridge
            for (int y = 2; y < 64; ++y) layer_put(t.far_layer, 512, x, y, (x == 362 || x == 407 || y == 22) ? rgb(176, 184, 192) : rgb(240, 240, 244));
        static const int k_mag[][3] = {{370, 30, 0}, {382, 36, 1}, {394, 28, 2}, {376, 44, 3}, {398, 42, 0}};
        for (const auto& m : k_mag) {
            static const u32 cols[4] = {RGB(232, 56, 56), RGB(248, 208, 64), RGB(40, 168, 168), RGB(136, 72, 184)};
            for (int dy = 0; dy < 3; ++dy) for (int dx = 0; dx < 4; ++dx) layer_put(t.far_layer, 512, m[0] + dx, m[1] + dy, cols[m[2]]);
        }
        for (int dy = -7; dy <= 7; ++dy) for (int dx = -7; dx <= 7; ++dx) { // the clock
            int r2 = dx * dx + dy * dy;
            if (r2 <= 49) layer_put(t.far_layer, 512, 300 + dx, 16 + dy, r2 > 36 ? rgb(232, 56, 56) : rgb(248, 248, 240));
        }
        for (int k = 0; k < 5; ++k) { layer_put(t.far_layer, 512, 300, 16 - k, rgb(24, 24, 32)); layer_put(t.far_layer, 512, 300 + k, 16, rgb(24, 24, 32)); }
    } break;
    case BG_CYBER: {
        // a skyline with lit windows far away, darker towers and neon strips close by
        for (int b = 0; b < 46; ++b) {
            int x0 = (int)(hash2(b, 51) % 512), w = 8 + (int)(hash2(b, 52) % 18), top = 10 + (int)(hash2(b, 53) % 34);
            for (int x = x0; x < x0 + w; ++x)
                for (int y = top; y < 64; ++y) {
                    bool win = ((x - x0) % 3 == 1) && (y % 3 == 1) && (hash2(x, y) % 3 == 0);
                    layer_put(t.far_layer, 512, x, y, win ? th.far_b : th.far_a);
                }
            if (b % 5 == 0) for (int y = top - 6; y < top; ++y) layer_put(t.far_layer, 512, x0 + w / 2, y, th.far_a);
        }
        for (int b = 0; b < 40; ++b) {
            int x0 = (int)(hash2(b, 61) % 1024), w = 14 + (int)(hash2(b, 62) % 22), top = 26 + (int)(hash2(b, 63) % 28);
            u32 neon = (b & 1) ? rgb(248, 48, 200) : th.near_b;
            for (int x = x0; x < x0 + w; ++x)
                for (int y = top; y < 64; ++y) {
                    u32 c = th.near_a;
                    if (y == top || ((y - top) % 9 == 4 && (x - x0) > 2 && (x - x0) < w - 2)) c = neon;
                    layer_put(t.near_layer, 1024, x, y, c);
                }
        }
    } break;
    }
}

// ---- spline -------------------------------------------------------------------------------
static vec2 catmull(vec2 p0, vec2 p1, vec2 p2, vec2 p3, f32 t) {
    f32 t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
}

static void build_line(Track& t, const TrackDef& d) {
    Array<vec2> dense;
    Array<f32> dcum;
    dense.alloc = t.line.alloc; dcum.alloc = t.line.alloc;
    int n = d.nnodes;
    auto P = [&](int i) { i = ((i % n) + n) % n; return vec2{d.nodes[i].x, d.nodes[i].y}; };
    const int SUB = 32;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < SUB; ++k) dense.push(catmull(P(i - 1), P(i), P(i + 1), P(i + 2), (f32)k / (f32)SUB));
    f32 run = 0;
    for (u32 i = 0; i < dense.len; ++i) { dcum.push(run); run += length(dense[(i + 1) % dense.len] - dense[i]); }
    const f32 dense_total = run;
    // resample evenly, every 6 texels
    const f32 step = 6.0f;
    t.line.clear(); t.dist.clear();
    t.line.push(dense[0]); t.dist.push(0);
    f32 acc = 0, total = 0;
    vec2 prev = dense[0];
    for (u32 i = 1; i <= dense.len; ++i) {
        vec2 cur = dense[i % dense.len];
        f32 seg = length(cur - prev);
        while (acc + seg >= step) {
            f32 need = step - acc;
            vec2 p = prev + (cur - prev) * (need / seg);
            total += step;
            if (i < dense.len || length(p - dense[0]) > step * 0.5f) { t.line.push(p); t.dist.push(total); }
            seg -= need; prev = p; acc = 0;
        }
        acc += seg; prev = cur;
    }
    t.length = t.dist[t.dist.len - 1] + length(t.line[0] - t.line[t.line.len - 1]);
    // where each node falls along the line, and the half-width at every line point
    t.node_s.clear();
    for (int i = 0; i < n; ++i) t.node_s.push(dcum[(u32)(i * SUB)] * t.length / dense_total);
    t.node_s.push(t.length);
    t.hw.clear();
    int span = 0;
    for (u32 j = 0; j < t.line.len; ++j) {
        f32 s = t.dist[j];
        while (span + 1 < n && t.node_s[(u32)span + 1] <= s) ++span;
        f32 s0 = t.node_s[(u32)span], s1 = t.node_s[(u32)span + 1];
        f32 u = s1 > s0 ? (s - s0) / (s1 - s0) : 0;
        u = u * u * (3.0f - 2.0f * u);
        f32 w0 = d.nodes[span].w > 0 ? d.nodes[span].w : d.half_width;
        f32 w1 = d.nodes[(span + 1) % n].w > 0 ? d.nodes[(span + 1) % n].w : d.half_width;
        t.hw.push(w0 + (w1 - w0) * u);
    }
    dense.free(); dcum.free();
}

f32 track_node_s(const Track& t, f32 node) {
    int n = (int)t.node_s.len - 1;
    f32 base = kv::floor(node / (f32)n) * t.length;
    node -= kv::floor(node / (f32)n) * (f32)n;
    int i = (int)node;
    if (i >= n) i = n - 1;
    f32 f = node - (f32)i;
    return base + t.node_s[(u32)i] + (t.node_s[(u32)i + 1] - t.node_s[(u32)i]) * f;
}

static u32 line_index(const Track& t, f32 s, f32* u) {
    s = kv::fmod(s, t.length);
    if (s < 0) s += t.length;
    u32 lo = 0, hi = t.line.len - 1;
    while (lo < hi) {
        u32 mid = (lo + hi + 1) / 2;
        if (t.dist[mid] <= s) lo = mid; else hi = mid - 1;
    }
    f32 seg_len = (lo + 1 < t.line.len ? t.dist[lo + 1] : t.length) - t.dist[lo];
    *u = seg_len > 0 ? (s - t.dist[lo]) / seg_len : 0;
    return lo;
}

vec2 track_point(const Track& t, f32 s, vec2* tangent) {
    f32 u;
    u32 lo = line_index(t, s, &u);
    vec2 a = t.line[lo], b = t.line[(lo + 1) % t.line.len];
    if (tangent) {
        vec2 d = b - a;
        f32 l = length(d);
        *tangent = l > 0 ? d / l : vec2{1, 0};
    }
    return a + (b - a) * u;
}

f32 track_half_width(const Track& t, f32 s) {
    f32 u;
    u32 lo = line_index(t, s, &u);
    f32 a = t.hw[lo], b = t.hw[(lo + 1) % t.hw.len];
    return a + (b - a) * u;
}

f32 track_project(const Track& t, vec2 p, int* seg, int window) {
    int n = (int)t.line.len;
    int best = *seg;
    f32 best_d = 1e30f, best_s = 0;
    for (int k = -window; k <= window; ++k) {
        int i = ((*seg + k) % n + n) % n;
        vec2 a = t.line[i], b = t.line[(i + 1) % n];
        vec2 ab = b - a;
        f32 l2 = dot(ab, ab);
        f32 u = l2 > 0 ? clampf(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0;
        vec2 q = a + ab * u;
        f32 d = length_sq(p - q);
        if (d < best_d) {
            best_d = d; best = i;
            f32 seg_len = (i + 1 < n ? t.dist[i + 1] : t.length) - t.dist[i];
            best_s = t.dist[i] + seg_len * u;
        }
    }
    *seg = best;
    return best_s;
}

u8 track_surface(const Track& t, f32 x, f32 y) {
    int ix = (int)kv::floor(x), iy = (int)kv::floor(y);
    if (ix < 0 || iy < 0 || ix >= MAP || iy >= MAP) return S_WALL;
    return t.surf[(u32)(iy * MAP + ix)];
}

// ---- the map --------------------------------------------------------------------------------
struct Span { const Feat* f; f32 s0, s1; };

static bool in_span(f32 s, f32 s0, f32 s1, f32 L) {
    if (s1 <= L) return s >= s0 && s < s1;
    return s >= s0 || s < s1 - L;
}
// distance along the track from s0 forward to s
static f32 after(f32 s, f32 s0, f32 L) {
    f32 d = s - s0;
    if (d < 0) d += L;
    return d;
}

static f32 line_dist(vec2 p, vec2 a, vec2 b, f32* along) {
    vec2 d = b - a;
    f32 l = length(d);
    vec2 u = d / l;
    vec2 q = p - a;
    *along = dot(q, u);
    return dot(q, vec2{-u.y, u.x});
}

static u8 water_color(int x, int y, u32 h) { return (u8)((int)C_WATER0 + ((x / 3 + y / 5 + (int)(h & 1)) % 3)); }

static u8 terrain_color(u8 surf, int x, int y, u32 h, const Theme& th) {
    switch (surf) {
    case S_WATER: return water_color(x, y, h);
    case S_PIT:
        if (th.road == RS_SPACE) { // stars, a few of them warm, and a faint band of nebula
            if (h % 211 == 0) return C_PIT1;
            if (h % 397 == 1) return C_DEC1;
            f32 band = kv::sin((f32)(x + y) * 0.006f) + kv::sin((f32)(x - 2 * y) * 0.004f);
            if (band > 0.9f && (h % 5) < (u32)((band - 0.9f) * 4.0f)) return (h & 8) ? C_DEC0 : C_DEC2; // a thin dusting of nebula
            return C_PIT0;
        }
        if (th.road == RS_PCB) return (y % 9 == 0) ? C_PIT1 : C_PIT0; // the desk under the board
        if (th.road == RS_TABLE) return (x % 48 < 2 || y % 48 < 2) ? C_WALL1 : ((((x / 48) + (y / 48)) & 1) ? C_PIT0 : C_PIT1); // floor tiles
        return ((h & 63) == 0) ? C_PIT1 : C_PIT0;
    case S_WALL:
        if (th.road == RS_SEWER) { int row = y / 4; return (y % 4 == 0 || (x + (row & 1) * 4) % 8 == 0) ? C_WALL1 : C_WALL0; } // brick
        return (((x >> 2) ^ (y >> 2)) & 1) ? C_WALL0 : C_WALL1;
    case S_SHALLOW: return (((x + y / 2) / 3) & 1) ? C_SHAL0 : C_SHAL1;
    default:
        if (th.road == RS_PCB) {
            // the board's solder mask; its copper and parts are painted by pcb_paint_*
            return (h & 63) == 0 ? C_OFF1 : C_OFF0;
        }
        if (th.road == RS_TABLE) {
            // gingham, with the odd stray cereal loop on it
            int cx = x % 9 - 4, cy = y % 9 - 4, r2 = cx * cx + cy * cy;
            u32 cell = hash2(x / 9, y / 9);
            if (cell % 31 == 0 && r2 >= 4 && r2 <= 13) { static const u8 loops[4] = {C_CURB_A, C_CURB_B, C_DEC4, C_DEC5}; return loops[(cell >> 8) & 3]; }
            int g = ((x / 12) & 1) + ((y / 12) & 1);
            return g == 0 ? C_OFF1 : (g == 1 ? C_OFF2 : C_OFF0);
        }
        if (th.road == RS_NEON) return ((x & 15) == 0 || (y & 15) == 0) ? C_OFF2 : C_OFF0;
        return ((h & 31) == 0) ? C_OFF2 : ((((x >> 4) ^ (y >> 4)) & 1) ? C_OFF0 : C_OFF1);
    }
}

// ---- the circuit board ------------------------------------------------------------------------
// What lies flat on the board is painted into the map, so the floor's own perspective draws
// it: chips with their legs, resistors, sockets, and the copper. Traces come off the pins in
// fans, run as buses with forty-five degree bends, and end on another chip or a row of vias.
static u8 s_pcb_c[MAP * MAP], s_pcb_s[MAP * MAP]; // color (255: nothing) and surface
static bool s_pcb_on = false;

static vec2 pc_norm(vec2 v) { f32 l = length(v); return l > 0.0001f ? v * (1.0f / l) : vec2{1, 0}; }
static void pc_put(int x, int y, u8 c, u8 su) {
    if ((unsigned)x < (unsigned)MAP && (unsigned)y < (unsigned)MAP) { s_pcb_c[y * MAP + x] = c; s_pcb_s[y * MAP + x] = su; }
}
static void pc_rect(f32 x0, f32 y0, f32 x1, f32 y1, u8 c, u8 su) {
    for (int y = (int)y0; y < (int)y1; ++y) for (int x = (int)x0; x < (int)x1; ++x) pc_put(x, y, c, su);
}
static void pc_line(vec2 a, vec2 b, int th = 2, u8 c = C_OFF2) {
    f32 dx = b.x - a.x, dy = b.y - a.y;
    int n = (int)(kv::fabs(dx) > kv::fabs(dy) ? kv::fabs(dx) : kv::fabs(dy)) + 1;
    for (int i = 0; i <= n; ++i) {
        f32 u = (f32)i / (f32)n;
        int x = (int)kv::floor(a.x + dx * u), y = (int)kv::floor(a.y + dy * u);
        for (int j = 0; j < th; ++j) for (int k = 0; k < th; ++k) pc_put(x + k, y + j, c, S_OFF);
    }
}
static void pc_via(vec2 p) {
    int x = (int)p.x - 1, y = (int)p.y - 1;
    for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) pc_put(x + k, y + j, (j == 0 || j == 3) && (k == 0 || k == 3) ? C_OFF2 : C_CURB_A, S_OFF);
    pc_put(x + 1, y + 1, C_DEC3, S_OFF); pc_put(x + 2, y + 1, C_DEC3, S_OFF); pc_put(x + 1, y + 2, C_DEC3, S_OFF); pc_put(x + 2, y + 2, C_DEC3, S_OFF);
}
// n parallel traces along a polyline, mitred at the bends; `vias`: bit 0 at the start, bit 1 at the end
static void pc_bus(const vec2* pts, int np, int n, int vias = 2, f32 pitch = 4.0f) {
    for (int i = 0; i < n; ++i) {
        f32 off = ((f32)i - (f32)(n - 1) * 0.5f) * pitch;
        vec2 prev{};
        for (int k = 0; k < np; ++k) {
            vec2 d0 = k > 0 ? pc_norm(pts[k] - pts[k - 1]) : pc_norm(pts[1] - pts[0]);
            vec2 d1 = k < np - 1 ? pc_norm(pts[k + 1] - pts[k]) : d0;
            vec2 n0{-d0.y, d0.x}, n1{-d1.y, d1.x};
            vec2 q = pts[k] + (n0 + n1) * (off / (1.0f + dot(n0, n1)));
            if (k == 0 && (vias & 1)) { q = q - d1 * ((i & 1) ? 6.0f : 0.0f); }
            if (k == np - 1 && (vias & 2)) { q = q + d0 * ((i & 1) ? 6.0f : 0.0f); }
            if (k > 0) pc_line(prev, q);
            if ((k == 0 && (vias & 1)) || (k == np - 1 && (vias & 2))) pc_via(q);
            prev = q;
        }
    }
}
struct PcChip { f32 cx, cy, hl, hs; bool vert; f32 pitch; }; // half-length along its axis, half-width across
static int pc_pins(const PcChip& c) { return (int)((c.hl * 2.0f - 8.0f) / c.pitch) + 1; }
static vec2 pc_axis(const PcChip& c) { return c.vert ? vec2{0, 1} : vec2{1, 0}; }
// the pad a pin's leg comes down on: side +1 or -1 across the chip
static vec2 pc_pin(const PcChip& c, int i, int side) {
    vec2 a = pc_axis(c), o{a.y * (f32)side, a.x * (f32)side};
    f32 t = ((f32)i - (f32)(pc_pins(c) - 1) * 0.5f) * c.pitch;
    return vec2{c.cx, c.cy} + a * t + o * (c.hs + 4.0f);
}
// pins i0..i1 (inclusive) on one side leave the chip and turn together into a bus heading
// `dir` (+1/-1) along the chip; returns where the bus starts, and its heading
static vec2 pc_fan(const PcChip& c, int side, int i0, int i1, int dir, vec2* heading, f32 base = 6.0f) {
    vec2 a = pc_axis(c) * (f32)dir, o{pc_axis(c).y * (f32)side, pc_axis(c).x * (f32)side};
    int n = i1 - i0 + 1;
    vec2 last = pc_pin(c, dir > 0 ? i1 : i0, side);
    f32 t_end = dot(last, a) + 10.0f;
    for (int i = i0; i <= i1; ++i) {
        int rank = dir > 0 ? i1 - i : i - i0; // the pin nearest the way out turns first, so none cross
        vec2 p = pc_pin(c, i, side);
        f32 out = base + (f32)rank * 4.0f;
        vec2 q1 = p + o * (out - 3.0f), q2 = p + o * out + a * 3.0f;
        vec2 q3 = q2 + a * (t_end - dot(q2, a));
        pc_line(p, q1); pc_line(q1, q2); pc_line(q2, q3);
    }
    *heading = a;
    vec2 mid = last + o * (base + (f32)(n - 1) * 2.0f);
    return mid + a * (t_end - dot(mid, a));
}
// a fan and the bus that carries on from it through `more` points
static void pc_fan_bus(const PcChip& c, int side, int i0, int i1, int dir, const vec2* more, int nmore, int vias = 2) {
    vec2 pts[10], h;
    pts[0] = pc_fan(c, side, i0, i1, dir, &h);
    for (int i = 0; i < nmore && i < 9; ++i) pts[i + 1] = more[i];
    pc_bus(pts, nmore + 1, i1 - i0 + 1, vias);
}
// short stubs off a side's pins to staggered vias
static void pc_stubs(const PcChip& c, int side, int i0 = 0, int i1 = -1) {
    vec2 o{pc_axis(c).y * (f32)side, pc_axis(c).x * (f32)side};
    if (i1 < 0) i1 = pc_pins(c) - 1;
    for (int i = i0; i <= i1; ++i) { vec2 p = pc_pin(c, i, side), q = p + o * ((i & 1) ? 11.0f : 5.0f); pc_line(p, q); pc_via(q); }
}
static void pc_chip(const PcChip& c) {
    f32 hx = c.vert ? c.hs : c.hl, hy = c.vert ? c.hl : c.hs;
    pc_rect(c.cx - hx + 3, c.cy - hy + 3, c.cx + hx + 7, c.cy + hy + 6, C_DEC3, S_OFF); // its shadow on the board
    for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i < pc_pins(c); ++i) {
            vec2 p = pc_pin(c, i, side);
            f32 lx = c.vert ? 5.0f : 2.0f, ly = c.vert ? 2.0f : 5.0f;
            pc_rect(p.x - lx, p.y - ly, p.x + lx, p.y + ly, C_CURB_A, S_WALL);
        }
    pc_rect(c.cx - hx, c.cy - hy, c.cx + hx, c.cy + hy, C_WALL0, S_WALL);
    // a lighter rim along two edges, the notch at one end, the dot by pin one, a line of print
    pc_rect(c.cx - hx, c.cy - hy, c.cx + hx, c.cy - hy + 2, C_WALL1, S_WALL);
    pc_rect(c.cx - hx, c.cy - hy, c.cx - hx + 2, c.cy + hy, C_WALL1, S_WALL);
    vec2 a = pc_axis(c), e = vec2{c.cx, c.cy} - a * c.hl;
    for (int y = -5; y <= 5; ++y) for (int x = -5; x <= 5; ++x)
        if (x * x + y * y <= 22 && kv::fabs((f32)x) <= hx && kv::fabs((f32)y) <= hy) { vec2 q = e + vec2{(f32)x, (f32)y}; if (dot(q - e, a) >= 0) pc_put((int)q.x, (int)q.y, C_WALL1, S_WALL); }
    vec2 dot1 = e + a * 9.0f + vec2{a.y, a.x} * (c.hs - 6.0f);
    pc_rect(dot1.x - 1, dot1.y - 1, dot1.x + 2, dot1.y + 2, C_DEC2, S_WALL);
    for (f32 t = -c.hl * 0.5f; t < c.hl * 0.5f; t += 5.0f) {
        if (((int)(t + 1000.0f) / 5) % 4 == 3) continue;
        vec2 q = vec2{c.cx, c.cy} + a * t;
        pc_rect(q.x - (c.vert ? 3.0f : 1.0f), q.y - (c.vert ? 1.0f : 3.0f), q.x + (c.vert ? 3.0f : 2.0f), q.y + (c.vert ? 2.0f : 3.0f), C_WALL1, S_WALL);
    }
}
// an axial resistor standing north-south: pads, leads, a body with its bands
static void pc_resistor(f32 x, f32 y) {
    pc_line(vec2{x, y - 19}, vec2{x, y + 19}, 2, C_CURB_A);
    pc_via(vec2{x, y - 20}); pc_via(vec2{x, y + 20});
    pc_rect(x - 3, y - 9, x + 7, y + 13, C_DEC3, S_OFF);
    pc_rect(x - 4, y - 11, x + 5, y + 11, C_DEC4, S_WALL);
    pc_rect(x - 4, y - 8, x + 5, y - 6, C_DEC5, S_WALL); pc_rect(x - 4, y - 3, x + 5, y - 1, C_WALL0, S_WALL);
    pc_rect(x - 4, y + 2, x + 5, y + 4, C_DEC5, S_WALL); pc_rect(x - 4, y + 7, x + 5, y + 9, C_DEC1, S_WALL);
}
// an expansion slot lying east-west: a long socket with two rows of contacts
static void pc_slot(f32 x0, f32 x1, f32 cy) {
    pc_rect(x0 + 3, cy - 9, x1 + 6, cy + 16, C_DEC3, S_OFF);
    pc_rect(x0, cy - 12, x1, cy + 12, C_WALL0, S_WALL);
    pc_rect(x0, cy - 12, x1, cy - 10, C_WALL1, S_WALL);
    pc_rect(x0 + 8, cy - 2, x1 - 8, cy + 2, C_DEC3, S_WALL);
    for (f32 x = x0 + 12; x < x1 - 10; x += 12) { pc_rect(x, cy - 7, x + 5, cy - 3, C_DEC1, S_WALL); pc_rect(x, cy + 3, x + 5, cy + 7, C_DEC1, S_WALL); }
}

static void pcb_clear() { for (int i = 0; i < MAP * MAP; ++i) s_pcb_c[i] = 255; s_pcb_on = true; }

// CIRCUIT BREAKER's board: a home computer. The RAM bank stands either side of the alley,
// the processor in the north-west with the power supply above it, glue logic and ROMs in
// the middle, expansion slots and the edge connector along the south.
static void pcb_paint_board() {
    pcb_clear();
    const PcChip cpu{345, 330, 120, 34, false, 10}, io{400, 690, 100, 30, false, 10};
    const PcChip rom1{640, 560, 70, 22, false, 10}, rom2{640, 640, 70, 22, false, 10};
    PcChip ram[9], logic[4];
    for (int i = 0; i < 9; ++i) ram[i] = PcChip{i / 3 == 0 ? 790.0f : (i / 3 == 1 ? 856.0f : 964.0f), 360.0f + 160.0f * (f32)(i % 3), 62, 22, true, 8};
    for (int i = 0; i < 4; ++i) logic[i] = PcChip{i / 2 ? 712.0f : 650.0f, 270.0f + 90.0f * (f32)(i % 2), 34, 16, true, 8};

    // --- copper first; everything else sits on top of it
    // the address bus: processor, east along the top, down past the RAM, back west to the I/O chip
    { vec2 m[] = {{500, 272}, {562, 210}, {716, 210}, {748, 242}, {748, 713}, {716, 745}, {512, 745}}; pc_fan_bus(cpu, -1, 15, 22, 1, m, 7, 0); }
    { vec2 h; pc_fan(io, 1, 11, 18, 1, &h); }
    // the data bus: processor, under the road, and along the aisle of the RAM bank
    { vec2 m[] = {{596, 384}, {652, 440}, {938, 440}}; pc_fan_bus(cpu, 1, 17, 22, 1, m, 3); }
    // processor to the power supply, and to the pull-up resistors
    { vec2 m[] = {{200, 276}, {170, 246}, {170, 186}}; pc_fan_bus(cpu, -1, 0, 5, -1, m, 3); }
    { vec2 m[] = {{207, 384}, {187, 404}, {187, 552}}; pc_fan_bus(cpu, 1, 0, 5, -1, m, 3); }
    // ROM to the I/O chip
    { vec2 m[] = {{545, 602}, {507, 640}, {502, 640}}; pc_fan_bus(rom1, 1, 0, 5, -1, m, 3, 0); }
    { vec2 h; pc_fan(io, -1, 13, 18, 1, &h); }
    { vec2 m[] = {{730, 682}, {740, 692}, {740, 700}}; pc_fan_bus(rom2, 1, 7, 12, 1, m, 3); }
    // RAM: column to column across the gap, taps off the address bus, stubs elsewhere
    for (int i = 0; i < 3; ++i) {
        for (int p = 0; p < pc_pins(ram[i]); ++p) pc_line(pc_pin(ram[i], p, 1), pc_pin(ram[i + 3], p, -1));
        pc_stubs(ram[i], -1);
    }
    for (int i = 0; i < 2; ++i) for (int p = 0; p < pc_pins(logic[i]); ++p) pc_line(pc_pin(logic[i], p, 1), pc_pin(logic[i + 2], p, -1));
    for (int i = 0; i < 2; ++i) { pc_stubs(logic[i], -1); pc_stubs(logic[i + 2], 1); }
    pc_stubs(cpu, -1, 6, 14); pc_stubs(cpu, 1, 6, 16);
    pc_stubs(io, -1, 0, 12); pc_stubs(io, 1, 0, 10);
    pc_stubs(rom1, -1); pc_stubs(rom1, 1, 6, 12); pc_stubs(rom2, -1); pc_stubs(rom2, 1, 0, 6);
    // the power rail: a broad trace past the capacitors, down to the processor
    pc_line(vec2{196, 168}, vec2{500, 168}, 6); pc_line(vec2{500, 168}, vec2{500, 246}, 6); pc_line(vec2{500, 246}, vec2{474, 272}, 6);
    for (int i = 0; i < 4; ++i) pc_line(vec2{230.0f + 70.0f * (f32)i, 140}, vec2{230.0f + 70.0f * (f32)i, 168}, 4);
    pc_line(vec2{60, 60}, vec2{60, 930}, 4); pc_line(vec2{60, 60}, vec2{200, 60}, 4); // the ground ring by the board's edge
    for (int i = 0; i < 4; ++i) { f32 x = 200.0f + 60.0f * (f32)i; pc_line(vec2{x, 232}, vec2{x, 250}); pc_via(vec2{x, 252}); pc_line(vec2{x, 208}, vec2{x, 168}); }
    // the edge connector's fingers, and their runs north to the slots
    for (f32 x = 200; x < 830; x += 12) {
        bool to_slot = x > 296 && x < 664;
        pc_line(vec2{x + 2, 952}, vec2{x + 2, to_slot ? 826.0f : (((int)x / 12) & 1 ? 938.0f : 944.0f)});
        if (!to_slot) pc_via(vec2{x + 2, ((int)x / 12) & 1 ? 938.0f : 944.0f});
        if (to_slot) pc_line(vec2{x + 2, 782}, vec2{x + 2, 804});
        pc_rect(x, 952, x + 7, 992, C_DEC1, S_OFF);
    }
    { vec2 m[] = {{100, 176}, {80, 196}, {80, 456}}; pc_bus(m, 3, 4, 3); }
    { vec2 m[] = {{520, 560}, {560, 520}, {560, 512}}; pc_bus(m, 3, 3, 3); }
    const PcChip sw{230, 740, 40, 16, true, 8}, glue{450, 560, 34, 16, false, 8};
    pc_stubs(sw, -1); pc_stubs(sw, 1); pc_stubs(glue, -1); pc_stubs(glue, 1);
    // mounting holes in the corners
    const vec2 holes[3] = {{958, 68}, {958, 962}, {100, 962}};
    for (int i = 0; i < 3; ++i)
        for (int y = -13; y <= 13; ++y) for (int x = -13; x <= 13; ++x) {
            int r2 = x * x + y * y;
            if (r2 <= 169) pc_put((int)holes[i].x + x, (int)holes[i].y + y, r2 <= 49 ? C_PIT0 : (r2 >= 144 ? C_OFF2 : C_DEC1), S_OFF);
        }
    // --- parts
    pc_chip(sw); pc_chip(glue);
    pc_chip(cpu); pc_chip(io); pc_chip(rom1); pc_chip(rom2);
    for (int i = 0; i < 9; ++i) pc_chip(ram[i]);
    for (int i = 0; i < 4; ++i) pc_chip(logic[i]);
    for (int i = 0; i < 8; ++i) pc_resistor(196.0f + 22.0f * (f32)i, 596);
    pc_slot(290, 670, 770); pc_slot(290, 670, 815);
    // the crystal: a tin can on its side
    pc_rect(737, 588, 757, 628, C_DEC3, S_OFF);
    for (int y = -18; y <= 18; ++y) for (int x = -8; x <= 8; ++x) {
        f32 ey = kv::fabs((f32)y) > 10.0f ? (kv::fabs((f32)y) - 10.0f) / 8.0f : 0.0f;
        if ((f32)(x * x) / 64.0f + ey * ey <= 1.0f) pc_put(744 + x, 604 + y, x < -4 || y < -14 ? C_DEC2 : C_CURB_A, S_WALL);
    }
}

// a board with no plan (the random tracks): buses on a loose grid, bent at forty-five degrees
static void pcb_paint_generic(u32 seed) {
    pcb_clear();
    for (int i = 0; i < 26; ++i) {
        u32 h = hash2((i32)seed, i * 7 + 1);
        f32 x = 60.0f + (f32)(h % 15) * 60.0f, y = 60.0f + (f32)((h >> 8) % 15) * 60.0f;
        int n = 3 + (int)((h >> 16) % 6);
        f32 l0 = 120.0f + (f32)((h >> 20) % 5) * 60.0f, l1 = 120.0f + (f32)((h >> 24) % 5) * 60.0f, j = 40.0f + (f32)((h >> 28) % 3) * 20.0f;
        f32 sx = (h & 1) ? 1.0f : -1.0f, sy = (h & 2) ? 1.0f : -1.0f;
        vec2 m[4];
        if (h & 4) { m[0] = vec2{x, y}; m[1] = vec2{x + sx * l0, y}; m[2] = vec2{x + sx * (l0 + j), y + sy * j}; m[3] = vec2{x + sx * (l0 + j), y + sy * (j + l1)}; }
        else { m[0] = vec2{x, y}; m[1] = vec2{x, y + sy * l0}; m[2] = vec2{x + sx * j, y + sy * (l0 + j)}; m[3] = vec2{x + sx * (j + l1), y + sy * (l0 + j)}; }
        pc_bus(m, 4, n, 3);
    }
}

void track_build(Track& t, const TrackDef& def, Allocator* alloc) {
    t.def = &def;
    t.theme = &k_themes[def.theme];
    const Theme& th = *t.theme;
    s_pcb_on = false;
    if (th.road == RS_PCB) { if (def.objs == pcb_objs) pcb_paint_board(); else pcb_paint_generic(def.seed + 77u); }
    t.color.alloc = t.surf.alloc = alloc;
    t.line.alloc = alloc; t.dist.alloc = alloc; t.hw.alloc = alloc; t.node_s.alloc = alloc;
    t.props.alloc = alloc; t.boxes.alloc = alloc; t.bolts.alloc = alloc;
    t.far_layer.alloc = alloc; t.near_layer.alloc = alloc;
    t.sp.bodies.alloc = alloc; t.statics.alloc = alloc;
    t.props.clear(); t.boxes.clear(); t.bolts.clear(); t.statics.clear();
    t.ngates = 0;
    build_line(t, def);
    const f32 L = t.length;
    const int n = (int)t.line.len;

    f32 max_hw = 0;
    for (f32 w : t.hw) max_hw = mxf(max_hw, w);
    const f32 def_sh = def.shoulder > 0 ? def.shoulder : 36.0f;
    const f32 wall_w = 5.0f, edge_w = 70.0f;
    f32 max_sh = def_sh;
    Span spans[80];
    int nspans = 0;
    for (int i = 0; i < def.nfeats && nspans < 80; ++i) {
        const Feat& f = def.feats[i];
        f32 s0 = track_node_s(t, f.n0);
        f32 s1 = f.n1 > f.n0 ? track_node_s(t, f.n1) : s0;
        s0 = kv::fmod(s0, L);
        if (s1 < s0) s1 += L;
        while (s1 - s0 > L) s1 -= L;
        spans[nspans++] = Span{&f, s0, s1};
        if (f.kind == F_SHOULDER) max_sh = mxf(max_sh, f.a);
        if (f.kind == F_GAP) max_sh = mxf(max_sh, f.a);
        if (f.kind == F_GATE && t.ngates < 8) { t.gate_s[t.ngates] = s0; t.gate_period[t.ngates] = f.a; ++t.ngates; }
    }
    const f32 reach = max_hw + 3.0f + max_sh + edge_w + 2.0f;
    Rng rng = Rng::seeded(1234 + (u64)def.theme * 977 + (u64)def.seed + (u64)def.nnodes * 31);

    // the set piece's path: given by the track, or a line through the middle
    SetPiece& sp = t.sp;
    sp.kind = th.setpiece;
    sp.center = vec2{512, 512};
    bool given = false;
    for (f32 v : def.sp) if (v != 0) given = true;
    bool flyer = sp.kind == SPK_BLIMP || sp.kind == SPK_DRAGON || sp.kind == SPK_SHIP;
    if (given && flyer) { sp.center = vec2{def.sp[0], def.sp[1]}; sp.radius = def.sp[2]; }
    else if (given) {
        sp.a = vec2{def.sp[0], def.sp[1]}; sp.b = vec2{def.sp[2], def.sp[3]};
        sp.a2 = vec2{def.sp[4], def.sp[5]}; sp.b2 = vec2{def.sp[6], def.sp[7]};
    } else {
        f32 ang = (f32)(hash2((i32)def.seed + def.theme * 31, def.nnodes) % 360) * 0.0174533f;
        vec2 u{kv::cos(ang), kv::sin(ang)};
        sp.a = sp.center - u * 760.0f; sp.b = sp.center + u * 760.0f;
        vec2 u2{-u.y, u.x};
        sp.a2 = sp.center - u2 * 760.0f; sp.b2 = sp.center + u2 * 760.0f;
        sp.radius = 300.0f;
    }

    // nearest point of the centerline for every texel within reach: distance, where along,
    // which side
    Array<f32> d2;   d2.alloc = alloc;
    Array<f32> spar; spar.alloc = alloc;
    Array<f32> hwt;  hwt.alloc = alloc;
    Array<i8> side;  side.alloc = alloc;
    d2.resize_fill(MAP * MAP, 1e30f);
    spar.resize_fill(MAP * MAP, 0.0f);
    hwt.resize_fill(MAP * MAP, 0.0f);
    side.resize_fill(MAP * MAP, 0);
    for (int i = 0; i < n; ++i) {
        vec2 a = t.line[i], b = t.line[(i + 1) % n];
        vec2 ab = b - a;
        f32 l2 = dot(ab, ab);
        f32 seg_len = (i + 1 < n ? t.dist[i + 1] : L) - t.dist[i];
        f32 w0 = t.hw[(u32)i], w1 = t.hw[(u32)((i + 1) % n)];
        int x0 = (int)(mnf(a.x, b.x) - reach), x1 = (int)(mxf(a.x, b.x) + reach);
        int y0 = (int)(mnf(a.y, b.y) - reach), y1 = (int)(mxf(a.y, b.y) + reach);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= MAP) x1 = MAP - 1;
        if (y1 >= MAP) y1 = MAP - 1;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                vec2 p{(f32)x + 0.5f, (f32)y + 0.5f};
                f32 uu = l2 > 0 ? clampf(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0;
                vec2 q = a + ab * uu;
                f32 dd = length_sq(p - q);
                u32 k = (u32)(y * MAP + x);
                if (dd < d2[k]) {
                    d2[k] = dd; spar[k] = t.dist[i] + seg_len * uu; hwt[k] = w0 + (w1 - w0) * uu;
                    side[k] = (ab.x * (p.y - a.y) - ab.y * (p.x - a.x)) >= 0 ? 1 : -1;
                }
            }
    }

    t.color.resize_fill(MAP * MAP, 0);
    t.surf.resize_fill(MAP * MAP, S_OUT);
    const bool sea = th.setpiece == SPK_SHIP;
    for (int y = 0; y < MAP; ++y)
        for (int x = 0; x < MAP; ++x) {
            u32 k = (u32)(y * MAP + x);
            u32 h = hash2(x, y);
            bool near_track = d2[k] < 1e29f;
            f32 d = near_track ? kv::sqrt(d2[k]) : 1e9f;
            f32 s = spar[k];
            f32 hw = near_track ? hwt[k] : 30.0f;
            f32 lf = near_track ? (f32)side[k] * d / hw : 0;
            u8 c = 0, su = S_OUT;
            bool is_terrain = true; // whether stamps may repaint it

            // this side's shoulder, and what lies beyond it
            f32 sh = def_sh;
            u8 edge = def.edge;
            const Feat* gap = nullptr;
            if (near_track)
                for (int i = 0; i < nspans; ++i) {
                    const Feat& f = *spans[i].f;
                    if (f.kind == F_SHOULDER && in_span(s, spans[i].s0, spans[i].s1, L) && ((side[k] < 0 && f.l0 < 0) || (side[k] > 0 && f.l1 > 0))) { sh = f.a; edge = f.b; }
                    if (f.kind == F_GAP && in_span(s, spans[i].s0, spans[i].s1, L) && d < hw + f.a) gap = &f;
                }

            if (gap) {
                su = gap->b; c = terrain_color(su, x, y, h, th); is_terrain = false;
            } else if (d < hw) {
                su = S_ROAD; is_terrain = false;
                u32 tile = hash2(x >> 3, y >> 3) & 3;
                switch (th.road) {
                case RS_COBBLE: {
                    int bx = (x + ((y >> 2) & 1) * 2) & 3, by = y & 3;
                    c = (bx == 0 || by == 0) ? C_ROAD1 : ((h & 7) == 0 ? C_ROAD2 : C_ROAD0);
                } break;
                case RS_PLANKS: {
                    int board = (int)(s / 4.0f);
                    c = ((int)(s) % 4 == 0) ? C_ROAD2 : ((board & 1) ? C_ROAD0 : C_ROAD1);
                    if ((h & 31) == 0) c = C_ROAD2;
                } break;
                case RS_DIRT: c = (h & 7) == 0 ? C_ROAD2 : ((h & 15) == 1 ? C_ROAD1 : (tile == 0 ? C_ROAD1 : C_ROAD0)); break;
                case RS_NEON: c = (((x >> 3) ^ (y >> 3)) & 1) ? C_ROAD0 : C_ROAD1; if ((x & 7) == 0 || (y & 7) == 0) c = C_ROAD2; break;
                case RS_SEWER: { // wet concrete with a drain channel down the middle
                    c = (h & 15) == 0 ? C_ROAD2 : (tile == 0 ? C_ROAD1 : C_ROAD0);
                    if (kv::fabs(lf) < 0.1f) c = water_color(x, y, h);
                } break;
                case RS_PCB: c = (h & 31) == 0 ? C_ROAD2 : ((((int)(s / 3.0f)) & 7) == 0 ? C_ROAD1 : C_ROAD0); break; // tinned copper
                case RS_SPACE: c = (((int)(s / 16.0f) + (int)((lf + 4.0f) * 2.0f)) & 1) ? C_ROAD0 : C_ROAD1; if (((int)s) % 16 == 0 || kv::fabs(kv::fabs(lf) - 0.5f) < 0.02f) c = C_ROAD2; break;
                case RS_HAUNT: c = (h & 7) == 0 ? C_ROAD2 : ((h & 15) == 1 ? C_ROAD1 : (tile == 0 ? C_ROAD1 : C_ROAD0)); break;
                case RS_TABLE: { // wood grain running the length of the table
                    u32 g = hash2(x >> 5, y / 3);
                    c = (g & 7) == 0 ? C_ROAD1 : ((g & 7) == 1 ? C_ROAD2 : C_ROAD0);
                } break;
                default: c = (h & 15) == 0 ? C_ROAD2 : ((h & 31) == 1 ? C_ROAD1 : (tile == 0 ? C_ROAD1 : C_ROAD0)); break;
                }
                if (d > hw - 4 && d < hw - 3) c = C_LINE;
                if (s < 8.0f || s > L - 0.5f) c = (((x >> 2) ^ (y >> 2)) & 1) ? C_START_A : C_START_B;
                for (int i = 0; i < nspans; ++i) {
                    const Feat& f = *spans[i].f;
                    f32 rel = after(s, spans[i].s0, L);
                    switch (f.kind) {
                    case F_SURFACE: {
                        f32 len = spans[i].s1 - spans[i].s0;
                        f32 wob = f.a * (0.10f * kv::sin(s * 0.21f + (f32)i) + 0.06f * kv::sin(s * 0.53f));
                        // rounded ends: the patch narrows over its first and last stretch
                        f32 endk = f.a > 0 ? clampf(mnf(rel, len - rel) / 14.0f, 0.0f, 1.0f) : 1.0f;
                        f32 mid = (f.l0 + f.l1) * 0.5f, half = (f.l1 - f.l0) * 0.5f * (0.35f + 0.65f * endk) + wob;
                        if (rel < len && kv::fabs(lf - mid) < half) {
                            su = f.b;
                            bool alt = (h & 3) == 0;
                            c = f.b == S_ICE ? (alt ? C_ICE1 : C_ICE0) : f.b == S_SHALLOW ? ((((x + y / 2) / 3) & 1) ? C_SHAL0 : C_SHAL1) : (alt ? C_MUD1 : C_MUD0);
                        }
                    } break;
                    case F_BOOST:
                        if (rel < 30.0f && kv::fabs(lf * hw - f.l0 * hw) < 11.0f) {
                            su = S_BOOST;
                            c = kv::fmod(rel + kv::fabs(lf * hw - f.l0 * hw) * 0.7f, 10.0f) < 5.0f ? C_BOOST0 : C_BOOST1;
                        }
                        break;
                    case F_RAMP:
                        if (rel < 14.0f && lf >= f.l0 && lf <= f.l1) {
                            su = S_RAMP;
                            c = (rel > 12.0f || ((int)(rel + kv::fabs(lf) * hw * 0.5f) / 3) & 1) ? C_RAMP0 : C_RAMP1;
                        }
                        break;
                    case F_PAINT:
                        if (rel >= spans[i].s1 - spans[i].s0 || su != S_ROAD) break;
                        if (f.b == 1 && c != C_LINE) c = (x % 10 < 2 || y % 10 < 2) ? C_DEC3 : C_DEC2;       // a waffle
                        else if (f.b == 2 && kv::fabs(lf) < 0.72f) c = water_color(x, y, h);                 // a channel of running water
                        else if (f.b == 3) {                                                                // floorboards and a carpet runner
                            f32 al = kv::fabs(lf);
                            c = al < 0.5f ? C_DEC2 : (al < 0.57f ? C_DEC3 : ((((int)(s / 5.0f)) & 1) ? C_DEC0 : C_DEC1));
                            if (al >= 0.57f && ((int)s) % 5 == 0) c = C_DEC1;
                        }
                        break;
                    case F_ISLAND: {
                        f32 len = spans[i].s1 - spans[i].s0;
                        f32 endk = clampf(mnf(rel, len - rel) / 10.0f, 0.0f, 1.0f);
                        f32 mid = (f.l0 + f.l1) * 0.5f, half = (f.l1 - f.l0) * 0.5f * endk;
                        if (rel < len && kv::fabs(lf - mid) < half) { su = S_WALL; c = (((int)(s / 4.0f)) & 1) ? C_CURB_A : C_CURB_B; }
                    } break;
                    default: break;
                    }
                }
            } else if (d < hw + 3.0f) {
                su = S_ROAD; is_terrain = false;
                c = (((int)(s / 6.0f)) & 1) ? C_CURB_A : C_CURB_B;
            } else if (d < hw + 3.0f + sh) {
                su = S_OFF;
                c = terrain_color(S_OFF, x, y, h, th);
            } else if (edge == 0 && d < hw + 3.0f + sh + wall_w) {
                su = S_WALL;
                c = terrain_color(S_WALL, x, y, h, th);
            } else if (edge != 0 && d < hw + 3.0f + sh + edge_w) {
                su = edge;
                c = terrain_color(edge, x, y, h, th);
                if (d < hw + 3.0f + sh + 2.0f) c = C_WALL1; // a lip at the very edge
            } else {
                su = S_OUT;
                c = (((x >> 3) ^ (y >> 3)) & 1) ? C_OUT0 : C_OUT1;
                if (th.road == RS_NEON) c = ((h & 127) == 0) ? C_PIT1 : C_OUT0;
                else if (sea) c = water_color(x, y, h);
                else if ((h & 63) == 0) c = C_OFF2;
            }

            if (is_terrain && th.road == RS_SPACE) { su = S_PIT; c = terrain_color(S_PIT, x, y, h, th); } // nothing but the road
            if (is_terrain && th.road == RS_PCB) {
                const int e0 = 30, e1 = 994;
                if (x < e0 || y < e0 || x >= e1 || y >= e1) { su = S_PIT; c = terrain_color(S_PIT, x, y, h, th); }
                else {
                    su = S_OFF; c = terrain_color(S_OFF, x, y, h, th);
                    if (s_pcb_on && s_pcb_c[k] != 255) { c = s_pcb_c[k]; su = s_pcb_s[k]; }
                }
            }
            if (is_terrain && th.road == RS_TABLE) {
                const int e0 = 52, e1 = 972;
                if (x < e0 || y < e0 || x >= e1 || y >= e1) { su = S_PIT; c = terrain_color(S_PIT, x, y, h, th); }
                else if (x < e0 + 5 || y < e0 + 5 || x >= e1 - 5 || y >= e1 - 5) { su = S_OFF; c = C_ROAD1; } // the table's rim
                else { su = S_OFF; c = terrain_color(S_OFF, x, y, h, th); }
            }
            if (is_terrain)
                for (int i = 0; i < def.nstamps; ++i) {
                    const Stamp& st = def.stamps[i];
                    if (near_track && d - hw - 3.0f < st.keep) continue;
                    vec2 p{(f32)x + 0.5f, (f32)y + 0.5f};
                    f32 wob = (st.color || th.road == RS_TABLE || st.surf == S_WALL || st.surf == S_ROAD) ? 0.0f : 5.0f * kv::sin((f32)x * 0.045f + (f32)i) + 4.0f * kv::sin((f32)y * 0.06f + 1.3f); // made things have clean edges
                    bool hit = false;
                    if (st.kind == ST_DISC) hit = length(p - vec2{st.x0, st.y0}) < st.r0 + (st.surf == S_WALL ? 0.0f : wob);
                    else if (st.kind == ST_RING) { f32 r = length(p - vec2{st.x0, st.y0}); hit = r >= st.r0 && r < st.r1; }
                    else if (st.kind == ST_BOX) hit = p.x >= st.x0 && p.x < st.x1 && p.y >= st.y0 && p.y < st.y1;
                    else {
                        vec2 a{st.x0, st.y0}, b{st.x1, st.y1}, ab = b - a;
                        f32 u = clampf(dot(p - a, ab) / dot(ab, ab), 0.0f, 1.0f);
                        hit = length(p - (a + ab * u)) < st.r0 + wob * 0.6f;
                    }
                    if (hit) {
                        su = st.surf; c = terrain_color(st.surf, x, y, h, th);
                        if (st.color >= 1 && st.color <= 6) c = (u8)((int)C_DEC0 + st.color - 1);
                        else if (st.color == 7) c = (x % 10 < 2 || y % 10 < 2) ? C_DEC3 : C_DEC2;
                        else if (st.color == 8) c = ((x + y) % 14 < 2) ? C_DEC5 : C_DEC4;
                    }
                }
            if (is_terrain)
                for (int i = 0; i < def.nobjs; ++i) { // what stands on the map is solid over its footprint
                    const Obj& o = def.objs[i];
                    f32 ca = kv::cos(o.heading), sa = kv::sin(o.heading);
                    f32 px = (f32)x + 0.5f - o.x, py = (f32)y + 0.5f - o.y;
                    if (kv::fabs(px * ca + py * sa) < o.hx && kv::fabs(-px * sa + py * ca) < o.hy) {
                        if (th.road == RS_PCB) { // on the board, a round part has a round foot, in shadow
                            if (o.hx == o.hy && px * px + py * py > o.hx * o.hx) continue;
                            su = S_WALL; c = C_DEC3;
                        } else { su = S_WALL; c = C_WALL1; }
                    }
                }
            t.color[k] = c;
            t.surf[k] = su;
        }
    d2.free(); hwt.free(); side.free();

    // the set piece's marks on the map: rails and hover lanes
    if (sp.kind == SPK_TRAIN || sp.kind == SPK_TRAFFIC) {
        for (int pass = 0; pass < (sp.kind == SPK_TRAFFIC ? 2 : 1); ++pass) {
            vec2 a = pass ? sp.a2 : sp.a, b = pass ? sp.b2 : sp.b;
            if (length_sq(b - a) < 1.0f) continue;
            for (int y = 0; y < MAP; ++y)
                for (int x = 0; x < MAP; ++x) {
                    f32 along;
                    f32 off = line_dist(vec2{(f32)x + 0.5f, (f32)y + 0.5f}, a, b, &along);
                    f32 ao = kv::fabs(off);
                    if (ao > 16.0f) continue;
                    u32 k = (u32)(y * MAP + x);
                    u8 su = t.surf[k];
                    bool roadish = su == S_ROAD || su == S_BOOST;
                    if (sp.kind == SPK_TRAIN) {
                        if (ao > 6.0f) continue;
                        if (su == S_WALL) t.surf[k] = S_OFF; // the rails cut through the barrier
                        if (su == S_PIT || su == S_WATER) continue;
                        if (ao > 3.5f && ao < 4.5f) t.color[k] = C_RAIL;
                        else if (kv::fmod(along + 4000.0f, 6.0f) < 2.0f) t.color[k] = C_TIE;
                    } else {
                        if (ao > 14.0f || su == S_WALL) continue;
                        if (roadish) { if (kv::fmod(along + 4000.0f, 6.0f) < 3.0f) t.color[k] = C_LANE; }
                        else t.color[k] = (ao < 1.0f && kv::fmod(along + 4000.0f, 10.0f) < 5.0f) ? C_LANE : C_ROAD0;
                    }
                }
        }
    }
    spar.free();

    for (int my = 0; my < MINI; ++my)
        for (int mx = 0; mx < MINI; ++mx) {
            u8 v = 0;
            for (int sy = 0; sy < 4 && !v; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    u8 su = t.surf[(u32)((my * 16 + sy * 4 + 2) * MAP + mx * 16 + sx * 4 + 2)];
                    if (su == S_ROAD || su == S_BOOST || su == S_MUD || su == S_ICE || su == S_RAMP || su == S_SHALLOW) { v = 1; break; }
                }
            t.mini[my * MINI + mx] = v;
        }

    // ---- things on and beside the track ----
    auto at = [&](f32 s, f32 lat, vec2* tan_out) {
        vec2 tan;
        vec2 c = track_point(t, s, &tan);
        if (tan_out) *tan_out = tan;
        return c + vec2{-tan.y, tan.x} * (lat * track_half_width(t, s));
    };
    for (int i = 0; i < nspans; ++i) {
        const Feat& f = *spans[i].f;
        switch (f.kind) {
        case F_BOXES: {
            int count = (int)f.a < 1 ? 1 : (int)f.a;
            for (int k = 0; k < count; ++k) {
                f32 lat = count == 1 ? (f.l0 + f.l1) * 0.5f : f.l0 + (f.l1 - f.l0) * (f32)k / (f32)(count - 1);
                t.boxes.push(ItemBox{at(spans[i].s0, lat, nullptr), 0, 0, 0, 0, false});
            }
        } break;
        case F_BOLTS: {
            f32 len = spans[i].s1 - spans[i].s0;
            int count = (int)(len / 13.0f) + 1;
            for (int k = 0; k < count; ++k) {
                f32 u = count > 1 ? (f32)k / (f32)(count - 1) : 0;
                t.bolts.push(Bolt{at(spans[i].s0 + len * u, f.l0 + (f.l1 - f.l0) * u, nullptr), 0});
            }
        } break;
        case F_POSTS: {
            f32 len = spans[i].s1 - spans[i].s0;
            int k = 0;
            for (f32 q = 0; q < len; q += f.a, ++k)
                t.props.push(Prop{at(spans[i].s0 + q, (k & 1) ? f.l1 : f.l0, nullptr), f.b, true});
        } break;
        default: break;
        }
    }

    // scenery beyond the edge and the odd obstacle in the shoulder
    for (int i = 0; i < def.nobjs; ++i) {
        SetBody o{};
        o.img = def.objs[i].img;
        o.pos = vec2{def.objs[i].x, def.objs[i].y};
        o.heading = def.objs[i].heading;
        o.z = def.objs[i].z;
        o.scale = def.objs[i].scale;
        t.statics.push(o);
    }
    if (th.road == RS_SPACE) // with no walls, a line of beacons marks each edge
        for (f32 s = 20.0f; s < L; s += 46.0f)
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 tan;
                vec2 c = track_point(t, s, &tan);
                vec2 p = c + vec2{-tan.y, tan.x} * ((f32)sd * (track_half_width(t, s) + 7.0f));
                if (track_surface(t, p.x, p.y) == S_PIT) t.props.push(Prop{p, P_BEACON, false});
            }
    if (th.road == RS_SPACE) { t.props.push(Prop{vec2{560, 660}, P_ASTRONAUT, false}); t.props.push(Prop{vec2{820, 520}, P_ASTRONAUT, false}); }
    if (th.road == RS_HAUNT && def.nstamps) { // the mansion's furniture
        static const f32 k_things[][3] = {{700, 132, P_ARMOR}, {340, 132, P_ARMOR}, {600, 132, P_CANDLE}, {440, 132, P_CANDLE}, {520, 280, P_CLOCK},
            {850, 300, P_COFFIN}, {200, 430, P_COFFIN}, {360, 372, P_CANDLE}, {600, 372, P_CANDLE}, {700, 450, P_ARMOR},
            {560, 560, P_PUMPKIN}, {650, 600, P_PUMPKIN}, {300, 720, P_PUMPKIN}, {400, 600, P_BAT}, {250, 600, P_BAT}};
        for (const auto& q : k_things) t.props.push(Prop{vec2{q[0], q[1]}, (u8)q[2], (u8)q[2] != P_BAT && (u8)q[2] != P_PUMPKIN});
    }
    if (th.road == RS_SEWER) // rats, a duck, lamps and outflow pipes along the walls
        for (f32 s = 60.0f; s < L; s += 150.0f) {
            vec2 tan;
            vec2 c = track_point(t, s, &tan);
            f32 hw = track_half_width(t, s);
            static const u8 kinds[4] = {P_SLAMP, P_PIPE, P_RAT, P_LADDER};
            vec2 p = c + vec2{-tan.y, tan.x} * ((((int)(s / 150.0f)) & 1 ? 1.0f : -1.0f) * (hw + 9.0f));
            if (track_surface(t, p.x, p.y) == S_OFF) t.props.push(Prop{p, kinds[((int)(s / 150.0f)) & 3], false});
        }
    if (th.road == RS_TABLE) { // the table is set: a few small things about, nothing else
        static const f32 k_small[][3] = {{452, 652, P_SALT}, {484, 660, P_PEPPER}, {120, 520, P_EGGCUP}, {850, 640, P_EGGCUP}};
        for (const auto& q : k_small) t.props.push(Prop{vec2{q[0], q[1]}, (u8)q[2], true});
    }
    for (f32 s = 30.0f; s < L && th.road != RS_TABLE && th.road != RS_PCB && th.road != RS_SPACE; s += 30.0f) {
        vec2 tan;
        vec2 c = track_point(t, s, &tan);
        vec2 nrm{-tan.y, tan.x};
        f32 hw = track_half_width(t, s);
        for (int sd = -1; sd <= 1; sd += 2) {
            if (th.decor != P_NONE)
                for (int k = 0; k < 2; ++k) {
                    f32 off = hw + def_sh + wall_w + 12.0f + rng.range(0.0f, 70.0f);
                    vec2 p = c + nrm * (off * (f32)sd) + tan * rng.range(-14.0f, 14.0f);
                    if (track_surface(t, p.x, p.y) == S_OUT && !sea)
                        t.props.push(Prop{p, (u8)(rng.chance(0.22f) && th.extra != P_NONE ? th.extra : th.decor), false});
                }
            if (rng.chance(0.14f)) {
                f32 off = hw + 3.0f + mnf(def_sh * 0.6f, 12.0f + rng.range(0.0f, 16.0f));
                vec2 p = c + nrm * (off * (f32)sd);
                if (track_surface(t, p.x, p.y) == S_OFF) t.props.push(Prop{p, (u8)th.obstacle, true});
            }
        }
    }
    if (sea) // palms and crates on the sand, buoys out in the water
        for (int k = 0; k < 60; ++k) {
            vec2 p{rng.range(40.0f, 984.0f), rng.range(40.0f, 984.0f)};
            u8 su = track_surface(t, p.x, p.y);
            if (su == S_OUT || su == S_WATER) t.props.push(Prop{p, P_BUOY, false});
        }

    // corner boards on the outside of every real corner, facing the traffic
    {
        const int look = 7; // line points (42 texels) either way
        f32 last = -1000.0f;
        for (int i = 0; i < n; ++i) {
            vec2 a = t.line[(u32)((i - look + n) % n)], m = t.line[(u32)i], b = t.line[(u32)((i + look) % n)];
            vec2 d0 = m - a, d1 = b - m;
            f32 turn = wrap_pi(kv::atan2(d1.y, d1.x) - kv::atan2(d0.y, d0.x));
            f32 s = t.dist[(u32)i];
            if (kv::fabs(turn) < 0.62f || s - last < 34.0f) continue;
            last = s;
            vec2 tan;
            vec2 c = track_point(t, s + 26.0f, &tan);
            vec2 nrm{-tan.y, tan.x};
            f32 out_side = turn > 0 ? -1.0f : 1.0f; // turning right: the outside is the left
            vec2 p = c + nrm * (out_side * (track_half_width(t, s) + 9.0f));
            u8 su = track_surface(t, p.x, p.y);
            if (su != S_OFF && su != S_OUT) continue;
            SetBody b2{};
            b2.img = turn > 0 ? SPI_CHEVRON_R : SPI_CHEVRON_L;
            b2.pos = p;
            b2.heading = kv::atan2(tan.y, tan.x) + 3.14159265f; // its face toward oncoming karts
            t.statics.push(b2);
        }
        // the start gantry
        vec2 tan;
        SetBody g{};
        g.img = SPI_GANTRY;
        g.pos = track_point(t, 2.0f, &tan);
        g.heading = kv::atan2(tan.y, tan.x);
        t.statics.push(g);
    }
    // nothing is scattered where a big thing stands, or close enough to poke through it
    for (int i = 0; i < def.nobjs; ++i) {
        const Obj& o = def.objs[i];
        if (o.hx <= 0) continue;
        f32 ca = kv::cos(o.heading), sa = kv::sin(o.heading);
        for (u32 p = 0; p < t.props.len;) {
            vec2 d = t.props[p].pos - vec2{o.x, o.y};
            if (kv::fabs(d.x * ca + d.y * sa) < o.hx + 34.0f && kv::fabs(-d.x * sa + d.y * ca) < o.hy + 34.0f) { t.props[p] = t.props[t.props.len - 1]; --t.props.len; }
            else ++p;
        }
    }
    build_layers(t);
}

void track_free(Track& t) {
    t.color.free(); t.surf.free(); t.line.free(); t.dist.free(); t.hw.free(); t.node_s.free();
    t.props.free(); t.boxes.free(); t.bolts.free();
    t.far_layer.free(); t.near_layer.free();
    t.sp.bodies.free(); t.statics.free();
}

// ---- random tracks --------------------------------------------------------------------------
// A layout that is always a track, then a design pass over it.
//
// The layout is the outline of a random block shape on a coarse grid: grow a connected
// shape of cells with no holes and no corner-to-corner pinches, walk its boundary, nudge
// the grid points so nothing is ruler-straight, and round every corner. Grid lines are
// further apart than two roads with their shoulders, so no two stretches ever collide; and
// the outline of a block shape is all straights, square corners, notches (hairpins) and
// steps (chicanes), which is what a circuit is made of.
//
// The design pass then treats it the way the hand-made tracks were laid out: the longest
// straight is the start; the next gets a launch over a gap; another is widened and split by
// a divider with crystals one side and a boost pad the other; corners get mud on the inside
// line, or sheet ice on the frozen theme, or bolts on the racing line, or are pinched
// narrow; water or a canyon fills the infield and the notches.
const TrackDef& track_generate(u32 seed, GenTrack& out) {
    Rng rng = Rng::seeded(0x7A3C0000ull + seed);
    int theme = (int)rng.below((u32)k_theme_count);
    const Theme& th = k_themes[theme];
    const f32 hw = rng.range(29.0f, 33.0f);
    const bool open = theme == 6 || theme == 10, sea = theme == 4;
    const f32 shoulder = open ? 12.0f : (sea ? 30.0f : 36.0f);

    // a block shape on a 4x4 grid
    const int G = 4;
    bool cell[G][G];
    int want = 8 + (int)rng.below(6);
    for (int attempt = 0;; ++attempt) {
        for (int y = 0; y < G; ++y) for (int x = 0; x < G; ++x) cell[y][x] = false;
        int cx = (int)rng.below(G), cy = (int)rng.below(G), count = 1;
        cell[cy][cx] = true;
        for (int tries = 0; count < want && tries < 400; ++tries) {
            int x = (int)rng.below(G), y = (int)rng.below(G);
            if (cell[y][x]) continue;
            bool touch = (x > 0 && cell[y][x - 1]) || (x < G - 1 && cell[y][x + 1]) || (y > 0 && cell[y - 1][x]) || (y < G - 1 && cell[y + 1][x]);
            if (!touch) continue;
            cell[y][x] = true;
            // no pinches: two cells touching only at a corner would make the outline meet itself
            bool pinch = false;
            for (int by = 0; by < G - 1 && !pinch; ++by)
                for (int bx = 0; bx < G - 1; ++bx) {
                    bool a = cell[by][bx], b = cell[by][bx + 1], c = cell[by + 1][bx], d = cell[by + 1][bx + 1];
                    if ((a && d && !b && !c) || (b && c && !a && !d)) { pinch = true; break; }
                }
            if (pinch) { cell[y][x] = false; continue; }
            ++count;
        }
        // no holes: every empty cell must reach the outside
        bool seen[G + 2][G + 2] = {};
        int stack[(G + 2) * (G + 2)], sn = 0;
        stack[sn++] = 0; seen[0][0] = true;
        while (sn) {
            int v = stack[--sn], x = v % (G + 2), y = v / (G + 2);
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                int nx = x + dx[k], ny = y + dy[k];
                if (nx < 0 || ny < 0 || nx >= G + 2 || ny >= G + 2 || seen[ny][nx]) continue;
                bool filled = nx >= 1 && ny >= 1 && nx <= G && ny <= G && cell[ny - 1][nx - 1];
                if (filled) continue;
                seen[ny][nx] = true; stack[sn++] = ny * (G + 2) + nx;
            }
        }
        bool hole = false;
        for (int y = 0; y < G; ++y) for (int x = 0; x < G; ++x) if (!cell[y][x] && !seen[y + 1][x + 1]) hole = true;
        if (!hole && count >= 5) break;
        if (attempt > 200) { for (int y = 0; y < G; ++y) for (int x = 0; x < G; ++x) cell[y][x] = (x < 3 && y < 3) && !(x == 1 && y == 0); break; }
    }

    // walk the outline: edges with the shape on the left as we go, so the loop runs the way
    // the hand-made tracks do (east along the bottom, north up the right)
    auto filled = [&](int x, int y) { return x >= 0 && y >= 0 && x < G && y < G && cell[y][x]; };
    int vx[64], vy[64], nv = 0;
    {
        // start at the bottom-left-most outline vertex of the bottom row
        int sx = -1, sy = -1;
        for (int y = G - 1; y >= 0 && sx < 0; --y) for (int x = 0; x < G; ++x) if (cell[y][x]) { sx = x; sy = y + 1; break; }
        int x = sx, y = sy, dxn = 1, dyn = 0; // heading east along the bottom edge of that cell
        do {
            vx[nv] = x; vy[nv] = y; ++nv;
            x += dxn; y += dyn;
            // at the new vertex: keep the shape on the left (north when heading east, in y-down)
            // try turning left (toward the shape side), then straight, then right
            int lx = dyn, ly = -dxn;  // left of heading in y-down coordinates
            auto edge_ok = [&](int ex, int ey) {
                // moving from (x,y) by (ex,ey): the cell on its left must be filled, the one on its right empty
                int lcx, lcy, rcx, rcy;
                if (ex == 1) { lcx = x; lcy = y - 1; rcx = x; rcy = y; }
                else if (ex == -1) { lcx = x - 1; lcy = y; rcx = x - 1; rcy = y - 1; }
                else if (ey == -1) { lcx = x - 1; lcy = y - 1; rcx = x; rcy = y - 1; }
                else { lcx = x; lcy = y; rcx = x - 1; rcy = y; }
                return filled(lcx, lcy) && !filled(rcx, rcy);
            };
            if (edge_ok(lx, ly)) { dxn = lx; dyn = ly; }
            else if (edge_ok(dxn, dyn)) {}
            else { dxn = -lx; dyn = -ly; }
        } while ((x != sx || y != sy) && nv < 60);
    }
    // grid points, nudged
    f32 gx[G + 1][G + 1], gy[G + 1][G + 1];
    // spread the shape over the whole map: its bounding box sets the pitch each way
    int bx0 = G, bx1 = 0, by0 = G, by1 = 0;
    for (int i = 0; i < nv; ++i) {
        if (vx[i] < bx0) bx0 = vx[i];
        if (vx[i] > bx1) bx1 = vx[i];
        if (vy[i] < by0) by0 = vy[i];
        if (vy[i] > by1) by1 = vy[i];
    }
    const f32 pitch_x = mnf(270.0f, 790.0f / (f32)(bx1 - bx0)), pitch_y = mnf(270.0f, 790.0f / (f32)(by1 - by0));
    const f32 pitch = mnf(pitch_x, pitch_y);
    const f32 ox = 512.0f - pitch_x * (f32)(bx0 + bx1) * 0.5f, oy = 512.0f - pitch_y * (f32)(by0 + by1) * 0.5f;
    for (int y = 0; y <= G; ++y)
        for (int x = 0; x <= G; ++x) {
            gx[y][x] = ox + pitch_x * (f32)x + rng.range(-22.0f, 22.0f);
            gy[y][x] = oy + pitch_y * (f32)y + rng.range(-22.0f, 22.0f);
        }
    // corners only; each becomes an entry and an exit node, so it is rounded
    struct Corner { vec2 p, in, out; f32 turn; };
    Corner cor[40];
    int nc = 0;
    for (int i = 0; i < nv; ++i) {
        int p = (i + nv - 1) % nv, q = (i + 1) % nv;
        int d0x = vx[i] - vx[p], d0y = vy[i] - vy[p], d1x = vx[q] - vx[i], d1y = vy[q] - vy[i];
        if (d0x == d1x && d0y == d1y) continue;
        vec2 here{gx[vy[i]][vx[i]], gy[vy[i]][vx[i]]};
        vec2 from{gx[vy[p]][vx[p]], gy[vy[p]][vx[p]]}, to{gx[vy[q]][vx[q]], gy[vy[q]][vx[q]]};
        vec2 in = here - from, outd = to - here;
        in = in / length(in); outd = outd / length(outd);
        f32 turn = wrap_pi(kv::atan2(outd.y, outd.x) - kv::atan2(in.y, in.x));
        if (nc < 40) cor[nc++] = Corner{here, in, outd, turn};
    }
    // nodes: corner entry, corner exit; the longest straight carries the start
    const f32 round = 70.0f;
    int nn = 0;
    int corner_node[40];
    f32 straight_len[40];
    int best = 0;
    for (int i = 0; i < nc; ++i) {
        const Corner& a = cor[i];
        const Corner& b = cor[(i + 1) % nc];
        straight_len[i] = length((b.p - b.in * round) - (a.p + a.out * round)); // the straight after corner i
        if (straight_len[i] > straight_len[best]) best = i;
    }
    // node 0 sits a third of the way down the longest straight; the corners follow in order
    {
        const Corner& a = cor[best];
        const Corner& b = cor[(best + 1) % nc];
        vec2 s0 = a.p + a.out * round, s1 = b.p - b.in * round;
        out.nodes[nn++] = Node{s0.x + (s1.x - s0.x) * 0.3f, s0.y + (s1.y - s0.y) * 0.3f, 0};
        out.nodes[nn++] = Node{s0.x + (s1.x - s0.x) * 0.7f, s0.y + (s1.y - s0.y) * 0.7f, 0};
    }
    for (int k = 1; k <= nc; ++k) {
        int i = (best + k) % nc;
        const Corner& c = cor[i];
        corner_node[i] = nn;
        vec2 e = c.p - c.in * round, x = c.p + c.out * round;
        out.nodes[nn++] = Node{e.x, e.y, 0};
        out.nodes[nn++] = Node{x.x, x.y, 0};
    }

    // ---- the design pass ----
    int nf = 0;
    auto add = [&](u8 kind, f32 n0, f32 n1, f32 l0, f32 l1, f32 a, u8 b) { if (nf < 64) out.feats[nf++] = Feat{kind, n0, n1, l0, l1, a, b}; };
    add(F_BOOST, 0.2f, 0, 0, 0, 0, 0);
    add(F_BOXES, 1.2f, 0, -0.7f, 0.7f, 4, 0);
    // straights, longest first (the start straight aside)
    int order[40], no = 0;
    for (int i = 0; i < nc; ++i) if (i != best) order[no++] = i;
    for (int a = 1; a < no; ++a)
        for (int b = a; b > 0 && straight_len[order[b]] > straight_len[order[b - 1]]; --b) { int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t; }
    int used = 0;
    for (int k = 0; k < no; ++k) {
        int i = order[k];
        f32 len = straight_len[i];
        f32 n0 = (f32)corner_node[i] + 1.0f; // the straight runs from this corner's exit node to the next node
        if (len < 120.0f) break;
        if (used == 0 && len > 190.0f) {
            f32 g0 = 0.38f, g1 = g0 + 56.0f / len;
            add(F_RAMP, n0 + g0 - 17.0f / len, 0, -1, 1, 0, 0);
            add(F_GAP, n0 + g0, n0 + g1, 0, 0, 34, th.edge);
            add(F_BOLTS, n0 + g0 - 0.04f, n0 + g1 + 0.04f, 0, 0, 0, 0);
        } else if (used == 1 && len > 230.0f) {
            // a split: crystals down one lane, a boost pad down the other
            int a = corner_node[i] + 1, b = (a + 1) % nn;
            out.nodes[a].w = hw * 1.35f; out.nodes[b].w = hw * 1.35f;
            f32 side = rng.chance(0.5f) ? 1.0f : -1.0f;
            add(F_ISLAND, n0 + 0.2f, n0 + 0.8f, -0.1f, 0.1f, 0, 0);
            add(F_BOXES, n0 + 0.5f, 0, side * 0.35f, side * 0.8f, 2, 0);
            add(F_BOOST, n0 + 0.42f, 0, -side * 0.55f, 0, 0, 0);
        } else if (used <= 3) {
            add(F_BOOST, n0 + 0.3f, 0, rng.range(-0.4f, 0.4f), 0, 0, 0);
        } else if (used == 4 || used == 6) {
            add(F_BOXES, n0 + 0.5f, 0, -0.7f, 0.7f, used == 4 ? 4.0f : 3.0f, 0);
        }
        ++used;
    }
    // corners
    int pick = (int)rng.below(4);
    bool narrowed = false;
    for (int i = 0; i < nc; ++i) {
        if (i == (best + 1) % nc || i == best) { ++pick; continue; } // keep the first and last corner of the lap clean
        f32 n0 = (f32)corner_node[i];
        f32 in = cor[i].turn > 0 ? 1.0f : -1.0f;
        switch (pick++ % 4) {
        case 0:
            if (theme == 2) add(F_SURFACE, n0 - 0.25f, n0 + 1.25f, -1, 1, 0, S_ICE);
            else add(F_SURFACE, n0 + 0.1f, n0 + 0.9f, in > 0 ? 0.25f : -1.0f, in > 0 ? 1.0f : -0.25f, 0.8f, S_MUD);
            break;
        case 1: add(F_BOLTS, n0 - 0.1f, n0 + 1.1f, in * 0.62f, in * 0.62f, 0, 0); break;
        case 2:
            if (!narrowed) { out.nodes[corner_node[i]].w = hw * 0.74f; out.nodes[corner_node[i] + 1].w = hw * 0.74f; narrowed = true; }
            else add(F_BOLTS, n0 - 0.1f, n0 + 1.1f, -in * 0.5f, in * 0.6f, 0, 0);
            break;
        default: break;
        }
    }
    // terrain: the notches and the infield hold water or a canyon; the road beside them loses its barrier
    int nst = 0;
    if (!sea && !open) {
        for (int y = 0; y <= G && nst < 6; ++y)
            for (int x = 0; x <= G && nst < 6; ++x) {
                bool inside = filled(x - 1, y - 1) && filled(x, y - 1) && filled(x - 1, y) && filled(x, y);
                if (inside) out.stamps[nst++] = Stamp{ST_DISC, th.edge, gx[y][x], gy[y][x], 0, 0, pitch - hw - 22.0f, 0, 10, 0};
            }
        for (int y = 0; y < G && nst < 6; ++y)
            for (int x = 0; x < G && nst < 6; ++x) {
                if (cell[y][x]) continue;
                int around = (int)filled(x - 1, y) + (int)filled(x + 1, y) + (int)filled(x, y - 1) + (int)filled(x, y + 1);
                if (around >= 2 && rng.chance(0.7f)) {
                    vec2 c{(gx[y][x] + gx[y + 1][x + 1]) * 0.5f, (gy[y][x] + gy[y + 1][x + 1]) * 0.5f};
                    out.stamps[nst++] = Stamp{ST_DISC, th.edge, c.x, c.y, 0, 0, pitch * 0.5f - hw - 6.0f, 0, 10, 0};
                }
            }
    }
    out.def = TrackDef{};
    // a name of its own: the theme, and a word picked by the seed
    static const char* const k_names[] = {"MEADOW", "DUNE", "FROST", "CASTLE", "COVE", "JUNGLE", "NEON", "TABLE", "SEWER", "CIRCUIT", "SPACE", "HAUNTED"};
    static const char* const k_words[] = {"DASH", "LOOP", "SPRINT", "TWIST", "DERBY", "RUSH", "CIRCUIT", "SCRAMBLE", "GALLOP", "TANGLE", "HUSTLE", "RUMBLE"};
    static_assert(sizeof k_names / sizeof k_names[0] == sizeof k_themes / sizeof k_themes[0], "a name for every theme");
    int len = 0;
    for (const char* p = k_names[theme]; *p && len < 22; ++p) out.name[len++] = *p;
    out.name[len++] = ' ';
    for (const char* p = k_words[(seed * 7u + (u32)theme) % 12u]; *p && len < 23; ++p) out.name[len++] = *p;
    out.name[len] = 0;
    out.def.name = out.name;
    out.def.blurb = "A NEW TRACK FOR EVERY SEED";
    out.def.theme = theme;
    out.def.nodes = out.nodes; out.def.nnodes = nn;
    out.def.half_width = hw;
    out.def.feats = out.feats; out.def.nfeats = nf;
    out.def.stamps = out.stamps; out.def.nstamps = nst;
    out.def.shoulder = shoulder;
    out.def.edge = open ? (u8)S_PIT : (sea ? (u8)S_WATER : (u8)0);
    out.def.seed = seed;
    // the set piece runs along the middle of a grid row (and, for traffic, a column), so
    // it meets the road square-on wherever it crosses
    if (th.setpiece == SPK_TRAIN || th.setpiece == SPK_TRAFFIC || th.setpiece == SPK_SNOWBALL || th.setpiece == SPK_DINO) {
        int row = by0, col = bx0, most = -1;
        for (int y = by0; y < by1; ++y) { int c = 0; for (int x = 0; x < G; ++x) c += (int)cell[y][x]; if (c > most) { most = c; row = y; } }
        most = -1;
        for (int x = bx0; x < bx1; ++x) { int c = 0; for (int y = 0; y < G; ++y) c += (int)cell[y][x]; if (c > most) { most = c; col = x; } }
        f32 ly = oy + pitch_y * ((f32)row + 0.5f), lx = ox + pitch_x * ((f32)col + 0.5f);
        out.def.sp[0] = -230; out.def.sp[1] = ly; out.def.sp[2] = 1254; out.def.sp[3] = ly + 6;
        out.def.sp[4] = lx; out.def.sp[5] = -230; out.def.sp[6] = lx + 6; out.def.sp[7] = 1254;
    }
    return out.def;
}

} // namespace sr
