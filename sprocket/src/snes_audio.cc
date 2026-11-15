// SNES-style sound: an emulation of the S-DSP's shape, not of any one game's driver.
//
//   - 32 kHz output, eight voices total: five for music, one for the player's engine, two
//     for sound effects (effects never take music voices; the SNES drivers that did were
//     working around the same limit)
//   - samples are BRR blocks (tools/bake_audio.py) decoded once at startup, read with a
//     4-tap Gaussian-style interpolator, pitch capped at the DSP's ~4x
//   - ADSR envelopes with a per-instrument release, direct gain for the engine and the
//     effects, the 15-bit noise generator, and the echo unit: a delay of EDL x 16 ms, an
//     8-tap FIR on the echo, feedback and echo volume (set per song)
//   - what the drivers added on top: vibrato after a delay on held notes, release tails,
//     pitch and volume slides, and a tempo lift for the final lap
//   - each song has its own instrument set (tools/bake_audio.py SONG_SETS), which with the
//     effects' samples, the echo buffer and the driver fits the 64 KB of audio RAM, as a
//     song's upload would on the SNES; startup warns if a song strays outside its set
//
// The sequencer runs on the audio thread, sample-accurately. Songs are written below in
// a small tracker notation, one string per channel, one token per 16th-note row.
#include "common.h"
#include "audio_bank.h"
#include "korvid/core/atomic.h"
#include "korvid/core/log.h"
#include "korvid/platform_audio/audio.h"
#include <string.h>

namespace sr {

constexpr int RATE = 32000;
constexpr int VOICES = 8, MUSIC_CH = 5, ENGINE_V = 5, SFX_V0 = 6;
constexpr int ECHO_FRAMES = 4 * 512; // EDL 4: 64 ms

// ---- instruments --------------------------------------------------------------------------
// attack and decay in seconds, sustain level, fade (seconds for a held note to die away, 0:
// it holds), release (seconds for a keyed-off note to fall 60 dB)
struct Adsr { f32 attack, decay, sustain, fade, release; };
struct AdsrDef { int smp; Adsr adsr; };
static const AdsrDef k_adsr_defs[] = {
    {SMP_LEAD, {0.005f, 0.25f, 0.70f, 3.0f, 0.08f}},
    {SMP_GUITAR, {0.002f, 0.30f, 0.60f, 2.5f, 0.06f}},
    {SMP_SLAP, {0.001f, 0.15f, 0.50f, 1.2f, 0.05f}},
    {SMP_BRASS, {0.020f, 0.20f, 0.80f, 0.0f, 0.09f}},
    {SMP_STRINGS, {0.060f, 0.30f, 0.80f, 0.0f, 0.22f}},
    {SMP_SQUARE, {0.001f, 0.10f, 0.70f, 0.0f, 0.03f}},
    {SMP_EPIANO, {0.001f, 0.40f, 0.40f, 2.0f, 0.18f}},
    {SMP_ORCHHIT, {0.001f, 0.50f, 0.00f, 0.5f, 0.10f}},
    {SMP_SAW, {0.002f, 0.20f, 0.80f, 0.0f, 0.05f}},
    {SMP_OVERDRIVE, {0.002f, 0.30f, 0.70f, 3.0f, 0.09f}},
    {SMP_SYNBASS, {0.001f, 0.20f, 0.60f, 1.5f, 0.04f}},
    {SMP_VIBES, {0.001f, 0.30f, 0.40f, 1.5f, 0.30f}},
    {SMP_GSYNTH, {0.003f, 0.30f, 0.70f, 3.0f, 0.10f}},
    {SMP_KICK, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    {SMP_SNARE, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    {SMP_PSNARE, {0.001f, 0.05f, 0.80f, 0.35f, 0.012f}},
    {SMP_HAT, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    {SMP_OHAT, {0.001f, 0.05f, 0.70f, 0.30f, 0.012f}},
    {SMP_CRASH, {0.001f, 0.10f, 0.80f, 1.40f, 0.012f}},
    {SMP_TOM, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    {SMP_STOM, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    // Crown Keep
    {SMP_PORGAN, {0.006f, 0.15f, 0.85f, 0.0f, 0.06f}},
    {SMP_CHOIR, {0.080f, 0.30f, 0.85f, 0.0f, 0.25f}},
    {SMP_HARP, {0.001f, 0.35f, 0.35f, 1.5f, 0.30f}},
    {SMP_DBASS, {0.002f, 0.20f, 0.70f, 1.5f, 0.05f}},
    {SMP_TIMPANI, {0.001f, 0.00f, 1.00f, 0.0f, 0.10f}},
    // Gullwater Cove
    {SMP_ACCORD, {0.010f, 0.15f, 0.80f, 0.0f, 0.06f}},
    {SMP_FLUTE, {0.020f, 0.20f, 0.80f, 0.0f, 0.08f}},
    {SMP_FIDDLE, {0.012f, 0.20f, 0.70f, 0.0f, 0.06f}},
    {SMP_HORN, {0.015f, 0.25f, 0.75f, 0.0f, 0.08f}},
    {SMP_PBASS, {0.001f, 0.25f, 0.55f, 1.2f, 0.05f}},
    {SMP_TAMB, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    {SMP_CASTANET, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    // Fossil Falls
    {SMP_MARIMBA, {0.001f, 0.30f, 0.45f, 1.0f, 0.15f}},
    {SMP_PANFLUTE, {0.025f, 0.20f, 0.80f, 0.0f, 0.12f}},
    {SMP_TRUMPET, {0.015f, 0.20f, 0.80f, 0.0f, 0.07f}},
    {SMP_ABASS, {0.002f, 0.25f, 0.60f, 1.5f, 0.05f}},
    {SMP_BONGO, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    {SMP_JTOM, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    {SMP_TAIKO, {0.001f, 0.10f, 0.80f, 0.6f, 0.05f}},
    // Neon Sprawl
    {SMP_SYNLEAD, {0.004f, 0.25f, 0.75f, 0.0f, 0.08f}},
    {SMP_PULSE, {0.001f, 0.12f, 0.45f, 0.6f, 0.05f}},
    {SMP_MOOG, {0.001f, 0.15f, 0.60f, 1.2f, 0.04f}},
    {SMP_CLAP, {0.001f, 0.00f, 1.00f, 0.0f, 0.012f}},
    // the engine (direct gain; these are only its defaults)
    {SMP_ENGINE, {0.010f, 0.00f, 1.00f, 0.0f, 0.05f}},
};
static f32 g_master = 1.0f; // the master volume, set from the options (read by the mixer)
static Adsr k_adsr[SMP_COUNT];
static const char* const* const k_inst_names = k_smp_names;

static i16 g_pcm[k_bank_pcm_total];  // decoded BRR, 15-bit range, every sample back to back
static u32 g_pcm_off[SMP_COUNT];

static void decode_bank() {
    u32 off = 0;
    for (int s = 0; s < SMP_COUNT; ++s) {
        g_pcm_off[s] = off;
        off += k_bank[s].samples;
        const BankSample& b = k_bank[s];
        const u8* p = k_brr + b.brr_offset;
        i32 p1 = 0, p2 = 0;
        u32 n = 0;
        for (u32 blk = 0; blk < b.samples / 16; ++blk, p += 9) {
            int shift = p[0] >> 4, filt = (p[0] >> 2) & 3;
            for (int i = 0; i < 16; ++i) {
                int nib = (p[1 + i / 2] >> ((i & 1) ? 0 : 4)) & 15;
                if (nib >= 8) nib -= 16;
                i32 v = shift <= 12 ? ((nib << shift) >> 1) : (nib < 0 ? -2048 : 0);
                if (filt == 1) v += p1 + ((-p1) >> 4);
                else if (filt == 2) v += (p1 << 1) + ((-((p1 << 1) + p1)) >> 5) - p2 + (p2 >> 4);
                else if (filt == 3) v += (p1 << 1) + ((-(p1 + (p1 << 2) + (p1 << 3))) >> 6) - p2 + (((p2 << 1) + p2) >> 4);
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                v = (i16)(v << 1) >> 1;
                if (n < b.samples) g_pcm[g_pcm_off[s] + n++] = (i16)v;
                p2 = p1; p1 = v;
            }
        }
    }
}

// ---- songs --------------------------------------------------------------------------------
// Tokens: notes C4 F#3 Bb2 (optionally :rows), '-' tie, '.' key off/rest, drums k s p h o c t y
// (kick, snare, power snare, hat, open hat, crash, tom, synth tom) and n q b j i x (tambourine,
// castanet, bongo, jungle tom, taiko, clap), '@NAME' instrument (a drum channel may also play
// pitched notes on an instrument, e.g. '@TIMPANI G2'),
// 'vN' volume (127 is the sample at full level; quiet samples may go to 255), 'pN' pan -64..64, 'e1'/'e0' echo, '[ ... ]N' repeat, '|' bar line,
// 'mN' vibrato depth in cents (0 off: a held note starts to waver after a quarter second),
// 'rN' release tail in 10 ms units for the channel's key-offs (0: the instrument's own),
// 'gN' portamento: a note that follows another without a rest slides to its pitch in N x 10 ms.
//
// A song: name, tempo, rows per bar, whether it loops, its level (the main volume: every
// song is set to play about equally loud), echo volume and feedback, its chords (one per
// bar; "X,Y" splits a bar, "X:n" lasts n rows; for the reader and for the harmony check
// that is run on the offline renders), and five channels: lead, harmony, bass, drums, cymbals.
// The race songs are A B A C: the tune, a contrasting strain, the tune again, then a
// breakdown that builds back into the top.
struct SongDef { const char* name; int bpm; int bar_rows; bool loop; f32 gain, echo_vol, echo_fb; const char* chords; const char* ch[MUSIC_CH]; };

// TITLE, G major
#define TI_A0 "@LEAD v88 p-8 e1 m16 " \
    "D5:2 G5:2 B5:4 A5:2 G5:2 D5:4 | C5:2 F5:2 A5:4 G5:2 F5:2 C5:4 | E5:4 G5:4 C6:6 B5:2 | B5:8 .:4 D5:2 E5:2 |" \
    "G5:2 A5:2 B5:4 D6:4 B5:4 | C6:4 A5:2 F5:2 A5:4 C6:4 | v72 E6:6 v88 D6:2 C6:4 G5:4 | A5:4 B5:2 C6:2 v72 D6:8 v88 |"
#define TI_A1 "@BRASS v58 p20 e1 " \
    "B4:8 D5:8 | A4:8 C5:8 | G4:8 E5:8 | D5:16 | B4:8 D5:8 | A4:8 C5:8 | C5:8 E5:8 | F#4:8 A4:8 |"
#define TI_A2 "@SLAP v104 p0 e0 " \
    "G1:2 G2:2 .:2 G1:2 G2:2 G1:2 D2:4 | F1:2 F2:2 .:2 F1:2 F2:2 F1:2 C2:4 | C2:2 C3:2 .:2 C2:2 C3:2 C2:2 G2:4 | G1:2 G2:2 .:2 G1:2 G2:2 G1:2 D2:4 |" \
    "G1:2 G2:2 .:2 G1:2 G2:2 G1:2 D2:4 | F1:2 F2:2 .:2 F1:2 F2:2 F1:2 C2:4 | C2:2 C3:2 .:2 C2:2 C3:2 C2:2 G2:4 | D2:2 D3:2 .:2 D2:2 D3:2 D2:2 A2:4 |"
#define TI_A3 "[ k . . . . . . . s . . . . . k . ]7 k . . . . . . . s . s . s s s s "
#define TI_A4 "[ c . h . h . h . h . h . h . h . [ h . h . h . h . h . h . h . o . ]3 ]2 "
// B: the relative minor, the lead an octave down over a backbeat
#define TI_B0 "@LEAD v90 p-8 e1 m16 " \
    "E5:4 G5:2 E5:2 B4:4 .:2 B4:2 | C5:4 E5:2 C5:2 G4:6 .:2 | D5:4 G5:2 D5:2 B4:4 D5:2 G5:2 | F#5:6 E5:2 D5:4 A4:4 |" \
    "E5:4 G5:2 E5:2 B4:4 .:2 B4:2 | C5:4 E5:2 G5:2 E5:6 .:2 | A4:2 C5:2 E5:4 D5:2 C5:2 A4:4 | D5:4 F#5:4 A5:4 D5:2 E5:2 |"
#define TI_B1 "@BRASS v54 p20 e1 G4:16 | E4:16 | B4:16 | A4:16 | G4:16 | E4:16 | E4:16 | F#4:8 A4:8 |"
#define TI_B2 "@SLAP v104 p0 e0 " \
    "E2:4 .:2 E2:2 B2:4 E3:2 B2:2 | C2:4 .:2 C2:2 G2:4 C3:2 G2:2 | G1:4 .:2 G1:2 D2:4 G2:2 D2:2 | D2:4 .:2 D2:2 A2:4 D3:2 A2:2 |" \
    "E2:4 .:2 E2:2 B2:4 E3:2 B2:2 | C2:4 .:2 C2:2 G2:4 C3:2 G2:2 | A1:4 .:2 A1:2 E2:4 A2:2 E2:2 | D2:4 .:2 D2:2 A2:4 D3:2 A2:2 |"
#define TI_B3 "[ k . . . s . . . k . k . s . . . ]7 k . . . s . . . k . s . s s t t "
#define TI_B4 "c . h . h . o . h . h . h . o . [ h . h . h . o . h . h . h . o . ]7 "
// C: breakdown on the subdominant, then four bars of dominant build
#define TI_C0 "@LEAD v86 p-8 e1 m16 " \
    ".:4 E5:2 G5:2 E5:4 C5:4 | .:4 F#5:2 A5:2 F#5:4 D5:4 | .:4 G5:2 B5:2 G5:4 E5:4 | B4:2 E5:2 G5:2 E5:2 B4:8 |" \
    ".:4 E5:2 G5:2 E5:4 C5:4 | .:4 F#5:2 A5:2 F#5:4 D5:4 | A4:2 D5:2 F#5:2 A5:2 F#5:2 D5:2 A4:2 D5:2 | F#5:4 A5:4 F#5:4 D5:2 E5:2 |"
#define TI_C1 ".:64 @BRASS v56 p20 e1 E4:16 | F#4:16 | F#4:8 A4:8 | A4:8 D5:8 |"
#define TI_C2 "@SLAP v104 p0 e0 " \
    "C2:8 C2:4 G2:4 | D2:8 D2:4 A2:4 | E2:8 E2:4 B2:4 | E2:8 E2:2 E3:2 B2:2 E2:2 |" \
    "[ C2:2 C2:2 C3:2 C2:2 C2:2 C2:2 C3:2 G2:2 ] [ D2:2 D2:2 D3:2 D2:2 D2:2 D2:2 D3:2 A2:2 ]3 "
#define TI_C3 "[ k . . . . . k . . . . . . . . . ]3 k . . . . . k . . . s . s . s s [ k . . . s . . . k . . . s . . . ]3 k . s . k . s . s . s . s s s s "
#define TI_C4 "[ h . . . h . . . h . . . h . . . ]4 c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]3 "

// SPROCKET PARK, F major
#define PK_A0 "@LEAD v86 p-10 e1 m16 " \
    "C5:2 F5:2 A5:3 G5:1 F5:2 G5:2 A5:4 | D5:2 F5:2 A5:2 D6:6 C6:2 A5:2 | Bb5:3 A5:1 G5:2 F5:2 D5:4 F5:4 | G5:2 A5:2 Bb5:2 A5:2 G5:2 E5:2 C5:4 |" \
    "C5:2 F5:2 A5:3 G5:1 F5:2 G5:2 A5:2 C6:2 | v72 E6:4 v86 D6:2 C6:2 A5:4 E5:4 | F5:2 G5:2 A5:2 Bb5:2 C6:2 D6:2 E6:2 v72 G6:2 | F6:8 v86 C6:2 A5:2 F5:4 |"
#define PK_A1 "@BRASS v54 p22 e1 " \
    "[ .:2 A4 .:3 A4 - .:2 A4 .:3 C5 . ] [ .:2 F4 .:3 F4 - .:2 F4 .:3 A4 . ] [ .:2 D5 .:3 D5 - .:2 D5 .:3 F5 . ] [ .:2 E4 .:3 E4 - .:2 G4 .:3 G4 . ]" \
    "[ .:2 A4 .:3 A4 - .:2 A4 .:3 C5 . ] [ .:2 C5 .:3 C5 - .:2 E5 .:3 E5 . ] [ .:2 D5 .:3 D5 - .:2 E5 .:3 E5 . ] [ .:2 A4 .:3 A4 - .:2 C5 .:3 C5 . ]"
#define PK_A2 "@SLAP v108 p0 e0 " \
    "F2 . F3 . . F2 F3 . F2 . F3 . C3 . E3 . | D2 . D3 . . D2 D3 . D2 . D3 . A2 . C3 . | Bb1 . Bb2 . . Bb1 Bb2 . Bb1 . Bb2 . F2 . A2 . | C2 . C3 . . C2 C3 . C2 . C3 . G2 . B2 . |" \
    "F2 . F3 . . F2 F3 . F2 . F3 . C3 . E3 . | A1 . A2 . . A1 A2 . A1 . A2 . E2 . G2 . | Bb1 . Bb2 . Bb1 Bb2 . . C2 . C3 . C2 C3 . . | F2 . F3 . . F2 F3 . F2 . C3 . F3 . E3 . |"
#define PK_A3 "[ k . . . s . . k k . . . s . . . ]7 k . . k s . . k k . s . s s s s "
#define PK_A4 "[ c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]3 ]2 "
// B: the subdominant side, a square wave sings the tune in long notes over strings and a disco bass
#define PK_B0 "@SQUARE v88 p-6 e1 m18 " \
    "D5:6 F5:2 Bb4:4 D5:4 | E5:6 G5:2 C5:4 E5:4 | C5:2 E5:2 A5:4 G5:2 E5:2 C5:4 | D5:2 F5:2 A5:4 F5:4 D5:4 |" \
    "D5:6 F5:2 Bb5:4 F5:4 | E5:6 G5:2 C6:4 G5:4 | A5:2 G5:2 F5:2 C5:2 F5:4 A5:4 | G5:6 E5:2 C5:4 E5:4 |"
#define PK_B1 "@STRINGS v50 p18 e1 F4:16 | G4:16 | E4:16 | F4:16 | F4:16 | G4:16 | A4:16 | G4:8 E4:8 |"
#define PK_B2 "@SLAP v108 p0 e0 " \
    "Bb1:2 Bb2:2 Bb1:2 Bb2:2 Bb1:2 Bb2:2 F2:2 Bb2:2 | C2:2 C3:2 C2:2 C3:2 C2:2 C3:2 G2:2 C3:2 | A1:2 A2:2 A1:2 A2:2 A1:2 A2:2 E2:2 A2:2 | D2:2 D3:2 D2:2 D3:2 D2:2 D3:2 A2:2 D3:2 |" \
    "Bb1:2 Bb2:2 Bb1:2 Bb2:2 Bb1:2 Bb2:2 F2:2 Bb2:2 | C2:2 C3:2 C2:2 C3:2 C2:2 C3:2 G2:2 C3:2 | F2:2 F3:2 F2:2 F3:2 F2:2 F3:2 C3:2 F3:2 | C2:2 C3:2 C2:2 C3:2 C2:2 C3:2 G2:2 C3:2 |"
#define PK_B3 "[ k . . . s . . . k . k . s . . . ]7 k . . . s . . . k . s . s s t t "
#define PK_B4 "c . o . h . o . h . o . h . o . [ h . o . h . o . h . o . h . o . ]7 "
// C: breakdown in the relative minor, then the build on the dominant
#define PK_C0 "@LEAD v86 p-10 e1 m16 " \
    "F5:2 D5:2 .:4 F5:2 D5:2 A4:4 | F5:2 D5:2 .:4 F5:2 D5:2 Bb4:4 | F5:2 C5:2 .:4 F5:2 C5:2 A4:4 | E5:2 C5:2 .:4 G5:2 E5:2 C5:4 |" \
    "F5:2 D5:2 A5:2 F5:2 D5:2 F5:2 A5:4 | F5:2 D5:2 Bb5:2 F5:2 D5:2 F5:2 Bb5:4 | G5:2 E5:2 C6:2 G5:2 E5:2 G5:2 C6:4 | G5:2 E5:2 C5:2 E5:2 G5:4 E5:4 |"
#define PK_C1 ".:64 @BRASS v52 p22 e1 A4:16 | Bb4:16 | G4:16 | E4:8 G4:8 |"
#define PK_C2 "@SLAP v108 p0 e0 " \
    "D2:6 D2:2 .:4 A2:2 D3:2 | Bb1:6 Bb1:2 .:4 F2:2 Bb2:2 | F2:6 F2:2 .:4 C3:2 F3:2 | C2:6 C2:2 .:4 G2:2 C3:2 |" \
    "[ D2:2 ]8 [ Bb1:2 ]8 [ C2:2 ]8 [ C2:2 ]6 C3:2 G2:2 "
#define PK_C3 "[ k . . . . . . . k . . k . . . . ]3 k . . . . . . . k . . k s . s s [ k . . . s . . k k . . . s . . . ]3 k . s . k . s . s s s s t t t t "
#define PK_C4 "[ h . . . h . . . h . . . h . . . ]4 c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]3 "

// DUNE DRIFT, E minor
#define DU_A0 "@OVERDRIVE v84 p-12 e1 m14 " \
    "B4:4 E5:2 F#5:2 G5:4 F#5:2 E5:2 | D5:2 E5:2 B4:8 .:4 | C5:2 E5:2 G5:4 A5:2 G5:2 E5:4 | F#5:4 A5:4 D6:4 C6:2 A5:2 |" \
    "B5:4 G5:2 A5:2 B5:4 D6:2 B5:2 | v74 E6:8 v84 D6:4 B5:4 | C6:4 B5:2 A5:2 D6:4 C6:2 A5:2 | B5:8 A5:2 G5:2 F#5:2 D#5:2 |"
// (no rhythm guitar: it fought the melody; the strings hold one chord tone a bar instead)
#define DU_A1 "@STRINGS v44 p18 e1 B3:16 | G3:16 | C4:16 | D4:16 | E4:16 | G4:16 | E4:8 A4:8 | B3:16 |"
#define DU_A2 "@SYNBASS v104 p0 e0 " \
    "[ E2 . E3 E2 . E2 E3 . E2 . E3 E2 . E2 D3 . ]2 [ C3 . C4 C3 . C3 C4 . C3 . C4 C3 . C3 B2 . ] [ D3 . D4 D3 . D3 D4 . D3 . D4 D3 . D3 C#3 . ]" \
    "[ E2 . E3 E2 . E2 E3 . E2 . E3 E2 . E2 D3 . ]2 C3 . C4 C3 . C3 C4 . D3 . D4 D3 . D3 D4 . | B2 . B3 B2 . B2 B3 . B2 . B3 B2 . B2 D#3 . |"
#define DU_A3 "[ k . k . s . k . k . k . s . k s ]7 k . k . s . k . s s s s y y t t "
#define DU_A4 "[ c h h h h h h h h h h h o h h h [ h h h h h h h h h h h h o h h h ]3 ]2 "
// B: the relative major; guitar and bass hit a 3-3-2 figure together, an octave lower
#define DU_B0 "@OVERDRIVE v86 p-12 e1 m14 " \
    "D5:3 B4:3 G4:2 B4:3 D5:3 G5:2 | F#5:3 D5:3 A4:2 D5:3 F#5:3 A5:2 | E5:3 C5:3 A4:2 C5:3 E5:3 A5:2 | G5:4 F#5:2 E5:2 B4:8 |" \
    "D5:3 B4:3 G4:2 B4:3 D5:3 G5:2 | F#5:3 D5:3 A4:2 D5:3 F#5:3 A5:2 | G5:3 E5:3 C5:2 E5:3 G5:3 C6:2 | B5:4 F#5:4 D#5:4 F#5:2 A5:2 |"
#define DU_B1 "@STRINGS v44 p18 e1 D4:16 | A3:16 | C4:16 | B3:16 | D4:16 | F#4:16 | G4:16 | F#4:16 |"
#define DU_B2 "@SYNBASS v104 p0 e0 " \
    "G2:3 G2:3 G3:2 G2:3 G2:3 D3:2 | D2:3 D2:3 D3:2 D2:3 D2:3 A2:2 | A2:3 A2:3 A3:2 A2:3 A2:3 E3:2 | E2:3 E2:3 E3:2 E2:3 E2:3 B2:2 |" \
    "G2:3 G2:3 G3:2 G2:3 G2:3 D3:2 | D2:3 D2:3 D3:2 D2:3 D2:3 A2:2 | C3:3 C3:3 C4:2 C3:3 C3:3 G3:2 | B2:3 B2:3 B3:2 B2:3 B2:3 F#3:2 |"
#define DU_B3 "[ k . . k s . k . k . . k s . k . ]7 k . . k s . k . k . s . y y t t "
#define DU_B4 "c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7 "
// C: tom breakdown with trumpet calls, then the build on the dominant
#define DU_C0 "@TRUMPET v112 p-8 e1 m14 " \
    "E5:8 .:2 B4:2 E5:2 G5:2 | G5:4 E5:4 B4:8 | E5:8 .:2 C5:2 E5:2 G5:2 | G5:4 E5:4 C5:8 |" \
    "A4:2 C5:2 E5:4 A4:2 C5:2 E5:4 | A4:2 C5:2 E5:2 A5:2 E5:8 | B4:2 D#5:2 F#5:4 B4:2 D#5:2 F#5:4 | B4:2 D#5:2 F#5:2 A5:2 B5:4 F#5:2 D#5:2 |"
#define DU_C1 "@STRINGS v44 p18 e1 B3:16 | E4:16 | C4:16 | E4:16 | E4:16 | C4:16 | D#4:16 | F#4:16 |"
#define DU_C2 "@SYNBASS v104 p0 e0 " \
    "[ E2:8 .:4 E2:2 E3:2 ]2 [ C2:8 .:4 C2:2 C3:2 ]2 [ A2:2 ]16 [ B2:2 ]16 "
#define DU_C3 "[ k . . . . . y . . . y . t . . . ]4 [ k . . . s . . . k . . . s . . . ]2 k . s . k . s . k . s . k . s . k . s s s s s s s s s s y y t t "
#define DU_C4 "[ h . . . h . . . h . . . h . . . ]4 c h h h h h h h h h h h o h h h [ h h h h h h h h h h h h o h h h ]3 "

// FROST RING, D dorian
#define FR_A0 "@GSYNTH v112 p-10 e1 m16 " \
    "A4:4 D5:2 E5:2 F5:4 E5:2 D5:2 | G5:6 F5:2 E5:4 D5:4 | C5:2 E5:2 F5:2 A5:6 G5:2 F5:2 | E5:8 B4:4 D5:4 |" \
    "A5:4 C6:2 A5:2 G5:4 F5:2 E5:2 | D5:4 G5:4 B5:4 D6:4 | F6:6 D6:2 Bb5:4 F5:4 | E5:2 F5:2 G5:2 A5:2 C#6:4 E6:4 |"
#define FR_A1 "@EPIANO v48 p26 e1 " \
    "[ D4 F4 A4 C5 A4 F4 D4 F4 A4 C5 A4 F4 D4 F4 A4 C5 ] [ E4 G4 B4 D5 B4 G4 E4 G4 B4 D5 B4 G4 E4 G4 B4 D5 ] [ F4 A4 C5 E5 C5 A4 F4 A4 C5 E5 C5 A4 F4 A4 C5 E5 ] [ E4 G4 B4 D5 B4 G4 E4 G4 B4 D5 B4 G4 E4 G4 B4 D5 ]" \
    "[ D4 F4 A4 C5 A4 F4 D4 F4 A4 C5 A4 F4 D4 F4 A4 C5 ] [ G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 ] [ Bb3 D4 F4 A4 F4 D4 Bb3 D4 F4 A4 F4 D4 Bb3 D4 F4 A4 ] [ A3 C#4 E4 G4 E4 C#4 A3 C#4 E4 G4 E4 C#4 A3 C#4 E4 G4 ]"
#define FR_A2 "@SLAP v92 p0 e0 " \
    "D2:3 D3:1 .:2 D2:2 D3:2 D2:2 D3:2 C3:2 | E2:3 E3:1 .:2 E2:2 E3:2 E2:2 E3:2 D3:2 | F2:3 F3:1 .:2 F2:2 F3:2 F2:2 F3:2 E3:2 | E2:3 E3:1 .:2 E2:2 E3:2 E2:2 E3:2 C#3:2 |" \
    "D2:3 D3:1 .:2 D2:2 D3:2 D2:2 D3:2 C3:2 | G1:3 G2:1 .:2 G1:2 G2:2 G1:2 G2:2 F2:2 | Bb1:3 Bb2:1 .:2 Bb1:2 Bb2:2 Bb1:2 Bb2:2 A2:2 | A1:3 A2:1 .:2 A1:2 A2:2 A1:2 A2:2 C#3:2 |"
#define FR_A3 "[ k . . . s . . . k . k . s . . . ]7 k . . . s . . k k . s . s . s s "
#define FR_A4 "e1 [ c . h h h . h . h . h h o . h . [ h . h h h . h . h . h h o . h . ]3 ]2 "
// B: the relative major, half time; vibes carry the tune over held strings
#define FR_B0 "@VIBES v100 p-6 e1 m0 " \
    "A5:2 F5:2 C5:2 F5:2 A5:4 G5:2 F5:2 | G5:2 E5:2 C5:2 E5:2 G5:4 F5:2 E5:2 | Bb5:2 G5:2 D5:2 G5:2 Bb5:4 A5:2 G5:2 | A5:4 F5:4 D5:8 |" \
    "A5:2 F5:2 C5:2 F5:2 A5:4 C6:2 A5:2 | G5:2 E5:2 C5:2 E5:2 G5:4 C6:2 G5:2 | F5:2 D5:2 Bb4:2 D5:2 F5:4 Bb5:4 | E5:4 C#5:4 A4:4 C#5:2 E5:2 |"
#define FR_B1 "@STRINGS v46 p20 e1 C4:16 | G4:16 | D4:16 | F4:16 | A4:16 | G4:16 | F4:16 | E4:16 |"
#define FR_B2 "@SLAP v92 p0 e0 " \
    "F2:4 F2:2 C3:2 F3:2 C3:2 F2:2 C3:2 | C2:4 C2:2 G2:2 C3:2 G2:2 C2:2 G2:2 | G1:4 G1:2 D2:2 G2:2 D2:2 G1:2 D2:2 | D2:4 D2:2 A2:2 D3:2 A2:2 D2:2 A2:2 |" \
    "F2:4 F2:2 C3:2 F3:2 C3:2 F2:2 C3:2 | C2:4 C2:2 G2:2 C3:2 G2:2 C2:2 G2:2 | Bb1:4 Bb1:2 F2:2 Bb2:2 F2:2 Bb1:2 F2:2 | A1:4 A1:2 E2:2 A2:2 E2:2 A1:2 E2:2 |"
#define FR_B3 "[ k . . . . . . . s . . . . . k . ]7 k . . . . . . . s . . . s . t t "
#define FR_B4 "e1 c . . . h . . . h . . . h . o . [ h . . . h . . . h . . . h . o . ]7 "
// C: breakdown (the piano holds chords), then the arpeggios return for the build to A7
#define FR_C0 "@GSYNTH v112 p-10 e1 m16 " \
    "A4:4 D5:4 F5:6 E5:2 | D5:8 .:4 A4:2 D5:2 | Bb4:4 D5:4 F5:6 D5:2 | Bb4:8 .:4 D5:2 F5:2 |" \
    "G5:4 D5:4 Bb4:4 D5:4 | G5:4 Bb5:4 A5:2 G5:2 D5:4 | A4:4 C#5:4 E5:4 A5:4 | G5:4 E5:4 C#5:4 E5:4 |"
#define FR_C1 "@EPIANO v56 p26 e1 D4:8 A4:8 | F4:8 A4:8 | D4:8 F4:8 | Bb3:8 D4:8 | v50 " \
    "[ G3 Bb3 D4 G4 D4 Bb3 G3 Bb3 D4 G4 D4 Bb3 G3 Bb3 D4 G4 ]2 [ A3 C#4 E4 G4 E4 C#4 A3 C#4 E4 G4 E4 C#4 A3 C#4 E4 G4 ]2 "
#define FR_C2 "@SLAP v92 p0 e0 " \
    "[ D2:8 .:2 D2:2 A2:2 D3:2 ]2 [ Bb1:8 .:2 Bb1:2 F2:2 Bb2:2 ]2 " \
    "[ G1:3 G2:1 .:2 G1:2 G2:2 G1:2 G2:2 D2:2 ]2 [ A1:3 A2:1 .:2 A1:2 A2:2 A1:2 A2:2 C#3:2 ]2 "
#define FR_C3 "[ k . . . . . . . . . k . . . . . ]3 k . . . . . . . . . k . s . s . [ k . . . s . . . k . k . s . . . ]3 k . . . s . . k k . s . s s t t "
#define FR_C4 "e1 [ h . . . . . . . h . . . . . . . ]4 c . h h h . h . h . h h o . h . [ h . h h h . h . h . h h o . h . ]3 "

static const SongDef k_songs[] = {
    {   "TITLE", 136, 16, true, 0.71f, 0.30f, 0.42f,
        "G F C G G F C D | Em C G D Em C Am D | G F C G G F C D | C D Em Em C D D D", {
        TI_A0 TI_B0 TI_A0 TI_C0,
        TI_A1 TI_B1 TI_A1 TI_C1,
        TI_A2 TI_B2 TI_A2 TI_C2,
        "v110 p0 e0 " TI_A3 TI_B3 TI_A3 TI_C3,
        "v62 p-24 e0 " TI_A4 TI_B4 TI_A4 TI_C4,
    }},
    {   "SPROCKET PARK", 152, 16, true, 0.68f, 0.30f, 0.42f,
        "F Dm Bb C F Am Bb,C F | Bb C Am Dm Bb C F C | F Dm Bb C F Am Bb,C F | Dm Bb F C Dm Bb C C", {
        PK_A0 PK_B0 PK_A0 PK_C0,
        PK_A1 PK_B1 PK_A1 PK_C1,
        PK_A2 PK_B2 PK_A2 PK_C2,
        "v112 p0 e0 " PK_A3 PK_B3 PK_A3 PK_C3,
        "v58 p-20 e0 " PK_A4 PK_B4 PK_A4 PK_C4,
    }},
    {   "DUNE DRIFT", 164, 16, true, 0.64f, 0.30f, 0.42f,
        "Em Em C D Em Em C,D B | G D Am Em G D C B | Em Em C D Em Em C,D B | Em Em C C Am Am B B", {
        DU_A0 DU_B0 DU_A0 DU_C0,
        DU_A1 DU_B1 DU_A1 DU_C1,
        DU_A2 DU_B2 DU_A2 DU_C2,
        "v114 p0 e0 " DU_A3 DU_B3 DU_A3 DU_C3,
        "v56 p-22 e0 " DU_A4 DU_B4 DU_A4 DU_C4,
    }},
    {   "FROST RING", 148, 16, true, 0.87f, 0.32f, 0.45f,
        "Dm7 Em7 Fmaj7 Em7 Dm7 G Bb A7 | F C Gm Dm F C Bb A | Dm7 Em7 Fmaj7 Em7 Dm7 G Bb A7 | Dm Dm Bb Bb Gm Gm A7 A7", {
        FR_A0 FR_B0 FR_A0 FR_C0,
        FR_A1 FR_B1 FR_A1 FR_C1,
        FR_A2 FR_B2 FR_A2 FR_C2,
        "v108 p0 e0 " FR_A3 FR_B3 FR_A3 FR_C3,
        "v56 p-22 " FR_A4 FR_B4 FR_A4 FR_C4,
    }},
    {   "CROWN KEEP", 156, 16, true, 0.74f, 0.30f, 0.42f,
        "Cm Cm Ab Bb Cm Fm G G | Ab Bb Eb Cm Fm Db G G", {
        // C minor, harmonic: Cm Cm Ab Bb | Cm Fm G G || Ab Bb Eb Cm | Fm Db G G. Organ hook, then the
        // strings take the tune over a choir while the dragon circles.
        "@PORGAN v80 p-10 e1 m0 "
        "G4:2 C5:2 Eb5:2 D5:1 C5:1 G5:4 F5:2 Eb5:2 | D5:2 Eb5:2 C5:4 B4:2 C5:2 G4:4 | C5:2 Eb5:2 Ab5:4 G5:2 F5:2 Eb5:4 | D5:2 F5:2 Bb5:4 Ab5:2 G5:2 F5:2 D5:2 |"
        "G4:2 C5:2 Eb5:2 D5:1 C5:1 G5:4 Ab5:2 G5:2 | F5:2 Ab5:2 C6:4 Bb5:2 Ab5:2 F5:4 | G5:2 F5:2 Eb5:2 D5:2 B4:4 D5:4 | G5:8 .:2 B4:2 C5:2 D5:2 |"
        "@STRINGS v92 m14 "
        "Eb5:6 C5:2 Ab5:8 | F5:6 D5:2 Bb5:8 | G5:6 F5:2 Eb5:4 Bb4:4 | C5:4 Eb5:4 G5:6 Ab5:1 G5:1 |"
        "Ab5:6 G5:2 F5:4 C5:4 | Db5:4 F5:4 Ab5:6 F5:2 | G5:6 F5:2 D5:4 B4:4 | G4:2 Ab4:2 B4:2 C5:2 D5:2 Eb5:2 F5:2 D5:2 |",
        "@HARP v60 p24 e1 "
        "[ C4 Eb4 G4 C5 Eb5 C5 G4 Eb4 ]4 [ Ab3 C4 Eb4 Ab4 C5 Ab4 Eb4 C4 ]2 [ Bb3 D4 F4 Bb4 D5 Bb4 F4 D4 ]2 "
        "[ C4 Eb4 G4 C5 Eb5 C5 G4 Eb4 ]2 [ F3 Ab3 C4 F4 Ab4 F4 C4 Ab3 ]2 [ G3 B3 D4 G4 B4 G4 D4 B3 ]4 "
        "@CHOIR v64 C5:16 | D5:16 | Bb4:16 | G4:16 | C5:16 | F4:16 | B4:16 | B4:8 D5:8 |",
        "@DBASS v94 p0 e0 "
        "[ C2 . C2 C2 C3 . C2 C2 C2 . C2 C2 C3 . C3 C2 ]2 Ab1 . Ab1 Ab1 Ab2 . Ab1 Ab1 Ab1 . Ab1 Ab1 Ab2 . Ab2 Ab1 | Bb1 . Bb1 Bb1 Bb2 . Bb1 Bb1 Bb1 . Bb1 Bb1 Bb2 . Bb2 Bb1 |"
        "C2 . C2 C2 C3 . C2 C2 C2 . C2 C2 C3 . C3 C2 | F2 . F2 F2 F3 . F2 F2 F2 . F2 F2 F3 . F3 F2 | [ G1 . G1 G1 G2 . G1 G1 G1 . G1 G1 G2 . G2 G1 ]2 "
        "Ab1 . Ab1 Ab1 Ab2 . Ab1 Ab1 Ab1 . Ab1 Ab1 Ab2 . Ab2 Ab1 | Bb1 . Bb1 Bb1 Bb2 . Bb1 Bb1 Bb1 . Bb1 Bb1 Bb2 . Bb2 Bb1 | Eb2 . Eb2 Eb2 Eb3 . Eb2 Eb2 Eb2 . Eb2 Eb2 Eb3 . Eb3 Eb2 | C2 . C2 C2 C3 . C2 C2 C2 . C2 C2 C3 . C3 C2 |"
        "F2 . F2 F2 F3 . F2 F2 F2 . F2 F2 F3 . F3 F2 | Db2 . Db2 Db2 Db3 . Db2 Db2 Db2 . Db2 Db2 Db3 . Db3 Db2 | [ G1 . G1 G1 G2 . G1 G1 G1 . G1 G1 G2 . G2 G1 ]2",
        "v112 p0 e0 [ [ k . . k s . . k k . k . s . . . ]7 k . . k s . . k @TIMPANI v100 G2:2 G2:2 C3:2 G2 G2 v112 ]2",
        "v58 p-22 e0 [ c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7 ] "
        "@ORCHHIT v80 Ab4 . v58 h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7",
    }},
    {   "GULLWATER COVE", 168, 12, true, 0.69f, 0.30f, 0.42f,
        "Am Am G G F G Am E | C G Am E F C Dm E", {
        // A minor jig, 6/8 (12 rows a bar): Am Am G G | F G Am E || C G Am E | F C Dm E. The accordion
        // has the hook, the flute answers; the fiddle and then the horn play the "pah-pah".
        "@ACCORD v84 p-10 e1 m0 "
        "E5:4 A4:2 C5:2 B4:2 A4:2 | E5:4 A5:2 G5:2 E5:2 C5:2 | D5:4 G4:2 B4:2 A4:2 G4:2 | D5:4 G5:2 F5:2 D5:2 B4:2 |"
        "C5:4 F5:2 E5:2 D5:2 C5:2 | B4:4 D5:2 G5:2 F5:2 D5:2 | C5:2 B4:2 A4:2 E5:4 C5:2 | B4:6 G#4:2 A4:2 B4:2 |"
        "@FLUTE v88 m14 "
        "G5:4 E5:2 C5:2 E5:2 G5:2 | B5:4 G5:2 D5:2 G5:2 B5:2 | C6:4 B5:2 A5:2 G5:2 E5:2 | G#5:4 E5:2 B4:2 E5:2 G#5:2 |"
        "A5:4 F5:2 C5:2 F5:2 A5:2 | G5:4 E5:2 C5:2 E5:2 G5:2 | F5:4 E5:2 D5:2 A4:2 D5:2 | E5:6 D5:2 C5:2 B4:2 |",
        "@FIDDLE v50 p22 e1 "
        "[ .:2 C4:2 E4:2 .:2 C4:2 E4:2 ]2 [ .:2 B3:2 D4:2 .:2 B3:2 D4:2 ]2 .:2 A3:2 C4:2 .:2 A3:2 C4:2 | .:2 B3:2 D4:2 .:2 B3:2 D4:2 | .:2 C4:2 E4:2 .:2 C4:2 E4:2 | .:2 G#3:2 B3:2 .:2 G#3:2 B3:2 |"
        "@HORN v56 "
        ".:2 E4:2 G4:2 .:2 E4:2 G4:2 | .:2 D4:2 G4:2 .:2 D4:2 G4:2 | .:2 C4:2 E4:2 .:2 C4:2 E4:2 | .:2 G#3:2 B3:2 .:2 G#3:2 B3:2 |"
        ".:2 A3:2 C4:2 .:2 A3:2 C4:2 | .:2 E4:2 G4:2 .:2 E4:2 G4:2 | .:2 D4:2 F4:2 .:2 D4:2 F4:2 | .:2 G#3:2 B3:2 .:2 G#3:2 B3:2 |",
        "@PBASS v108 p0 e0 "
        "[ A2:4 .:2 E2:4 .:2 ]2 [ G2:4 .:2 D2:4 .:2 ]2 F2:4 .:2 C2:4 .:2 | G2:4 .:2 D2:4 .:2 | A2:4 .:2 E2:4 .:2 | E2:4 .:2 B1:4 .:2 |"
        "C2:4 .:2 G2:4 .:2 | G2:4 .:2 D2:4 .:2 | A2:4 .:2 E2:4 .:2 | E2:4 .:2 B1:4 .:2 | F2:4 .:2 C2:4 .:2 | C2:4 .:2 G2:4 .:2 | D2:4 .:2 A2:4 .:2 | E2:4 .:2 B1:4 .:2 |",
        "v112 p0 e0 [ [ k . . . . . s . . . k . ]7 k . s . k . s . s s s s ]2",
        "v60 p-20 e0 [ c . n . n q n . n . n q [ n . n . n q n . n . n q ]7 ]2",
    }},
    {   "FOSSIL FALLS", 150, 16, true, 0.89f, 0.30f, 0.42f,
        "D C G D D C Bm A | G A F#m Bm Em G A A", {
        // D mixolydian: D C G D | D C Bm A || G A F#m Bm | Em G A A. Marimba hook over pan pipes, then
        // the trumpet strides out over marimba arpeggios; taiko and bongos underneath.
        "@MARIMBA v165 p-8 e1 m0 "
        "D5:2 F#5:2 A5:3 F#5:1 D5:2 E5:2 F#5:4 | E5:2 G5:2 C6:3 B5:1 G5:2 E5:2 C5:4 | D5:2 G5:2 B5:3 A5:1 G5:2 B5:2 D6:4 | C6:2 A5:2 F#5:4 E5:2 D5:2 A4:4 |"
        "D5:2 F#5:2 A5:3 F#5:1 D5:2 E5:2 F#5:4 | G5:2 E5:2 C5:3 E5:1 G5:2 A5:2 G5:4 | F#5:2 D5:2 B4:3 D5:1 F#5:2 B5:2 A5:4 | E5:2 A5:2 C#6:4 B5:2 A5:2 E5:4 |"
        "@TRUMPET v84 m14 "
        "D5:6 B4:2 G4:4 B4:4 | C#5:6 A4:2 E5:8 | F#5:6 E5:2 C#5:4 A4:4 | B4:4 D5:4 F#5:8 |"
        "G5:6 F#5:2 E5:4 B4:4 | D5:4 G5:4 B5:6 A5:2 | A5:8 G5:2 F#5:2 E5:4 | C#5:4 E5:4 A5:4 .:4 |",
        "@PANFLUTE v40 p22 e1 "
        "A4:16 | G4:16 | B4:16 | A4:8 F#4:8 | A4:16 | G4:16 | F#4:16 | E4:8 C#5:8 |"
        "@MARIMBA v56 "
        "[ G3 B3 D4 G4 B4 G4 D4 B3 ]2 [ A3 C#4 E4 A4 C#5 A4 E4 C#4 ]2 [ F#3 A3 C#4 F#4 A4 F#4 C#4 A3 ]2 [ B3 D4 F#4 B4 D5 B4 F#4 D4 ]2 "
        "[ E3 G3 B3 E4 G4 E4 B3 G3 ]2 [ G3 B3 D4 G4 B4 G4 D4 B3 ]2 [ A3 C#4 E4 A4 C#5 A4 E4 C#4 ]4",
        "@ABASS v90 p0 e0 "
        "[ D2:3 D2:1 .:2 D2:2 .:2 D2:2 D3:2 D2:2 ] [ C2:3 C2:1 .:2 C2:2 .:2 C2:2 C3:2 C2:2 ] [ G1:3 G1:1 .:2 G1:2 .:2 G1:2 G2:2 G1:2 ] [ D2:3 D2:1 .:2 D2:2 .:2 D2:2 D3:2 D2:2 ]2 "
        "[ C2:3 C2:1 .:2 C2:2 .:2 C2:2 C3:2 C2:2 ] [ B1:3 B1:1 .:2 B1:2 .:2 B1:2 B2:2 B1:2 ] [ A1:3 A1:1 .:2 A1:2 .:2 A1:2 A2:2 A1:2 ] "
        "[ G1:3 G1:1 .:2 G1:2 .:2 G1:2 G2:2 G1:2 ] [ A1:3 A1:1 .:2 A1:2 .:2 A1:2 A2:2 A1:2 ] [ F#2:3 F#2:1 .:2 F#2:2 .:2 F#2:2 F#3:2 F#2:2 ] [ B1:3 B1:1 .:2 B1:2 .:2 B1:2 B2:2 B1:2 ] "
        "[ E2:3 E2:1 .:2 E2:2 .:2 E2:2 E3:2 E2:2 ] [ G1:3 G1:1 .:2 G1:2 .:2 G1:2 G2:2 G1:2 ] [ A1:3 A1:1 .:2 A1:2 .:2 A1:2 A2:2 A1:2 ]2",
        "v112 p0 e0 [ [ k . j . i . j k k . j . i . j j ]7 i . j j i . j j i . i . j j j j ]2",
        "v64 p-22 e0 [ c . h . b b h . b . h q b b h b [ b . h . b b h . b . h q b b h b ]7 ]2",
    }},
    {   "NEON SPRAWL", 150, 16, true, 0.75f, 0.30f, 0.42f,
        "F#m D A E F#m D Bm C# | D E F#m F#m Bm D E C#", {
        // F# minor synthwave: F#m D A E | F#m D Bm C# || D E F#m F#m | Bm D E C#. A synth lead over a
        // running pulse arpeggio, octave-pumping moog bass, four on the floor with a gated snare.
        "@SYNLEAD v90 p-8 e1 m14 "
        "C#5:2 F#5:2 A5:2 G#5:1 F#5:1 C#5:4 F#5:2 G#5:2 | A5:4 F#5:2 D5:2 F#5:2 A5:2 B5:2 A5:2 | E5:2 A5:2 C#6:4 B5:2 A5:2 E5:4 | G#5:4 E5:2 B4:2 E5:2 G#5:2 B5:4 |"
        "C#5:2 F#5:2 A5:2 G#5:1 F#5:1 C#5:4 F#5:2 G#5:2 | A5:4 B5:2 A5:2 F#5:4 D5:4 | D5:2 F#5:2 B5:4 A5:2 F#5:2 D5:4 | C#5:2 F5:2 G#5:4 F5:2 C#5:2 G#4:4 |"
        "F#5:6 E5:2 D5:4 A4:4 | G#5:6 F#5:2 E5:4 B4:4 | A5:6 G#5:2 F#5:4 C#5:4 | C#6:8 B5:4 A5:4 |"
        "B5:6 A5:2 F#5:4 D5:4 | A5:6 F#5:2 D5:4 F#5:4 | G#5:4 B5:4 E5:4 G#5:4 | F5:4 G#5:4 C#6:4 G#5:2 F5:2 |",
        "@PULSE v36 p24 e1 "
        "[ F#3 A3 C#4 F#4 A4 F#4 C#4 A3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ A3 C#4 E4 A4 C#5 A4 E4 C#4 ]2 [ E3 G#3 B3 E4 G#4 E4 B3 G#3 ]2 "
        "[ F#3 A3 C#4 F#4 A4 F#4 C#4 A3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ B3 D4 F#4 B4 D5 B4 F#4 D4 ]2 [ C#3 F3 G#3 C#4 F4 C#4 G#3 F3 ]2 "
        "[ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ E3 G#3 B3 E4 G#4 E4 B3 G#3 ]2 [ F#3 A3 C#4 F#4 A4 F#4 C#4 A3 ]4 "
        "[ B3 D4 F#4 B4 D5 B4 F#4 D4 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ E3 G#3 B3 E4 G#4 E4 B3 G#3 ]2 [ C#3 F3 G#3 C#4 F4 C#4 G#3 F3 ]2",
        "@MOOG v94 p0 e0 "
        "[ F#2 . F#2 F#3 F#2 . F#2 F#3 ]2 [ D2 . D2 D3 D2 . D2 D3 ]2 [ A1 . A1 A2 A1 . A1 A2 ]2 [ E2 . E2 E3 E2 . E2 E3 ]2 "
        "[ F#2 . F#2 F#3 F#2 . F#2 F#3 ]2 [ D2 . D2 D3 D2 . D2 D3 ]2 [ B1 . B1 B2 B1 . B1 B2 ]2 [ C#2 . C#2 C#3 C#2 . C#2 C#3 ]2 "
        "[ D2 . D2 D3 D2 . D2 D3 ]2 [ E2 . E2 E3 E2 . E2 E3 ]2 [ F#2 . F#2 F#3 F#2 . F#2 F#3 ]4 "
        "[ B1 . B1 B2 B1 . B1 B2 ]2 [ D2 . D2 D3 D2 . D2 D3 ]2 [ E2 . E2 E3 E2 . E2 E3 ]2 [ C#2 . C#2 C#3 C#2 . C#2 C#3 ]2",
        "v112 p0 e0 [ [ k . . . p - - . k . . k p - - . ]7 k . . . p - - . k . p - p - p p ]2",
        "v54 p-20 e0 [ c h o h h h o h h h o h h h o h [ h h o h h h o h h h o h h x o h ]7 ]2",
    }},
    {   "RESULTS", 120, 16, true, 0.90f, 0.30f, 0.42f,
        "C F G C", {
        "@BRASS v127 p-8 e1 m12 C4:2 E4:2 G4:2 C5:2 B4:2 G4:2 E4:2 G4:2 | A4:2 C5:2 F4:2 A4:2 C5:3 B4:1 A4:2 F4:2 | G4:2 B4:2 D5:2 B4:2 G4:2 F4:2 D4:2 F4:2 | E4:2 G4:2 C5:6 .:2 G4:2 B4:2 |",
        "@STRINGS v60 p20 e1 E4:16 | F4:16 | G4:16 | E4:16 |",
        "@SLAP v90 p0 e0 C2:4 G2:4 C3:4 G2:4 | F2:4 C3:4 F3:4 C3:4 | G2:4 D3:4 G3:4 D3:4 | C2:4 G2:4 C3:8 |",
        "v92 p0 e0 [ k . . . s . . . k . . . s . . . ]3 k . . . s . . . k . s . s s s s",
        "v46 p-20 e1 c . h . h . h . h . h . h . h . [ h . h . h . h . h . h . h . h . ]3",
    }},
    {   "MENU", 104, 16, true, 0.85f, 0.34f, 0.45f,
        "C Am F G C Am F G | F G Em Am F G C C,G", {
        // C major, laid back: vibes over an off-beat electric piano and a slap bass that leaves room.
        "@VIBES v100 p-6 e1 "
        "E5:3 G5:3 E5:2 .:2 G4:2 C5:2 E5:2 | E5:3 A5:3 E5:2 .:2 A4:2 C5:2 E5:2 | A5:3 F5:3 C5:2 .:2 A4:2 C5:2 F5:2 | G5:6 D5:2 B4:4 D5:4 |"
        "E5:3 G5:3 E5:2 .:2 G4:2 C5:2 E5:2 | E5:3 A5:3 v80 C6:2 v100 .:2 A5:2 G5:2 E5:2 | F5:3 A5:3 F5:2 .:2 C5:2 F5:2 A5:2 | B5:4 G5:4 D5:8 |"
        "C5:2 F5:2 A5:4 G5:2 F5:2 C5:4 | D5:2 G5:2 B5:4 A5:2 G5:2 D5:4 | E5:2 G5:2 B5:4 G5:2 E5:2 B4:4 | C5:2 E5:2 A5:6 .:2 E5:2 C5:2 |"
        "C5:2 F5:2 A5:4 G5:2 F5:2 C5:4 | D5:2 G5:2 B5:4 A5:2 G5:2 D5:4 | E5:2 G5:2 v80 C6:4 v100 G5:2 E5:2 C5:4 | C5:8 .:4 B4:2 D5:2 |",
        "@EPIANO v58 p22 e1 "
        ".:4 E4:2 G4:2 .:4 E4:2 G4:2 | .:4 E4:2 A4:2 .:4 E4:2 A4:2 | .:4 F4:2 A4:2 .:4 F4:2 A4:2 | .:4 D4:2 G4:2 .:4 D4:2 G4:2 |"
        ".:4 E4:2 G4:2 .:4 E4:2 G4:2 | .:4 E4:2 A4:2 .:4 E4:2 A4:2 | .:4 F4:2 A4:2 .:4 F4:2 A4:2 | .:4 D4:2 G4:2 .:4 D4:2 G4:2 |"
        ".:4 F4:2 A4:2 .:4 F4:2 A4:2 | .:4 D4:2 G4:2 .:4 D4:2 G4:2 | .:4 E4:2 G4:2 .:4 E4:2 G4:2 | .:4 E4:2 A4:2 .:4 E4:2 A4:2 |"
        ".:4 F4:2 A4:2 .:4 F4:2 A4:2 | .:4 D4:2 G4:2 .:4 D4:2 G4:2 | .:4 E4:2 G4:2 .:4 E4:2 G4:2 | .:4 E4:2 G4:2 .:4 D4:2 G4:2 |",
        "@SLAP v96 p0 e0 "
        "C2:3 C2:1 .:2 C3:2 .:2 G2:2 C3:2 G2:2 | A1:3 A1:1 .:2 A2:2 .:2 E2:2 A2:2 E2:2 | F2:3 F2:1 .:2 F3:2 .:2 C3:2 F3:2 C3:2 | G1:3 G1:1 .:2 G2:2 .:2 D2:2 G2:2 D2:2 |"
        "C2:3 C2:1 .:2 C3:2 .:2 G2:2 C3:2 G2:2 | A1:3 A1:1 .:2 A2:2 .:2 E2:2 A2:2 E2:2 | F2:3 F2:1 .:2 F3:2 .:2 C3:2 F3:2 C3:2 | G1:3 G1:1 .:2 G2:2 .:2 D2:2 G2:2 D2:2 |"
        "F2:3 F2:1 .:2 F3:2 .:2 C3:2 F3:2 C3:2 | G1:3 G1:1 .:2 G2:2 .:2 D2:2 G2:2 D2:2 | E2:3 E2:1 .:2 E3:2 .:2 B2:2 E3:2 B2:2 | A1:3 A1:1 .:2 A2:2 .:2 E2:2 A2:2 E2:2 |"
        "F2:3 F2:1 .:2 F3:2 .:2 C3:2 F3:2 C3:2 | G1:3 G1:1 .:2 G2:2 .:2 D2:2 G2:2 D2:2 | C2:3 C2:1 .:2 C3:2 .:2 G2:2 C3:2 G2:2 | C2:3 C2:1 .:2 C3:2 G1:4 G2:2 D2:2 |",
        "v96 p0 e0 [ k . . . s . . k . . k . s . . . ]15 k . . . s . . k . . k . s . s s",
        "v46 p-20 e0 [ h . h . h . o . h . h . h . o . ]16",
    }},
    {   "FINISH", 150, 16, false, 0.79f, 0.34f, 0.45f,
        "G:10 C:6 D:8 G:12", {
        // The chequered flag: a rising G major trumpet fanfare with the brass a third below, played once.
        "@TRUMPET v112 p-6 e1 m14 D4:2 G4:2 B4:2 D5:4 E5:2 C5:4 | A4:2 D5:2 F#5:4 G5:12",
        "@BRASS v110 p18 e1 B3:2 D4:2 G4:2 B4:4 C5:2 G4:4 | F#4:2 A4:2 D5:4 B4:12",
        "@SLAP v96 p0 e0 G2:4 G2:2 G2:4 C3:2 C3:4 | D3:4 D3:2 D2:2 G2:12",
        "v100 p0 e0 k . . . s . k . k . s . s . s s | k . s . s s s s k:12",
        "v60 p-20 e0 c:8 h . h . h . h . | h . h . o:4 c:12",
    }},
    {   "TROPHY", 126, 16, true, 0.76f, 0.32f, 0.45f,
        "D G A D Bm G Em A | G A F#m Bm G A D D,A", {
        // D major, a march for the podium: trumpet over strings, snare ruffs, a bass that strides.
        "@TRUMPET v104 p-6 e1 m14 "
        "D5:6 A4:2 D5:4 F#5:4 | G5:6 D5:2 B4:4 D5:4 | E5:6 C#5:2 E5:4 A5:4 | F#5:8 D5:6 A4:2 |"
        "B4:2 D5:2 F#5:4 B5:4 F#5:4 | G5:4 B5:2 G5:2 D5:4 G5:4 | E5:2 G5:2 B5:4 G5:4 E5:4 | A5:6 E5:2 C#5:4 E5:4 |"
        "D5:2 G5:2 B5:4 A5:2 G5:2 D5:4 | C#5:2 E5:2 A5:4 G5:2 E5:2 C#5:4 | C#5:2 F#5:2 A5:4 F#5:2 C#5:2 A4:4 | B4:2 D5:2 F#5:6 .:2 D5:2 F#5:2 |"
        "G5:4 B5:4 G5:4 D5:4 | A5:4 E5:4 C#5:4 E5:4 | D5:2 F#5:2 A5:4 D5:2 F#5:2 A5:4 | D5:8 .:2 A4:2 C#5:2 E5:2 |",
        "@STRINGS v56 p18 e1 "
        "F#4:16 | B4:16 | E4:16 | A4:16 | D4:16 | B3:16 | G4:16 | E4:16 |"
        "D4:16 | E4:16 | A4:16 | D4:16 | B3:16 | C#4:16 | F#4:16 | F#4:8 E4:8 |",
        "@SLAP v92 p0 e0 "
        "D2:2 D2:2 A2:2 D3:2 D2:2 D2:2 A2:2 D3:2 | G1:2 G1:2 D2:2 G2:2 G1:2 G1:2 D2:2 G2:2 | A1:2 A1:2 E2:2 A2:2 A1:2 A1:2 E2:2 A2:2 | D2:2 D2:2 A2:2 D3:2 D2:2 D2:2 A2:2 D3:2 |"
        "B1:2 B1:2 F#2:2 B2:2 B1:2 B1:2 F#2:2 B2:2 | G1:2 G1:2 D2:2 G2:2 G1:2 G1:2 D2:2 G2:2 | E2:2 E2:2 B2:2 E3:2 E2:2 E2:2 B2:2 E3:2 | A1:2 A1:2 E2:2 A2:2 A1:2 A1:2 E2:2 A2:2 |"
        "G1:2 G1:2 D2:2 G2:2 G1:2 G1:2 D2:2 G2:2 | A1:2 A1:2 E2:2 A2:2 A1:2 A1:2 E2:2 A2:2 | F#2:2 F#2:2 C#3:2 F#3:2 F#2:2 F#2:2 C#3:2 F#3:2 | B1:2 B1:2 F#2:2 B2:2 B1:2 B1:2 F#2:2 B2:2 |"
        "G1:2 G1:2 D2:2 G2:2 G1:2 G1:2 D2:2 G2:2 | A1:2 A1:2 E2:2 A2:2 A1:2 A1:2 E2:2 A2:2 | D2:2 D2:2 A2:2 D3:2 D2:2 D2:2 A2:2 D3:2 | D2:2 D2:2 A2:2 D3:2 A1:2 A1:2 E2:2 A2:2 |",
        "v96 p0 e0 [ k . . . s . s s k . k . s . . . ]7 k . . . s . s s k . s . s s t t [ k . . . s . s s k . k . s . . . ]7 k . s . k . s . s s s s t t t t",
        "v54 p-20 e0 [ c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7 ]2",
    }},
    {   "BREAKFAST RUN", 158, 16, true, 0.69f, 0.28f, 0.40f,
        "A D A E A D Bm,E A | F#m D A E F#m D Bm E | A D A E A D Bm,E A | F#m D A E F#m D Bm E", {
        // A major, Saturday-morning ska: the lead bounces over off-beat piano chops and a walking slap
        // bass; a square wave whistles the middle strain in half time, then the lead answers it at speed.
        "@LEAD v84 p-8 e1 m16 "
        "E5:3 C#5:1 E5:2 A5:4 .:2 E5:2 F#5:2 | A5:3 F#5:1 A5:2 D5:4 .:2 F#5:2 A5:2 | E5:3 C#5:1 E5:2 A5:4 .:2 C#5:2 E5:2 | B5:4 G#5:2 E5:2 B4:6 .:2 | "
        "E5:3 C#5:1 E5:2 A5:4 .:2 E5:2 F#5:2 | F#5:3 D5:1 F#5:2 A5:4 .:2 F#5:2 D5:2 | B4:2 D5:2 F#5:2 B5:2 G#5:2 E5:2 B4:2 E5:2 | A5:6 E5:2 C#5:2 E5:2 A4:4 | "
        "@SQUARE v84 p-6 m20 "
        "C#5:4 F#5:4 A5:6 F#5:2 | A5:4 F#5:4 D5:6 F#5:2 | E5:4 A5:2 E5:2 C#5:4 E5:4 | B4:4 E5:4 G#5:6 .:2 | C#5:4 F#5:4 A5:6 F#5:2 | "
        "A5:4 F#5:4 D5:4 F#5:2 A5:2 | B5:4 F#5:4 D5:4 F#5:4 | E5:2 G#5:2 B5:4 G#5:2 E5:2 B4:4 | "
        "@LEAD v84 p-8 m16 "
        "E5:3 C#5:1 E5:2 A5:4 .:2 E5:2 F#5:2 | A5:3 F#5:1 A5:2 D5:4 .:2 F#5:2 A5:2 | E5:3 C#5:1 E5:2 A5:4 .:2 C#5:2 E5:2 | B5:4 G#5:2 E5:2 B4:6 .:2 | "
        "E5:3 C#5:1 E5:2 A5:4 .:2 E5:2 F#5:2 | F#5:3 D5:1 F#5:2 A5:4 .:2 F#5:2 D5:2 | B4:2 D5:2 F#5:2 B5:2 G#5:2 E5:2 B4:2 E5:2 | A5:6 E5:2 C#5:2 E5:2 A4:4 | "
        "F#5:2 A5:2 C#5:4 F#5:2 A5:2 F#5:4 | F#5:2 A5:2 D5:4 F#5:2 A5:2 F#5:4 | E5:2 A5:2 C#5:4 E5:2 A5:2 E5:4 | E5:2 G#5:2 B4:4 E5:2 G#5:2 B5:4 | "
        "F#5:2 A5:2 C#5:4 F#5:2 A5:2 F#5:4 | F#5:2 A5:2 D5:4 F#5:2 A5:2 F#5:4 | B4:2 D5:2 F#5:2 B5:2 F#5:2 D5:2 B4:2 D5:2 | E5:2 G#5:2 B5:2 G#5:2 E5:4 .:4 | ",
        "@EPIANO v62 p22 e1 "
        ".:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | .:2 D4:2 .:2 F#4:2 .:2 D4:2 .:2 F#4:2 | .:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | "
        ".:2 E4:2 .:2 G#4:2 .:2 E4:2 .:2 G#4:2 | .:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | .:2 D4:2 .:2 F#4:2 .:2 D4:2 .:2 F#4:2 | "
        ".:2 D4:2 .:2 F#4:2 .:2 E4:2 .:2 G#4:2 | .:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | "
        "v56 "
        "C#4:8 F#4:8 | D4:8 F#4:8 | E4:8 C#4:8 | E4:8 G#4:8 | C#4:8 F#4:8 | D4:8 F#4:8 | D4:8 F#4:8 | E4:8 G#4:8 | "
        "v62 "
        ".:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | .:2 D4:2 .:2 F#4:2 .:2 D4:2 .:2 F#4:2 | .:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | "
        ".:2 E4:2 .:2 G#4:2 .:2 E4:2 .:2 G#4:2 | .:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | .:2 D4:2 .:2 F#4:2 .:2 D4:2 .:2 F#4:2 | "
        ".:2 D4:2 .:2 F#4:2 .:2 E4:2 .:2 G#4:2 | .:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | "
        ".:2 C#4:2 .:2 F#4:2 .:2 C#4:2 .:2 F#4:2 | .:2 D4:2 .:2 F#4:2 .:2 D4:2 .:2 F#4:2 | .:2 C#4:2 .:2 E4:2 .:2 C#4:2 .:2 E4:2 | "
        ".:2 E4:2 .:2 G#4:2 .:2 E4:2 .:2 G#4:2 | .:2 C#4:2 .:2 F#4:2 .:2 C#4:2 .:2 F#4:2 | .:2 D4:2 .:2 F#4:2 .:2 D4:2 .:2 F#4:2 | "
        ".:2 D4:2 .:2 F#4:2 .:2 D4:2 .:2 F#4:2 | .:2 E4:2 .:2 G#4:2 .:2 E4:2 .:2 G#4:2 | ",
        "@SLAP v100 p0 e0 "
        "A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | D2:2 F#2:2 A2:2 F#2:2 D2:2 F#2:2 A2:2 F#2:2 | A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | "
        "E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | D2:2 F#2:2 A2:2 F#2:2 D2:2 F#2:2 A2:2 F#2:2 | "
        "B1:2 D2:2 F#2:2 D2:2 E2:2 G#2:2 B2:2 G#2:2 | A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | "
        "F#2:4 .:2 F#2:2 C#3:4 F#2:2 C#3:2 | D2:4 .:2 D2:2 A2:4 D2:2 A2:2 | A2:4 .:2 A2:2 E3:4 A2:2 E3:2 | E2:4 .:2 E2:2 B2:4 E2:2 B2:2 | "
        "F#2:4 .:2 F#2:2 C#3:4 F#2:2 C#3:2 | D2:4 .:2 D2:2 A2:4 D2:2 A2:2 | B1:4 .:2 B1:2 F#2:4 B1:2 F#2:2 | E2:4 .:2 E2:2 B2:4 E2:2 B2:2 | "
        "A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | D2:2 F#2:2 A2:2 F#2:2 D2:2 F#2:2 A2:2 F#2:2 | A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | "
        "E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | D2:2 F#2:2 A2:2 F#2:2 D2:2 F#2:2 A2:2 F#2:2 | "
        "B1:2 D2:2 F#2:2 D2:2 E2:2 G#2:2 B2:2 G#2:2 | A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | "
        "F#2:2 A2:2 C#3:2 A2:2 F#2:2 A2:2 C#3:2 A2:2 | D2:2 F#2:2 A2:2 F#2:2 D2:2 F#2:2 A2:2 F#2:2 | A2:2 C#3:2 E3:2 C#3:2 A2:2 C#3:2 E3:2 C#3:2 | "
        "E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | F#2:2 A2:2 C#3:2 A2:2 F#2:2 A2:2 C#3:2 A2:2 | D2:2 F#2:2 A2:2 F#2:2 D2:2 F#2:2 A2:2 F#2:2 | "
        "B1:2 D2:2 F#2:2 D2:2 B1:2 D2:2 F#2:2 D2:2 | E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | ",
        "v108 p0 e0 [ k . . . s . . . k . k . s . . . ]7 k . . . s . . . k . s . s s t t [ k . . . . . k . s . . . . . k . ]7 k . . . . . k . s . s . s s t t [ k . . . s . . . k . k . s . . . ]7 k . . . s . . . k . s . s s t t [ k . . . s . . . k . k . s . . . ]7 k . s . k . s . s s s s t t t t",
        "v56 p-22 e0 c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7 c . . . h . h . h . . . h . o . [ h . . . h . h . h . . . h . o . ]7 c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7 c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7",
    }},
    {   "SEWER", 170, 16, true, 0.64f, 0.40f, 0.50f,
        "Am Am F E Am Am Dm E | C G F E C G F E | Am Am F E Am Am Dm E | C G F E C G F E", {
        // A minor surf rock: a twangy guitar, tremolo-picked and drenched in echo, over a surf organ, a walking
        // picked bass and a surf beat; the middle strain lifts to C major in long notes, and comes back the second
        // time as tremolo runs with a snare roll into the wave.
        "@GUITAR v64 p-8 e1 m14 "
        "A4 A4 A4 A4 C5 C5 C5 C5 E5:4 D5:2 C5:2 | E5:2 A5:2 G5:2 E5:2 C5:4 A4:4 | A4 A4 A4 A4 C5 C5 C5 C5 F5:4 E5:2 C5:2 | B4:2 E5:2 G#5:4 E5:2 B4:2 G#4:4 | "
        "A4 A4 A4 A4 C5 C5 C5 C5 E5:4 D5:2 C5:2 | E5:2 A5:2 G5:2 E5:2 A5:4 E5:4 | D5 D5 D5 D5 F5 F5 F5 F5 A5:4 F5:2 D5:2 | E5:2 G#5:2 B5:4 G#5:2 E5:2 B4:4 | "
        "m18 "
        "E5:6 G5:2 C5:4 E5:4 | D5:6 G5:2 B4:4 D5:4 | C5:6 F5:2 A4:4 C5:4 | B4:4 E5:4 G#5:6 .:2 | E5:6 G5:2 C6:4 G5:4 | D5:6 G5:2 B5:4 G5:4 | "
        "A5:4 F5:4 C5:4 F5:4 | E5 E5 E5 E5 G#5 G#5 G#5 G#5 B5 B5 B5 B5 G#5 G#5 E5 E5 | "
        "m14 "
        "A4 A4 A4 A4 C5 C5 C5 C5 E5:4 D5:2 C5:2 | E5:2 A5:2 G5:2 E5:2 C5:4 A4:4 | A4 A4 A4 A4 C5 C5 C5 C5 F5:4 E5:2 C5:2 | B4:2 E5:2 G#5:4 E5:2 B4:2 G#4:4 | "
        "A4 A4 A4 A4 C5 C5 C5 C5 E5:4 D5:2 C5:2 | E5:2 A5:2 G5:2 E5:2 A5:4 E5:4 | D5 D5 D5 D5 F5 F5 F5 F5 A5:4 F5:2 D5:2 | E5:2 G#5:2 B5:4 G#5:2 E5:2 B4:4 | "
        "C5 C5 C5 C5 E5 E5 E5 E5 G5 G5 G5 G5 E5 E5 E5 E5 | B4 B4 B4 B4 D5 D5 D5 D5 G5 G5 G5 G5 D5 D5 D5 D5 | A4 A4 A4 A4 C5 C5 C5 C5 F5 F5 F5 F5 C5 C5 C5 C5 | "
        "G#4 G#4 G#4 G#4 B4 B4 B4 B4 E5 E5 E5 E5 B4 B4 B4 B4 | C5 C5 C5 C5 E5 E5 E5 E5 G5 G5 G5 G5 E5 E5 E5 E5 | "
        "D5 D5 D5 D5 G5 G5 G5 G5 B5 B5 B5 B5 G5 G5 G5 G5 | C5 C5 C5 C5 F5 F5 F5 F5 A5 A5 A5 A5 F5 F5 F5 F5 | "
        "E5 E5 E5 E5 G#5 G#5 G#5 G#5 B5 B5 B5 B5 G#5 G#5 E5 E5 | ",
        "@PORGAN v40 p20 e1 "
        "E4:16 | C4:16 | C4:16 | B3:16 | E4:16 | C4:16 | F4:16 | G#4:16 | G4:16 | D4:16 | F4:16 | E4:16 | G4:16 | D4:16 | F4:16 | G#4:16 | E4:16 | C4:16 | "
        "C4:16 | B3:16 | E4:16 | C4:16 | F4:16 | G#4:16 | G4:16 | D4:16 | F4:16 | E4:16 | G4:16 | D4:16 | F4:16 | G#4:16 | ",
        "@PBASS v104 p0 e0 "
        "A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | F2:2 A2:2 C3:2 A2:2 F2:2 A2:2 C3:2 A2:2 | "
        "E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | "
        "D2:2 F2:2 A2:2 F2:2 D2:2 F2:2 A2:2 F2:2 | E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | C2:4 .:2 C2:2 G2:4 C2:2 G2:2 | G2:4 .:2 G2:2 D3:4 G2:2 D3:2 | "
        "F2:4 .:2 F2:2 C3:4 F2:2 C3:2 | E2:4 .:2 E2:2 B2:4 E2:2 B2:2 | C2:4 .:2 C2:2 G2:4 C2:2 G2:2 | G2:4 .:2 G2:2 D3:4 G2:2 D3:2 | "
        "F2:4 .:2 F2:2 C3:4 F2:2 C3:2 | E2:4 .:2 E2:2 B2:4 E2:2 B2:2 | A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | "
        "F2:2 A2:2 C3:2 A2:2 F2:2 A2:2 C3:2 A2:2 | E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | "
        "A2:2 C3:2 E3:2 C3:2 A2:2 C3:2 E3:2 C3:2 | D2:2 F2:2 A2:2 F2:2 D2:2 F2:2 A2:2 F2:2 | E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | "
        "C3:2 E3:2 G3:2 E3:2 C3:2 E3:2 G3:2 E3:2 | G2:2 B2:2 D3:2 B2:2 G2:2 B2:2 D3:2 B2:2 | F2:2 A2:2 C3:2 A2:2 F2:2 A2:2 C3:2 A2:2 | "
        "E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | C3:2 E3:2 G3:2 E3:2 C3:2 E3:2 G3:2 E3:2 | G2:2 B2:2 D3:2 B2:2 G2:2 B2:2 D3:2 B2:2 | "
        "F2:2 A2:2 C3:2 A2:2 F2:2 A2:2 C3:2 A2:2 | E2:2 G#2:2 B2:2 G#2:2 E2:2 G#2:2 B2:2 G#2:2 | ",
        "v110 p0 e0 [ k . . . s . s . k . k . s . . . ]7 k . s . s s s s s s s s t t t t [ k . . . . . k . s . . . . . k . ]7 k . . . . . k . s . s . s s t t [ k . . . s . s . k . k . s . . . ]7 k . s . s s s s s s s s t t t t [ k . . . s . s . k . k . s . . . ]7 s s s s s s s s s s s s t t t t",
        "v56 p-22 e0 [ c . h . h . h . h . h . h . o . [ h . h . h . h . h . h . h . o . ]7 ]4",
    }},
    {   "CIRCUIT", 150, 16, true, 0.71f, 0.24f, 0.35f,
        "G Em C D G Em C D | C D Bm Em C D G D | G Em C D G Em C D | C D Bm Em C D G D", {
        // G major, a home computer's idea of a racing tune: a pulse-wave lead with a touch of portamento over
        // square-wave arpeggios that stand in for the chords, an octave synth bass and a dry electronic kit.
        "@PULSE v100 p-8 e1 m16 g4 "
        "D5:2 G5:2 B5:2 G5:1 D5:1 G5:4 .:2 B4:2 | E5:2 G5:2 B5:2 G5:1 E5:1 G5:4 .:2 E5:2 | E5:2 G5:2 C6:2 G5:1 E5:1 G5:4 .:2 E5:2 | "
        "F#5:4 D5:2 A4:2 D5:4 F#5:2 A5:2 | D5:2 G5:2 B5:2 G5:1 D5:1 G5:4 .:2 B4:2 | E5:2 G5:2 B5:2 G5:1 E5:1 B4:4 .:2 E5:2 | "
        "C5:2 E5:2 G5:2 C6:2 G5:2 E5:2 C5:2 E5:2 | D5:2 F#5:2 A5:4 F#5:2 D5:2 A4:4 | "
        "g8 m20 "
        "G5:6 E5:2 C5:4 E5:4 | A5:6 F#5:2 D5:4 F#5:4 | F#5:6 D5:2 B4:4 D5:4 | G5:4 E5:4 B4:6 .:2 | G5:6 E5:2 C5:4 G5:4 | A5:6 F#5:2 D5:4 A5:4 | "
        "B5:4 G5:4 D5:4 G5:4 | A5:2 F#5:2 D5:2 F#5:2 A5:4 .:4 | "
        "g4 m16 "
        "D5:2 G5:2 B5:2 G5:1 D5:1 G5:4 .:2 B4:2 | E5:2 G5:2 B5:2 G5:1 E5:1 G5:4 .:2 E5:2 | E5:2 G5:2 C6:2 G5:1 E5:1 G5:4 .:2 E5:2 | "
        "F#5:4 D5:2 A4:2 D5:4 F#5:2 A5:2 | D5:2 G5:2 B5:2 G5:1 D5:1 G5:4 .:2 B4:2 | E5:2 G5:2 B5:2 G5:1 E5:1 B4:4 .:2 E5:2 | "
        "C5:2 E5:2 G5:2 C6:2 G5:2 E5:2 C5:2 E5:2 | D5:2 F#5:2 A5:4 F#5:2 D5:2 A4:4 | "
        "C5:2 E5:2 G5:2 E5:2 C5:2 E5:2 G5:4 | D5:2 F#5:2 A5:2 F#5:2 D5:2 F#5:2 A5:4 | B4:2 D5:2 F#5:2 D5:2 B4:2 D5:2 F#5:4 | "
        "B4:2 E5:2 G5:2 E5:2 B4:2 E5:2 G5:4 | C5:2 E5:2 G5:2 E5:2 C5:2 E5:2 G5:4 | D5:2 F#5:2 A5:2 F#5:2 D5:2 F#5:2 A5:4 | "
        "D5:2 G5:2 B5:2 G5:2 D5:2 G5:2 B5:4 | D5:2 F#5:2 A5:2 F#5:2 D5:4 .:4 | ",
        "@SQUARE v34 p24 e1 "
        "[ G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 ] [ E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 ] [ E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ B3 D4 F#4 B4 F#4 D4 B3 D4 F#4 B4 F#4 D4 B3 D4 F#4 B4 ] [ E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] "
        "[ G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 ] [ E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 ] [ E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ B3 D4 F#4 B4 F#4 D4 B3 D4 F#4 B4 F#4 D4 B3 D4 F#4 B4 ] [ E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 B3 G3 E3 G3 B3 E4 ] [ C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 G4 E4 C4 E4 G4 C5 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] [ G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 D4 B3 G3 B3 D4 G4 ] [ D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 A4 F#4 D4 F#4 A4 D5 ] ",
        "@SYNBASS v94 p0 e0 "
        "[ G2:2 G3:2 ]4 [ E2:2 E3:2 ]4 [ C2:2 C3:2 ]4 [ D2:2 D3:2 ]4 [ G2:2 G3:2 ]4 [ E2:2 E3:2 ]4 [ C2:2 C3:2 ]4 [ D2:2 D3:2 ]4 "
        "C2:3 C2:3 C3:2 C2:3 C2:3 C3:2 | D2:3 D2:3 D3:2 D2:3 D2:3 D3:2 | B2:3 B2:3 B3:2 B2:3 B2:3 B3:2 | E2:3 E2:3 E3:2 E2:3 E2:3 E3:2 | "
        "C2:3 C2:3 C3:2 C2:3 C2:3 C3:2 | D2:3 D2:3 D3:2 D2:3 D2:3 D3:2 | G2:3 G2:3 G3:2 G2:3 G2:3 G3:2 | D2:3 D2:3 D3:2 D2:3 D2:3 D3:2 | "
        "[ G2:2 G3:2 ]4 [ E2:2 E3:2 ]4 [ C2:2 C3:2 ]4 [ D2:2 D3:2 ]4 [ G2:2 G3:2 ]4 [ E2:2 E3:2 ]4 [ C2:2 C3:2 ]4 [ D2:2 D3:2 ]4 [ C2:2 C3:2 ]4 [ D2:2 D3:2 ]4 [ B2:2 B3:2 ]4 [ E2:2 E3:2 ]4 [ C2:2 C3:2 ]4 [ D2:2 D3:2 ]4 [ G2:2 G3:2 ]4 [ D2:2 D3:2 ]4 ",
        "v108 p0 e0 [ k . . k x . . . k . k . x . . . ]7 k . . k x . . . k . x . y y y y [ k . . . x . . . k . . . x . k . ]7 k . . . x . . . y y y y x x x x [ k . . k x . . . k . k . x . . . ]7 k . . k x . . . k . x . y y y y [ k . . k x . . . k . k . x . . . ]7 k . x . k . x . x x x x y y y y",
        "v50 p-22 e0 [ c h o h h h o h h h o h h h o h [ h h o h h h o h h h o h h h o h ]7 ]4",
    }},
    {   "SPACE", 144, 16, true, 0.71f, 0.44f, 0.55f,
        "Bb Gm Eb F Bb Gm Ab F | Eb Ebm Bb Gm Eb Ebm F F | Bb Gm Eb F Bb Gm Ab F | Eb Ebm Bb Gm Eb Ebm F F", {
        // Bb major with a borrowed Ab and Eb minor: a singing lead in long phrases over a strings pad, an octave
        // bass and big tom fills; the vibraphone's arpeggios take the cymbal channel, so there are no hats.
        "@SYNLEAD v96 p-6 e1 m18 g6 "
        "D5:8 F5:4 Bb5:4 | Bb5:2 G5:6 D5:8 | Eb5:8 G5:4 Bb5:4 | A5:8 F5:4 C5:4 | D5:8 F5:4 Bb5:4 | G5:2 Bb5:6 G5:8 | Ab5:8 Eb5:4 C5:4 | C5:4 F5:4 A5:6 .:2 | "
        "G5:6 Bb5:2 Eb5:8 | Gb5:6 Bb5:2 Eb5:8 | F5:6 D5:2 Bb4:8 | D5:4 G5:4 Bb5:6 .:2 | G5:6 Bb5:2 Eb5:4 G5:4 | Gb5:6 Bb5:2 Eb5:4 Gb5:4 | A5:8 F5:4 C5:4 | "
        "F5:4 A5:4 C6:4 .:4 | D5:8 F5:4 Bb5:4 | Bb5:2 G5:6 D5:8 | Eb5:8 G5:4 Bb5:4 | A5:8 F5:4 C5:4 | D5:8 F5:4 Bb5:4 | G5:2 Bb5:6 G5:8 | Ab5:8 Eb5:4 C5:4 | "
        "C5:4 F5:4 A5:6 .:2 | G5:6 Bb5:2 Eb5:8 | Gb5:6 Bb5:2 Eb5:8 | F5:6 D5:2 Bb4:8 | D5:4 G5:4 Bb5:6 .:2 | G5:6 Bb5:2 Eb5:4 G5:4 | Gb5:6 Bb5:2 Eb5:4 Gb5:4 | "
        "C5:4 F5:4 A5:8 | A5:6 F5:2 C5:4 .:4 | ",
        "@STRINGS v52 p18 e1 "
        "F4:16 | D4:16 | G4:16 | C4:16 | F4:16 | D4:16 | Eb4:16 | C4:16 | G4:16 | Gb4:16 | F4:16 | D4:16 | G4:16 | Gb4:16 | A4:16 | C4:16 | F4:16 | D4:16 | "
        "G4:16 | C4:16 | F4:16 | D4:16 | Eb4:16 | C4:16 | G4:16 | Gb4:16 | F4:16 | D4:16 | G4:16 | Gb4:16 | A4:16 | C4:16 | ",
        "@MOOG v92 p0 e0 "
        "[ Bb1:2 Bb2:2 ]4 [ G1:2 G2:2 ]4 [ Eb2:2 Eb3:2 ]4 [ F2:2 F3:2 ]4 [ Bb1:2 Bb2:2 ]4 [ G1:2 G2:2 ]4 [ Ab1:2 Ab2:2 ]4 [ F2:2 F3:2 ]4 "
        "Eb2:6 Eb2:2 Eb3:4 Eb2:4 | Eb2:6 Eb2:2 Eb3:4 Eb2:4 | Bb1:6 Bb1:2 Bb2:4 Bb1:4 | G1:6 G1:2 G2:4 G1:4 | Eb2:6 Eb2:2 Eb3:4 Eb2:4 | "
        "Eb2:6 Eb2:2 Eb3:4 Eb2:4 | F2:6 F2:2 F3:4 F2:4 | F2:6 F2:2 F3:4 F2:4 | "
        "[ Bb1:2 Bb2:2 ]4 [ G1:2 G2:2 ]4 [ Eb2:2 Eb3:2 ]4 [ F2:2 F3:2 ]4 [ Bb1:2 Bb2:2 ]4 [ G1:2 G2:2 ]4 [ Ab1:2 Ab2:2 ]4 [ F2:2 F3:2 ]4 [ Eb2:2 Eb3:2 ]4 [ Eb2:2 Eb3:2 ]4 [ Bb1:2 Bb2:2 ]4 [ G1:2 G2:2 ]4 [ Eb2:2 Eb3:2 ]4 [ Eb2:2 Eb3:2 ]4 [ F2:2 F3:2 ]4 [ F2:2 F3:2 ]4 ",
        "v104 p0 e0 [ k . . . s . . . k . k . s . . . ]3 k . . . s . . . k . t t y y y . [ k . . . s . . . k . k . s . . . ]3 k . . . s . . . t t t t y y y y [ k . . . . . . . s . . . . . k . ]3 k . . . . . . . s . t t y y y . [ k . . . . . . . s . . . . . k . ]3 k . . . s . . . t t t t y y y y [ k . . . s . . . k . k . s . . . ]3 k . . . s . . . k . t t y y y . [ k . . . s . . . k . k . s . . . ]3 k . . . s . . . t t t t y y y y [ k . . . s . . . k . k . s . . . ]3 k . . . s . . . k . t t y y y . [ k . . . s . . . k . k . s . . . ]2 k . . . s . t t k . . . s . y y k . t t t t t t y y y y s s s s",
        "@VIBES v40 p-26 e1 "
        "Bb3:2 D4:2 F4:2 Bb4:2 F4:2 D4:2 Bb3:2 D4:2 | G3:2 Bb3:2 D4:2 G4:2 D4:2 Bb3:2 G3:2 Bb3:2 | Eb4:2 G4:2 Bb4:2 Eb5:2 Bb4:2 G4:2 Eb4:2 G4:2 | "
        "F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | Bb3:2 D4:2 F4:2 Bb4:2 F4:2 D4:2 Bb3:2 D4:2 | G3:2 Bb3:2 D4:2 G4:2 D4:2 Bb3:2 G3:2 Bb3:2 | "
        "Ab3:2 C4:2 Eb4:2 Ab4:2 Eb4:2 C4:2 Ab3:2 C4:2 | F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | Eb4:2 G4:2 Bb4:2 Eb5:2 Bb4:2 G4:2 Eb4:2 G4:2 | "
        "Eb4:2 Gb4:2 Bb4:2 Eb5:2 Bb4:2 Gb4:2 Eb4:2 Gb4:2 | Bb3:2 D4:2 F4:2 Bb4:2 F4:2 D4:2 Bb3:2 D4:2 | G3:2 Bb3:2 D4:2 G4:2 D4:2 Bb3:2 G3:2 Bb3:2 | "
        "Eb4:2 G4:2 Bb4:2 Eb5:2 Bb4:2 G4:2 Eb4:2 G4:2 | Eb4:2 Gb4:2 Bb4:2 Eb5:2 Bb4:2 Gb4:2 Eb4:2 Gb4:2 | F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | "
        "F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | Bb3:2 D4:2 F4:2 Bb4:2 F4:2 D4:2 Bb3:2 D4:2 | G3:2 Bb3:2 D4:2 G4:2 D4:2 Bb3:2 G3:2 Bb3:2 | "
        "Eb4:2 G4:2 Bb4:2 Eb5:2 Bb4:2 G4:2 Eb4:2 G4:2 | F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | Bb3:2 D4:2 F4:2 Bb4:2 F4:2 D4:2 Bb3:2 D4:2 | "
        "G3:2 Bb3:2 D4:2 G4:2 D4:2 Bb3:2 G3:2 Bb3:2 | Ab3:2 C4:2 Eb4:2 Ab4:2 Eb4:2 C4:2 Ab3:2 C4:2 | F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | "
        "Eb4:2 G4:2 Bb4:2 Eb5:2 Bb4:2 G4:2 Eb4:2 G4:2 | Eb4:2 Gb4:2 Bb4:2 Eb5:2 Bb4:2 Gb4:2 Eb4:2 Gb4:2 | Bb3:2 D4:2 F4:2 Bb4:2 F4:2 D4:2 Bb3:2 D4:2 | "
        "G3:2 Bb3:2 D4:2 G4:2 D4:2 Bb3:2 G3:2 Bb3:2 | Eb4:2 G4:2 Bb4:2 Eb5:2 Bb4:2 G4:2 Eb4:2 G4:2 | Eb4:2 Gb4:2 Bb4:2 Eb5:2 Bb4:2 Gb4:2 Eb4:2 Gb4:2 | "
        "F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | F4:2 A4:2 C5:2 F5:2 C5:2 A4:2 F4:2 A4:2 | ",
    }},
    {   "HAUNT", 150, 16, true, 0.73f, 0.38f, 0.50f,
        "Gm Gm Eb D Gm Cm F#dim7 D | Bb F Gm D Eb Bb F#dim7 D | Gm Gm Eb D Gm Cm F#dim7 D | Bb F Gm D Eb Bb F#dim7 D", {
        // G minor, spooky but in a hurry: a flute wavering like a theremin (deep vibrato, portamento) with
        // chromatic neighbour notes, over music-box harp arpeggios, a plucked double bass, rim clicks and a
        // tambourine; a diminished seventh turns each strain around, and the middle one lifts to Bb.
        "@FLUTE v76 p-6 e1 m30 g6 "
        "D5:6 C#5:2 D5:4 G5:4 | Bb5:6 A5:2 G5:4 D5:4 | Eb5:6 D5:2 Eb5:4 G5:4 | F#5:8 A5:4 D5:4 | D5:6 C#5:2 D5:4 Bb5:4 | G5:6 F#5:2 G5:4 Eb5:4 | "
        "A4:2 C5:2 Eb5:2 F#5:2 A5:4 F#5:4 | D5:8 .:4 A4:2 C5:2 | "
        "F5:8 D5:4 Bb4:4 | A5:8 F5:4 C5:4 | Bb5:6 A5:2 G5:4 D5:4 | F#5:6 E5:2 D5:4 A4:4 | G5:8 Eb5:4 Bb4:4 | F5:8 D5:4 F5:4 | Eb5:4 C5:4 A4:4 C5:4 | "
        "D5:6 C#5:2 D5:4 .:4 | "
        "D5:6 C#5:2 D5:4 G5:4 | Bb5:6 A5:2 G5:4 D5:4 | Eb5:6 D5:2 Eb5:4 G5:4 | F#5:8 A5:4 D5:4 | D5:6 C#5:2 D5:4 Bb5:4 | G5:6 F#5:2 G5:4 Eb5:4 | "
        "A4:2 C5:2 Eb5:2 F#5:2 A5:4 F#5:4 | D5:8 .:4 A4:2 C5:2 | "
        "Bb4:2 D5:2 F5:2 D5:2 Bb4:2 D5:2 F5:4 | A4:2 C5:2 F5:2 C5:2 A4:2 C5:2 F5:4 | Bb4:2 D5:2 G5:2 D5:2 Bb4:2 D5:2 G5:4 | "
        "A4:2 D5:2 F#5:2 D5:2 A4:2 D5:2 F#5:4 | Bb4:2 Eb5:2 G5:2 Eb5:2 Bb4:2 Eb5:2 G5:4 | D5:2 F5:2 Bb5:2 F5:2 D5:2 F5:2 Bb5:4 | "
        "A4:2 C5:2 Eb5:2 C5:2 A4:2 C5:2 Eb5:4 | D5:2 F#5:2 A5:2 F#5:2 D5:4 .:4 | ",
        "@HARP v58 p22 e1 "
        "[ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ Eb3 G3 Bb3 Eb4 G4 Eb4 Bb3 G3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ C3 Eb3 G3 C4 Eb4 C4 G3 Eb3 ]2 [ F#3 A3 C4 Eb4 F#4 Eb4 C4 A3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 "
        "[ Bb3 D4 F4 Bb4 D5 Bb4 F4 D4 ]2 [ F3 A3 C4 F4 A4 F4 C4 A3 ]2 [ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ Eb3 G3 Bb3 Eb4 G4 Eb4 Bb3 G3 ]2 [ Bb3 D4 F4 Bb4 D5 Bb4 F4 D4 ]2 [ F#3 A3 C4 Eb4 F#4 Eb4 C4 A3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 "
        "[ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ Eb3 G3 Bb3 Eb4 G4 Eb4 Bb3 G3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ C3 Eb3 G3 C4 Eb4 C4 G3 Eb3 ]2 [ F#3 A3 C4 Eb4 F#4 Eb4 C4 A3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 "
        "[ Bb3 D4 F4 Bb4 D5 Bb4 F4 D4 ]2 [ F3 A3 C4 F4 A4 F4 C4 A3 ]2 [ G3 Bb3 D4 G4 Bb4 G4 D4 Bb3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 [ Eb3 G3 Bb3 Eb4 G4 Eb4 Bb3 G3 ]2 [ Bb3 D4 F4 Bb4 D5 Bb4 F4 D4 ]2 [ F#3 A3 C4 Eb4 F#4 Eb4 C4 A3 ]2 [ D3 F#3 A3 D4 F#4 D4 A3 F#3 ]2 ",
        "@DBASS v100 p0 e0 "
        "G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | Eb2:2 .:2 Bb2:2 .:2 Eb2:2 Eb2:2 Bb2:2 .:2 | "
        "D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | C2:2 .:2 G2:2 .:2 C2:2 C2:2 G2:2 .:2 | "
        "F#2:2 .:2 C3:2 .:2 F#2:2 F#2:2 C3:2 .:2 | D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | Bb1:2 .:2 F2:2 .:2 Bb1:2 Bb1:2 F2:2 .:2 | "
        "F2:2 .:2 C3:2 .:2 F2:2 F2:2 C3:2 .:2 | G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | "
        "Eb2:2 .:2 Bb2:2 .:2 Eb2:2 Eb2:2 Bb2:2 .:2 | Bb1:2 .:2 F2:2 .:2 Bb1:2 Bb1:2 F2:2 .:2 | F#2:2 .:2 C3:2 .:2 F#2:2 F#2:2 C3:2 .:2 | "
        "D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | "
        "Eb2:2 .:2 Bb2:2 .:2 Eb2:2 Eb2:2 Bb2:2 .:2 | D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | "
        "C2:2 .:2 G2:2 .:2 C2:2 C2:2 G2:2 .:2 | F#2:2 .:2 C3:2 .:2 F#2:2 F#2:2 C3:2 .:2 | D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | "
        "Bb1:2 .:2 F2:2 .:2 Bb1:2 Bb1:2 F2:2 .:2 | F2:2 .:2 C3:2 .:2 F2:2 F2:2 C3:2 .:2 | G2:2 .:2 D3:2 .:2 G2:2 G2:2 D3:2 .:2 | "
        "D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | Eb2:2 .:2 Bb2:2 .:2 Eb2:2 Eb2:2 Bb2:2 .:2 | Bb1:2 .:2 F2:2 .:2 Bb1:2 Bb1:2 F2:2 .:2 | "
        "F#2:2 .:2 C3:2 .:2 F#2:2 F#2:2 C3:2 .:2 | D2:2 .:2 A2:2 .:2 D2:2 D2:2 A2:2 .:2 | ",
        "v114 p0 e0 [ k . . . q . . . k . k . q . . . ]7 k . . . q . . . k . s . s s s s [ k . . . q . . . . . k . q . . . ]7 k . . . q . . . k . s . q q s s [ k . . . q . . . k . k . q . . . ]7 k . . . q . . . k . s . s s s s [ k . . . q . . . k . k . q . . . ]7 k . s . k . s . s s s s s s s s",
        "v64 p-22 e0 [ h . n . h . n n h . n . h . n . ]8 @ORCHHIT v64 Bb3:2 v64 n . h . n n h . n . h . n . [ h . n . h . n n h . n . h . n . ]7 [ h . n . h . n n h . n . h . n . ]8 @ORCHHIT v64 Bb3:2 v64 n . h . n n h . n . h . n . [ h . n . h . n n h . n . h . n . ]7",
    }},
};
static_assert(sizeof k_songs / sizeof k_songs[0] == SONG_COUNT, "k_songs must list every SongId, in enum order");

enum StepKind : u8 { ST_NOTE, ST_TIE, ST_REST, ST_INST, ST_VOL, ST_PAN, ST_ECHO, ST_VIB, ST_REL, ST_GLIDE };
struct Step { u8 kind; u8 drum; i16 value; u16 rows; };

constexpr int STEP_CAP = 1400;
struct CompiledSong {
    int bpm;
    Step* ch[MUSIC_CH];
    int len[MUSIC_CH];
};
static Step g_steps[SONG_COUNT][MUSIC_CH][STEP_CAP];
static CompiledSong g_compiled[SONG_COUNT];

static int parse_int(const char*& p) {
    int sign = 1, v = 0;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') ++p;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v * sign;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static int compile_channel(const char* src, Step* out, int cap) {
    // expand repeats by recursion over the text
    int n = 0;
    struct Frame { const char* start; int count; } stack[6];
    int depth = 0;
    const char* p = src;
    while (*p) {
        while (*p == ' ' || *p == '|') ++p;
        if (!*p) break;
        if (*p == '[') { stack[depth++] = Frame{p + 1, -1}; ++p; continue; }
        if (*p == ']') {
            ++p;
            int times = is_digit(*p) ? parse_int(p) : 1;
            Frame& f = stack[depth - 1];
            if (f.count < 0) f.count = times;
            if (--f.count > 0) p = f.start;
            else --depth;
            continue;
        }
        Step s{};
        s.rows = 1;
        char c = *p;
        if (c == '@') {
            ++p;
            const char* name = p;
            while (*p && *p != ' ') ++p;
            int len = (int)(p - name);
            s.kind = ST_INST;
            s.value = -1;
            for (int i = 0; i < SMP_COUNT; ++i) {
                const char* q = k_inst_names[i];
                int l = 0;
                while (q[l]) ++l;
                if (l == len) {
                    bool eq = true;
                    for (int j = 0; j < l; ++j) if (q[j] != name[j]) eq = false;
                    if (eq) s.value = (i16)i;
                }
            }
            s.rows = 0;
            if (s.value < 0) {
                s.value = 0;
                KV_WARN("sprocket", "song text: unknown instrument '@%.*s'", len, name);
            }
        } else if (c == 'v') { ++p; s.kind = ST_VOL; s.value = (i16)parse_int(p); s.rows = 0; }
        else if (c == 'p' && (p[1] == '-' || p[1] == '+' || is_digit(p[1]))) { // a bare 'p' is the power snare
            ++p; s.kind = ST_PAN; s.value = (i16)parse_int(p); s.rows = 0;
        }
        else if (c == 'e') { ++p; s.kind = ST_ECHO; s.value = (i16)parse_int(p); s.rows = 0; }
        else if (c == 'm') { ++p; s.kind = ST_VIB; s.value = (i16)parse_int(p); s.rows = 0; }
        else if (c == 'r') { ++p; s.kind = ST_REL; s.value = (i16)parse_int(p); s.rows = 0; }
        else if (c == 'g') { ++p; s.kind = ST_GLIDE; s.value = (i16)parse_int(p); s.rows = 0; }
        else if (c == '-') { ++p; s.kind = ST_TIE; }
        else if (c == '.') { ++p; s.kind = ST_REST; }
        else if (c >= 'A' && c <= 'G') {
            static const int base[7] = {9, 11, 0, 2, 4, 5, 7};
            int note = base[c - 'A'];
            ++p;
            if (*p == '#') { ++note; ++p; }
            else if (*p == 'b') { --note; ++p; }
            int oct = parse_int(p);
            s.kind = ST_NOTE;
            s.value = (i16)(note + (oct + 1) * 12);
        } else {
            static const struct { char c; u8 smp; } drums[] = {
                {'k', SMP_KICK}, {'s', SMP_SNARE}, {'p', SMP_PSNARE}, {'h', SMP_HAT}, {'o', SMP_OHAT}, {'c', SMP_CRASH},
                {'t', SMP_TOM}, {'y', SMP_STOM}, {'n', SMP_TAMB}, {'q', SMP_CASTANET}, {'b', SMP_BONGO}, {'j', SMP_JTOM},
                {'i', SMP_TAIKO}, {'x', SMP_CLAP},
            };
            int idx = -1;
            for (int i = 0; i < (int)(sizeof drums / sizeof drums[0]); ++i) if (drums[i].c == c) idx = i;
            ++p;
            if (idx < 0) { KV_WARN("sprocket", "song text: unknown token '%c'", c); continue; }
            s.kind = ST_NOTE;
            s.drum = 1;
            s.value = (i16)drums[idx].smp;
        }
        if (*p == ':') { ++p; s.rows = (u16)parse_int(p); }
        while (*p && *p != ' ' && *p != '|' && *p != ']' && *p != '[') ++p;
        if (n < cap) out[n++] = s;
        else { KV_WARN("sprocket", "song text: a channel is longer than %d steps", cap); break; }
    }
    return n;
}

// ---- sound effects ------------------------------------------------------------------------
// Each effect is a short program for one voice, or two programs side by side when it needs
// both effect voices. A step: source (a sample, NOISE or REST), note, milliseconds, volume
// 0-127, a pitch slide in semitones over the step, a volume fade over the step (in volume
// units, negative fades out), flags, and a pitch wobble (rate in Hz, depth in 1/8 semitones).
// A TIE step does not restart the voice: it carries on from where the last step left it, with
// a new slide, fade and wobble (note 0 / volume 0 mean "from where it is"). The noise "note"
// sets the generator's clock: low notes rumble, high notes hiss.
constexpr u8 NOISE = 255, REST = 254;
constexpr u8 TIE = 1;
struct SfxStep { u8 src; i8 note; u16 ms; u8 vol = 0; i8 slide = 0; i8 fade = 0; u8 flags = 0; u8 wob_hz = 0, wob_depth = 0; };
struct SfxDef { u8 priority; const SfxStep* a; int na; const SfxStep* b; int nb; };

#define SFX(name, ...) static const SfxStep name##_a[] = {__VA_ARGS__};
#define SFXB(name, ...) static const SfxStep name##_b[] = {__VA_ARGS__};
// -- the race's own voice: beeps and jingles on the square wave, pick-ups on the vibraphone
SFX(count, {SMP_SQUARE, 72, 150, 100, 0, -56})                                             // countdown beep
SFX(go, {SMP_SQUARE, 84, 380, 84, 0, -56})                                                // GO: the beep an octave up...
SFXB(go, {SMP_SQUARE, 72, 380, 60, 0, -40})                                               // ...over the beep itself
SFX(menu_move, {SMP_SQUARE, 84, 30, 66})                                                  // cursor tick
SFX(menu_ok, {SMP_SQUARE, 84, 50, 78}, {SMP_SQUARE, 91, 90, 78, 0, -40})                  // rising fifth
SFX(item_box, {SMP_VIBES, 84, 40, 100}, {SMP_VIBES, 88, 40, 100}, {SMP_VIBES, 91, 40, 100}, {SMP_VIBES, 96, 110, 100, 0, -50})
SFX(roulette, {SMP_SQUARE, 91, 22, 56})                                                   // roulette tick
SFX(item_get, {SMP_VIBES, 91, 60, 104}, {SMP_VIBES, 96, 160, 104, 0, -60})                // roulette lands
SFX(bolt, {SMP_VIBES, 88, 40, 92}, {SMP_VIBES, 95, 110, 92, 0, -50})                      // bolt picked up: a bright ding
SFX(hop, {SMP_SQUARE, 72, 70, 56, 7})                                                     // little upward blip
SFX(boost, {SMP_SAW, 48, 420, 127, 24, -60})                                               // two-octave rising saw...
SFXB(boost, {NOISE, 70, 420, 56, 14, -50})                                                // ...with a rush of air
SFX(miniturbo, {SMP_GSYNTH, 60, 260, 160, 12, -50})                                        // octave swoop
SFX(throw_, {SMP_SQUARE, 79, 120, 84, -7, -30})                                           // falling blip
SFX(explode, {NOISE, 44, 100, 108, -6}, {NOISE, 0, 400, 0, -12, -100, TIE})               // low noise burst dying away...
SFXB(explode, {SMP_KICK, 29, 400, 110, -4})                                               // ...on a deep thump
SFX(hit, {SMP_ORCHHIT, 55, 420, 105, -3})                                                 // orchestra stab
SFX(oil, {NOISE, 70, 160, 80, -12, -40})                                                  // squelch...
SFXB(oil, {SMP_SQUARE, 55, 70, 60, -9}, {SMP_SQUARE, 50, 90, 60, -10, -40})               // ...and two slippery blups
SFX(wall, {SMP_KICK, 40, 120, 110})                                                       // thud...
SFXB(wall, {NOISE, 50, 50, 56, -6, -46})                                                  // ...with a crunch
SFX(lap, {SMP_SQUARE, 84, 70, 76}, {SMP_SQUARE, 88, 70, 76}, {SMP_SQUARE, 91, 160, 76, 0, -44})
SFX(final_lap, {SMP_BRASS, 72, 110, 104}, {SMP_BRASS, 76, 110, 104}, {SMP_BRASS, 79, 110, 104}, {SMP_BRASS, 84, 380, 104, 0, -30})
SFX(shield, {SMP_STRINGS, 72, 420, 40, 12, 56})                                           // string swell up an octave...
SFXB(shield, {SMP_VIBES, 84, 60, 70}, {SMP_VIBES, 91, 60, 70}, {SMP_VIBES, 96, 220, 70, 0, -50}) // ...and a sparkle
SFX(charge, {SMP_SQUARE, 91, 40, 54, 5})                                                  // drift charge level
SFX(skid, {NOISE, 78, 90, 44, 0, -20})                                                    // tyre hiss
// -- the quality pass
SFX(splash, {NOISE, 86, 50, 100, -8}, {NOISE, 0, 380, 0, -22, -96, TIE})                  // bright burst sinking to a wash...
SFXB(splash, {SMP_SQUARE, 58, 110, 70, -14, -50})                                         // ...and the "ploop"
SFX(fall, {SMP_SQUARE, 89, 750, 60, -30, -30, 0, 7, 3})                                   // long descending whistle
SFX(rescue, {SMP_SQUARE, 72, 60, 64, 5}, {REST, 0, 25}, {SMP_SQUARE, 76, 60, 64, 5}, {REST, 0, 25},
            {SMP_SQUARE, 79, 60, 64, 5}, {REST, 0, 25}, {SMP_SQUARE, 84, 120, 64, 5, -30}) // four rising chirps
SFX(train_horn, {SMP_SQUARE, 71, 170, 60, 1, 0, 0, 6, 1}, {REST, 0, 70}, {SMP_SQUARE, 71, 580, 60, 1, -38, 0, 6, 1})   // toot, tooooot...
SFXB(train_horn, {SMP_SQUARE, 76, 170, 52, 1, 0, 0, 6, 1}, {REST, 0, 70}, {SMP_SQUARE, 76, 580, 52, 1, -34, 0, 6, 1}) // ...a fourth above
SFX(cannon, {NOISE, 80, 25, 100}, {NOISE, 40, 400, 106, -10, -98, TIE})                  // crack, then the boom rolling off...
SFXB(cannon, {SMP_KICK, 24, 450, 112, -3})                                                // ...over a very low thump
SFX(roar, {SMP_SAW, 38, 250, 64, 6, 40, 0, 28, 10}, {SMP_SAW, 0, 650, 0, -10, -98, TIE, 22, 14}) // growl rising and falling...
SFXB(roar, {NOISE, 58, 250, 28, 4, 44}, {NOISE, 0, 650, 0, -8, -70, TIE})                 // ...with breath
SFX(fire, {NOISE, 62, 220, 28, 14, 78}, {NOISE, 0, 520, 0, -12, -104, TIE})               // whoosh up, then roaring down
SFX(stomp, {SMP_KICK, 22, 420, 118, -3})                                                  // a huge slow thud...
SFXB(stomp, {NOISE, 36, 60, 104}, {NOISE, 0, 300, 0, -8, -102, TIE})                      // ...and the ground shaking
SFX(zap, {SMP_SAW, 86, 380, 104, -34, -64, 0, 38, 24})                                     // fast-warbling falling saw...
SFXB(zap, {NOISE, 92, 40, 60}, {REST, 0, 40}, {NOISE, 92, 40, 52}, {REST, 0, 50}, {NOISE, 90, 60, 44, 0, -36}) // ...and crackles
SFX(rocket, {NOISE, 58, 550, 56, 22, 30}, {NOISE, 0, 250, 0, 4, -84, TIE})                // air rushing up...
SFXB(rocket, {SMP_SAW, 43, 600, 68, 19, -30})                                             // ...over a rising motor
SFX(ramp, {SMP_SQUARE, 60, 330, 66, 24, -30, 0, 14, 3})                                   // two-octave swoop up
SFX(land, {SMP_KICK, 31, 140, 112})                                                       // low thud...
SFXB(land, {NOISE, 46, 70, 64, -4, -56})                                                  // ...and a scuff
SFX(whoosh, {SMP_SAW, 64, 200, 20, -2, 60}, {SMP_SAW, 0, 300, 0, -4, -78, TIE})           // hum swelling past, dropping in pitch...
SFXB(whoosh, {NOISE, 74, 200, 12, 0, 40}, {NOISE, 0, 300, 0, -4, -50, TIE})               // ...with its wake
SFX(rumble, {NOISE, 30, 300, 30, 2, 80, 0, 9, 16}, {NOISE, 0, 600, 0, -3, -104, TIE, 9, 16}) // rolling low noise, swelling and passing...
SFXB(rumble, {SMP_KICK, 24, 220, 96}, {SMP_KICK, 22, 220, 104}, {SMP_KICK, 24, 220, 96}, {SMP_KICK, 22, 240, 76}) // ...bumping along
SFX(drop, {SMP_VIBES, 91, 45, 84}, {SMP_VIBES, 88, 45, 82}, {SMP_VIBES, 84, 45, 80}, {SMP_VIBES, 79, 45, 76},
          {SMP_VIBES, 76, 45, 72}, {SMP_VIBES, 72, 120, 68, 0, -50})                      // descending sparkle
SFX(magnet, {SMP_SAW, 43, 260, 50, 5, 50, 0, 30, 4}, {SMP_SQUARE, 79, 35, 80})            // rising hum, then the click...
SFXB(magnet, {REST, 0, 260}, {SMP_KICK, 48, 80, 72})                                     // ...of the latch
SFX(start_boost, {SMP_SAW, 50, 380, 100, 26, -50})                                        // the boost...
SFXB(start_boost, {SMP_VIBES, 84, 55, 92}, {SMP_VIBES, 88, 55, 92}, {SMP_VIBES, 91, 55, 92}, {SMP_VIBES, 96, 200, 92, 0, -60}) // ...with a fanfare of bells
SFX(best_lap, {SMP_SQUARE, 79, 80, 64}, {SMP_SQUARE, 84, 80, 64}, {SMP_SQUARE, 88, 80, 64}, {SMP_SQUARE, 86, 80, 64}, {SMP_SQUARE, 91, 260, 64, 0, -40})
SFXB(best_lap, {SMP_SQUARE, 76, 80, 42}, {SMP_SQUARE, 79, 80, 42}, {SMP_SQUARE, 84, 80, 42}, {SMP_SQUARE, 83, 80, 42}, {SMP_SQUARE, 88, 260, 42, 0, -26}) // in thirds
SFX(pause, {SMP_SQUARE, 79, 70, 70}, {SMP_SQUARE, 74, 70, 70}, {SMP_SQUARE, 67, 150, 70, 0, -40}) // three notes down
SFX(back, {SMP_SQUARE, 79, 50, 72}, {SMP_SQUARE, 72, 100, 72, 0, -40})                    // falling fifth: menu_ok upside down
SFX(bump, {SMP_SQUARE, 52, 80, 84, -7, -40})                                              // bonk...
SFXB(bump, {SMP_KICK, 43, 90, 90})                                                       // ...with a knock
SFX(wrong_way, {SMP_SQUARE, 52, 160, 80, 0, 0, 0, 30, 6}, {REST, 0, 70}, {SMP_SQUARE, 52, 160, 80, 0, 0, 0, 30, 6}) // two low buzzes
SFX(stall, {NOISE, 54, 60, 120, -4, -84}, {REST, 0, 50}, {NOISE, 50, 60, 110, -4, -78}, {REST, 0, 80}, {NOISE, 46, 100, 100, -6, -86}) // three coughs...
SFXB(stall, {SMP_SAW, 43, 450, 104, -14, -90, 0, 16, 10})                                  // ...as the motor sags
SFX(draft, {NOISE, 80, 260, 14, 4, 44}, {NOISE, 0, 320, 0, -4, -50, TIE})                 // soft gust
SFX(bell, {SMP_VIBES, 55, 1500, 112, 0, -26})                                             // a low bell tolling once...
SFXB(bell, {SMP_VIBES, 62, 1500, 64, 0, -26})                                             // ...with its fifth ringing over it
#undef SFX
#undef SFXB

static const SfxDef k_sfx[] = {
#define E(name, prio) {prio, name##_a, (int)(sizeof name##_a / sizeof(SfxStep)), nullptr, 0}
#define E2(name, prio) {prio, name##_a, (int)(sizeof name##_a / sizeof(SfxStep)), name##_b, (int)(sizeof name##_b / sizeof(SfxStep))}
    E(count, 3), E2(go, 3), E(menu_move, 1), E(menu_ok, 2), E(item_box, 2), E(roulette, 0), E(item_get, 2), E(bolt, 1),
    E(hop, 1), E2(boost, 3), E(miniturbo, 2), E(throw_, 2), E2(explode, 4), E(hit, 4), E2(oil, 3), E2(wall, 2), E(lap, 3),
    E(final_lap, 4), E2(shield, 3), E(charge, 1), E(skid, 0),
    E2(splash, 3), E(fall, 4), E(rescue, 3), E2(train_horn, 3), E2(cannon, 3), E2(roar, 3), E(fire, 2), E2(stomp, 3), E2(zap, 4),
    E2(rocket, 3), E(ramp, 2), E2(land, 2), E2(whoosh, 1), E2(rumble, 2), E(drop, 2), E2(magnet, 2), E2(start_boost, 3),
    E2(best_lap, 4), E(pause, 3), E(back, 2), E2(bump, 2), E(wrong_way, 3), E2(stall, 3), E(draft, 0), E2(bell, 3),
#undef E
#undef E2
};
static_assert(sizeof k_sfx / sizeof k_sfx[0] == SFX_COUNT, "k_sfx must list every SfxId, in enum order");

// ---- the DSP ------------------------------------------------------------------------------
enum Stage : u8 { OFF, ATTACK, DECAY, SUSTAIN, RELEASE, GAIN };
struct Voice {
    int  smp = -1;
    bool noise = false;
    f64  pos = 0;
    f32  note = 60, slide = 0;       // semitones; slide per sample
    f32  glide = 0; u32 glide_n = 0; // portamento: per-sample step, samples left
    f32  env = 0;
    u8   stage = OFF;
    Adsr a{};
    f32  rel_k = 0;                  // RELEASE: the envelope's multiplier per sample
    f32  gain_to = 0, gain_k = 0;    // GAIN: the envelope chases gain_to (the DSP's direct gain)
    f32  vol = 1, vol_slide = 0, pan = 0; // pan -1..1
    bool echo = false;
    f32  vib = 0, vib_hz = 5.5f;     // vibrato depth in semitones, rate
    u32  vib_delay = 0, age = 0;     // samples
    f32  out = 0, tail = 0;          // the last output, and what a cut-off note leaves to die away (no clicks)
};

static f32 g_gauss[256][4];
static Voice g_v[VOICES];
static f32 g_echo[ECHO_FRAMES][2];
static int g_echo_pos = 0;
static f32 g_fir_hist[8][2];
static const f32 k_fir[8] = {12, 33, 43, 43, 19, -2, -13, -7}; // a gentle low-pass, sums to 128
static f32 g_echo_vol = 0.30f, g_echo_fb = 0.42f, g_music_gain = 1.0f;
static u32 g_noise = 0x4000;
static f32 g_noise_acc = 0;
constexpr f32 TAIL_K = 0.992f;       // a cut note's last level fades in about 15 ms

static void voice_key_on(Voice& v, int smp, f32 note, f32 vol, f32 pan, bool echo) {
    v.tail += v.stage != OFF ? v.out : 0;
    v.smp = smp; v.noise = false; v.pos = 0; v.note = note; v.slide = 0; v.glide_n = 0;
    v.env = 0; v.stage = ATTACK; v.a = k_adsr[smp]; v.vol = vol; v.vol_slide = 0; v.pan = pan; v.echo = echo;
    v.vib = 0; v.age = 0; v.out = 0;
}

// Key-on in direct gain: the envelope rises to 1 over `rise` seconds and then follows gain_to.
static void voice_gain_on(Voice& v, int smp, f32 note, f32 vol, f32 rise) {
    voice_key_on(v, smp, note, vol, 0, false);
    v.stage = GAIN; v.gain_to = 1; v.gain_k = 1.0f / (rise * RATE);
}

static void voice_key_off(Voice& v, f32 release) {
    if (v.stage == OFF || v.stage == RELEASE) return;
    if (release < 0.004f) release = 0.004f;
    v.rel_k = kv::exp(-6.9f / (release * RATE));
    v.stage = RELEASE;
}

static f32 voice_sample(Voice& v) {
    if (v.stage == OFF) {
        v.tail *= TAIL_K;
        if (v.tail > -1e-5f && v.tail < 1e-5f) v.tail = 0;
        return v.tail;
    }
    // envelope
    const Adsr& a = v.a;
    switch (v.stage) {
    case ATTACK: v.env += 1.0f / (a.attack * RATE); if (v.env >= 1) { v.env = 1; v.stage = DECAY; } break;
    case DECAY:
        if (a.decay <= 0) { v.env = a.sustain; v.stage = SUSTAIN; break; }
        v.env -= (v.env - a.sustain) * (4.0f / (a.decay * RATE));
        if (v.env - a.sustain < 0.002f) v.stage = SUSTAIN;
        break;
    case SUSTAIN: if (a.fade > 0) { v.env -= a.sustain / (a.fade * RATE); if (v.env <= 0) { v.env = 0; v.stage = OFF; } } break;
    case RELEASE: v.env *= v.rel_k; if (v.env <= 0.0006f) { v.env = 0; v.stage = OFF; } break;
    case GAIN: {
        f32 d = v.gain_to - v.env;
        if (d > v.gain_k) d = v.gain_k;
        else if (d < -v.gain_k) d = -v.gain_k;
        v.env += d;
        break;
    }
    }
    v.note += v.slide;
    if (v.glide_n) { v.note += v.glide; --v.glide_n; }
    v.vol += v.vol_slide;
    if (v.vol < 0) v.vol = 0;
    f32 note = v.note;
    if (v.vib > 0 && v.age >= v.vib_delay) {
        // a sine-ish LFO (a parabola per half cycle), eased in over a fifth of a second
        f32 t = (f32)(v.age - v.vib_delay) / RATE;
        f32 ph = t * v.vib_hz;
        ph -= (f32)(i32)ph;
        f32 x = ph * 2.0f - 1.0f;
        f32 lfo = 4.0f * x * (1.0f - (x < 0 ? -x : x));
        note += v.vib * lfo * (v.vib_delay && t < 0.2f ? t * 5.0f : 1.0f);
    }
    ++v.age;
    f32 s;
    if (v.noise) {
        // the DSP's noise: a 15-bit LFSR clocked at a rate set by the "note"
        f32 hz = 440.0f * kv::exp2((note - 69.0f) / 12.0f) * 8.0f;
        g_noise_acc += hz / RATE;
        while (g_noise_acc >= 1.0f) {
            g_noise_acc -= 1.0f;
            u32 fb = (g_noise ^ (g_noise >> 1)) & 1;
            g_noise = (g_noise >> 1) | (fb << 14);
        }
        s = ((f32)(g_noise & 0x7fff) / 16384.0f - 1.0f) * 0.5f;
    } else {
        const BankSample& b = k_bank[v.smp];
        f32 step = b.rate / (f32)RATE * kv::exp2((note - b.root) / 12.0f);
        if (step > 3.99f) step = 3.99f; // the pitch register's ceiling
        i32 i = (i32)v.pos;
        f32 frac = (f32)(v.pos - (f64)i);
        const f32* w = g_gauss[(int)(frac * 255.0f)];
        const i16* pcm = g_pcm + g_pcm_off[v.smp];
        auto at = [&](i32 k) -> f32 {
            if (k < 0) return 0;
            if ((u32)k >= b.loop_end) {
                if (b.loop_start < 0) return 0;
                k = b.loop_start + (i32)((u32)(k - b.loop_start) % (b.loop_end - (u32)b.loop_start));
            }
            return (f32)pcm[k];
        };
        s = at(i - 1) * w[0] + at(i) * w[1] + at(i + 1) * w[2] + at(i + 2) * w[3];
        v.pos += step;
        if (v.pos >= (f64)b.loop_end) {
            if (b.loop_start < 0) { v.stage = OFF; }
            else v.pos -= (f64)(b.loop_end - (u32)b.loop_start);
        }
        s *= kv::exp2(-b.atten_db / 6.02f) / 16384.0f;
    }
    if (v.stage == GAIN && v.gain_to <= 0 && v.env <= 0.0006f) { v.stage = OFF; v.env = 0; }
    v.out = s * v.env * v.vol;
    v.tail *= TAIL_K;
    return v.out + v.tail;
}

// ---- the sequencer and effect players (audio thread) ----------------------------------------
struct ChanState {
    int idx = 0; int rows_left = 0; int inst = SMP_LEAD; f32 vol = 1, pan = 0; bool echo = false;
    f32 vib = 0, rel = 0;   // vibrato depth in semitones; release override in seconds (0: the instrument's)
    f32 glide = 0; int last = -1; // portamento time in seconds; the note before this one (-1: a rest)
    bool ended = false;     // a song that does not loop has run out
};
static ChanState g_ch[MUSIC_CH];
static int g_song = -1;
static f64 g_row_acc = 0;
static f32 g_tempo = 1.0f;  // 1.12 on the final lap
constexpr f32 FINAL_LAP_TEMPO = 1.12f;
constexpr f32 VIB_DELAY = 0.25f, VIB_HZ = 5.6f;

struct SfxState { const SfxStep* steps = nullptr; int n = 0; int step = 0; int samples_left = 0; int priority = 0; };
static SfxState g_sfx[2];

// the engine voice: what the game asked for, and where the voice has got to
struct EngineState { f32 speed = 0; bool running = false; };
static EngineState g_eng;

// commands from the game thread: a single-producer single-consumer ring
static u32 g_ring[256];
static u32 g_ring_w = 0, g_ring_r = 0;
static u32 g_engine_bits = 0; // f32 bits: speed 0..1.5
static u32 g_engine_on = 0;
static bool g_open = false;

enum Cmd : u32 { CMD_MUSIC = 1, CMD_SFX = 2, CMD_FINAL_LAP = 3 };

static void push_cmd(u32 c) {
    u32 w = atomic_load(&g_ring_w, MO_RELAXED);
    u32 r = atomic_load(&g_ring_r, MO_ACQUIRE);
    if (w - r >= 256) return;
    g_ring[w & 255] = c;
    atomic_store(&g_ring_w, w + 1, MO_RELEASE);
}

static void stop_music_voices() {
    for (int c = 0; c < MUSIC_CH; ++c) voice_key_off(g_v[c], 0.03f);
}

static void start_song(int s) {
    g_song = s;
    g_row_acc = 0;
    g_tempo = 1.0f; // the final lap's lift belongs to the song it was asked for
    g_music_gain = k_songs[s].gain;
    g_echo_vol = k_songs[s].echo_vol;
    g_echo_fb = k_songs[s].echo_fb;
    for (int c = 0; c < MUSIC_CH; ++c) g_ch[c] = ChanState{};
    stop_music_voices();
}

static void advance_channel(int c) {
    ChanState& st = g_ch[c];
    const CompiledSong& song = g_compiled[g_song];
    if (song.len[c] == 0 || st.ended) return;
    if (st.rows_left > 0 && --st.rows_left > 0) return;
    Voice& v = g_v[c];
    for (int guard = 0; guard < 64; ++guard) {
        if (st.idx >= song.len[c]) {
            if (!k_songs[g_song].loop) { // played once: let the last note ring off
                st.ended = true;
                voice_key_off(v, v.a.release > 0.35f ? v.a.release : 0.35f);
                return;
            }
            st.idx = 0;
        }
        const Step& s = song.ch[c][st.idx++];
        switch (s.kind) {
        case ST_INST: st.inst = s.value; continue;
        case ST_VOL: st.vol = (f32)s.value / 127.0f; continue;
        case ST_PAN: st.pan = (f32)s.value / 64.0f; continue;
        case ST_ECHO: st.echo = s.value != 0; continue;
        case ST_VIB: st.vib = (f32)s.value / 100.0f; continue;
        case ST_REL: st.rel = (f32)s.value / 100.0f; continue;
        case ST_GLIDE: st.glide = (f32)s.value / 100.0f; continue;
        case ST_NOTE:
            if (s.drum) voice_key_on(v, s.value, k_bank[s.value].root, st.vol, st.pan, st.echo);
            else {
                voice_key_on(v, st.inst, (f32)s.value, st.vol, st.pan, st.echo);
                v.vib = st.vib; v.vib_hz = VIB_HZ; v.vib_delay = (u32)(VIB_DELAY * RATE);
                if (st.glide > 0 && st.last >= 0 && st.last != s.value) {
                    v.glide_n = (u32)(st.glide * RATE);
                    v.glide = (f32)(s.value - st.last) / (f32)v.glide_n;
                    v.note = (f32)st.last;
                }
                st.last = s.value;
            }
            break;
        case ST_REST: voice_key_off(v, st.rel > 0 ? st.rel : v.a.release); st.last = -1; break;
        case ST_TIE: break;
        }
        st.rows_left = s.rows;
        return;
    }
}

static void sfx_start_step(int slot) {
    SfxState& s = g_sfx[slot];
    const SfxStep& st = s.steps[s.step];
    Voice& v = g_v[SFX_V0 + slot];
    s.samples_left = st.ms * RATE / 1000;
    f32 n = (f32)(s.samples_left > 0 ? s.samples_left : 1);
    if (st.src == REST) { voice_key_off(v, 0.01f); return; }
    if ((st.flags & TIE) && v.stage != OFF) {
        if (st.note) v.note = (f32)st.note;
        if (st.vol) v.vol = (f32)st.vol / 127.0f;
    } else {
        voice_gain_on(v, st.src == NOISE ? (int)SMP_SQUARE : (int)st.src, (f32)st.note, (f32)st.vol / 127.0f, 0.0015f);
        v.noise = st.src == NOISE;
        v.echo = !v.noise;
    }
    v.slide = (f32)st.slide / n;
    v.vol_slide = (f32)st.fade / 127.0f / n;
    if (st.wob_hz || !(st.flags & TIE)) { v.vib = (f32)st.wob_depth / 8.0f; v.vib_hz = (f32)st.wob_hz; v.vib_delay = 0; }
}

static void sfx_claim(int slot, const SfxStep* steps, int n, int priority) {
    g_sfx[slot] = SfxState{steps, n, 0, 0, priority};
    sfx_start_step(slot);
}

static void play_sfx(int id) {
    const SfxDef& d = k_sfx[id];
    // a free slot, else the lower-priority one, else the older
    int slot = -1;
    for (int i = 0; i < 2; ++i) if (!g_sfx[i].steps) { slot = i; break; }
    if (slot < 0) {
        slot = g_sfx[0].priority <= g_sfx[1].priority ? 0 : 1;
        if (d.priority < g_sfx[slot].priority) return;
    }
    sfx_claim(slot, d.a, d.na, d.priority);
    // the second layer takes the other voice if it is free or playing something no more important
    int other = slot ^ 1;
    if (d.b && (!g_sfx[other].steps || g_sfx[other].priority <= d.priority)) sfx_claim(other, d.b, d.nb, d.priority);
}

// The engine: the looped engine sample in direct gain. The pitch glides after the speed, it
// lopes at idle (a slow, deep wobble that tightens as the revs come up), and it opens up
// under boost. Kept low and quiet: the music leads.
static void engine_control(bool on, f32 speed) {
    Voice& v = g_v[ENGINE_V];
    if (on && !g_eng.running) {
        voice_gain_on(v, SMP_ENGINE, 31.0f, 0, 0.05f);
        v.gain_k = 1.0f / (0.06f * RATE);
        g_eng.speed = 0;
        g_eng.running = true;
    } else if (!on && g_eng.running) {
        v.gain_to = 0; // fades over 60 ms, then the voice switches itself off
        g_eng.running = false;
    }
    if (!g_eng.running) return;
    if (speed < 0) speed = 0;
    if (speed > 1.6f) speed = 1.6f;
    g_eng.speed = speed;
}

constexpr f32 ENGINE_VOL = 0.30f;
static void engine_sample() {
    if (!g_eng.running) return;
    Voice& v = g_v[ENGINE_V];
    f32 target = 31.0f + 14.0f * g_eng.speed;
    v.note += (target - v.note) * (1.0f / (0.09f * RATE));   // glide
    f32 s = (v.note - 31.0f) / 14.0f;                         // the speed the voice has reached
    f32 idle = s < 0.3f ? 1.0f - s / 0.3f : 0.0f;
    f32 boost = s > 0.95f ? (s - 0.95f) / 0.3f : 0.0f;
    if (boost > 1) boost = 1;
    v.vib = 0.05f + 0.30f * idle;
    v.vib_hz = 7.0f + 6.0f * s;
    v.vol = ENGINE_VOL * (0.62f + 0.38f * (s < 1 ? s : 1.0f) + 0.45f * boost);
}

// The output stage. Below KNEE it is a wire; a peak that would have clipped (an explosion
// landing on a downbeat) is rounded off into the last fifth of the range instead.
constexpr f32 KNEE = 0.8f;
static f32 dac(f32 x) {
    f32 a = x < 0 ? -x : x;
    if (a <= KNEE) return x;
    f32 d = (a - KNEE) / (1.0f - KNEE);
    a = KNEE + (1.0f - KNEE) * d / (1.0f + d);
    return x < 0 ? -a : a;
}

static void audio_callback(void*, f32* out, u32 frames) {
    // commands
    u32 w = atomic_load(&g_ring_w, MO_ACQUIRE);
    u32 r = g_ring_r;
    while (r != w) {
        u32 c = g_ring[r & 255];
        ++r;
        u32 kind = c >> 16, arg = c & 0xffff;
        if (kind == CMD_MUSIC) {
            if (arg == 0xffff) { g_song = -1; g_tempo = 1.0f; stop_music_voices(); }
            else if ((int)arg < SONG_COUNT && (int)arg != g_song) start_song((int)arg);
        } else if (kind == CMD_SFX && arg < SFX_COUNT) play_sfx((int)arg);
        else if (kind == CMD_FINAL_LAP) g_tempo = arg ? FINAL_LAP_TEMPO : 1.0f;
    }
    atomic_store(&g_ring_r, r, MO_RELEASE);

    f32 speed;
    u32 bits = atomic_load(&g_engine_bits, MO_RELAXED);
    memcpy(&speed, &bits, sizeof speed);
    engine_control(atomic_load(&g_engine_on, MO_RELAXED) != 0, speed);

    // tempo only: the rows come sooner, the pitches are untouched
    f64 row_samples = g_song >= 0 ? (f64)RATE * 60.0 / ((f64)g_compiled[g_song].bpm * 4.0 * (f64)g_tempo) : 0;
    for (u32 fidx = 0; fidx < frames; ++fidx) {
        if (g_song >= 0) {
            g_row_acc -= 1.0;
            if (g_row_acc <= 0) {
                g_row_acc += row_samples;
                for (int c = 0; c < MUSIC_CH; ++c) advance_channel(c);
            }
        }
        for (int s = 0; s < 2; ++s) {
            SfxState& st = g_sfx[s];
            if (!st.steps) continue;
            if (--st.samples_left <= 0) {
                if (++st.step >= st.n) { st.steps = nullptr; voice_key_off(g_v[SFX_V0 + s], 0.03f); }
                else sfx_start_step(s);
            }
        }
        engine_sample();
        f32 l = 0, rr = 0, el = 0, er = 0;
        for (int i = 0; i < VOICES; ++i) {
            Voice& v = g_v[i];
            if (v.stage == OFF && v.tail == 0) continue;
            f32 x = voice_sample(v);
            if (i < MUSIC_CH) x *= g_music_gain;
            f32 pl = v.pan <= 0 ? 1.0f : 1.0f - v.pan, pr = v.pan >= 0 ? 1.0f : 1.0f + v.pan;
            l += x * pl; rr += x * pr;
            if (v.echo) { el += x * pl; er += x * pr; }
        }
        // echo: FIR over the delayed signal, fed back into the buffer
        f32* e = g_echo[g_echo_pos];
        for (int k = 7; k > 0; --k) { g_fir_hist[k][0] = g_fir_hist[k - 1][0]; g_fir_hist[k][1] = g_fir_hist[k - 1][1]; }
        g_fir_hist[0][0] = e[0]; g_fir_hist[0][1] = e[1];
        f32 fl = 0, fr = 0;
        for (int k = 0; k < 8; ++k) { fl += g_fir_hist[k][0] * k_fir[k]; fr += g_fir_hist[k][1] * k_fir[k]; }
        fl /= 128.0f; fr /= 128.0f;
        e[0] = el + fl * g_echo_fb; e[1] = er + fr * g_echo_fb;
        g_echo_pos = (g_echo_pos + 1) % ECHO_FRAMES;
        l += fl * g_echo_vol; rr += fr * g_echo_vol;
        // the DAC: 16-bit. The mix is levelled to stay under full scale; the knee is the fuse.
        out[fidx * 2] = dac(l * 0.5f * g_master);
        out[fidx * 2 + 1] = dac(rr * 0.5f * g_master);
    }
}

// ---- the game's side ----------------------------------------------------------------------
static void init_tables() {
    bool have[SMP_COUNT] = {};
    for (const AdsrDef& d : k_adsr_defs) { k_adsr[d.smp] = d.adsr; have[d.smp] = true; }
    for (int i = 0; i < SMP_COUNT; ++i)
        if (!have[i]) { k_adsr[i] = Adsr{0.002f, 0.2f, 0.8f, 0.0f, 0.05f}; KV_WARN("sprocket", "sample %s has no envelope", k_smp_labels[i]); }
    decode_bank();
    for (int i = 0; i < 256; ++i) {
        f32 f = (f32)i / 255.0f, sum = 0;
        const f32 d[4] = {1.0f + f, f, 1.0f - f, 2.0f - f};
        for (int k = 0; k < 4; ++k) { g_gauss[i][k] = kv::exp(-(d[k] * d[k]) / (2.0f * 0.55f * 0.55f)); sum += g_gauss[i][k]; }
        for (int k = 0; k < 4; ++k) g_gauss[i][k] /= sum;
    }
    for (int s = 0; s < SONG_COUNT; ++s) {
        g_compiled[s].bpm = k_songs[s].bpm;
        for (int c = 0; c < MUSIC_CH; ++c) {
            g_compiled[s].ch[c] = g_steps[s][c];
            g_compiled[s].len[c] = compile_channel(k_songs[s].ch[c], g_steps[s][c], STEP_CAP);
        }
        // every channel of a song must be the same length, or the parts drift apart
        int rows[MUSIC_CH];
        for (int c = 0; c < MUSIC_CH; ++c) {
            rows[c] = 0;
            for (int i = 0; i < g_compiled[s].len[c]; ++i) rows[c] += g_steps[s][c][i].rows;
        }
        for (int c = 1; c < MUSIC_CH; ++c)
            if (rows[c] != rows[0]) KV_WARN("sprocket", "song '%s': channel %d has %d rows, channel 0 has %d", k_songs[s].name, c, rows[c], rows[0]);
        // a song may only use the instruments its audio RAM upload holds (see bake_audio.py SONG_SETS)
        const SongSet* set = nullptr;
        for (int i = 0; i < k_song_set_count; ++i) {
            const char *a = k_song_sets[i].name, *b = k_songs[s].name;
            while (*a && *a == *b) { ++a; ++b; }
            if (*a == *b) set = &k_song_sets[i];
        }
        if (!set || set != &k_song_sets[s]) { KV_WARN("sprocket", "song '%s' has no instrument set in SongId order (bake_audio.py SONG_SETS)", k_songs[s].name); continue; }
        u64 bad = 0;
        for (int c = 0; c < MUSIC_CH; ++c)
            for (int i = 0; i < g_compiled[s].len[c]; ++i) {
                const Step& st = g_steps[s][c][i];
                if (st.kind == ST_INST || (st.kind == ST_NOTE && st.drum)) bad |= (1ull << st.value) & ~(set->samples | k_sfx_samples);
            }
        for (int i = 0; i < SMP_COUNT; ++i)
            if (bad >> i & 1) KV_WARN("sprocket", "song '%s' uses %s, which is not in its instrument set", k_songs[s].name, k_smp_labels[i]);
    }
    static_assert(SONG_COUNT == k_song_set_count, "every song needs an instrument set");
    // the effects and the engine may only use the resident samples
    for (int i = 0; i < SFX_COUNT; ++i)
        for (int layer = 0; layer < 2; ++layer) {
            const SfxStep* st = layer ? k_sfx[i].b : k_sfx[i].a;
            for (int k = 0; k < (layer ? k_sfx[i].nb : k_sfx[i].na); ++k)
                if (st[k].src < REST && !(k_sfx_samples >> st[k].src & 1)) KV_WARN("sprocket", "effect %d uses %s, which is not resident", i, k_smp_labels[st[k].src]);
        }
    static_assert(k_sfx_samples >> SMP_ENGINE & 1, "the engine sample must be resident");
}

bool snd_start(Engine& engine) {
    init_tables();
    AudioDeviceDesc d;
    d.rate = RATE;
    d.channels = 2;
    d.frames = 512;
    d.callback = audio_callback;
    g_open = platform_audio_open(engine, d);
    if (!g_open) KV_INFO("sprocket", "no audio device; running silent");
    return g_open;
}

void snd_stop(Engine& engine) {
    if (g_open) platform_audio_close(engine);
    g_open = false;
}

void snd_music(int song) { push_cmd((CMD_MUSIC << 16) | (song < 0 ? 0xffffu : (u32)song)); }
void snd_sfx(int id) { push_cmd((CMD_SFX << 16) | (u32)id); }
void snd_engine(bool on, f32 speed) {
    u32 bits;
    memcpy(&bits, &speed, sizeof bits);
    atomic_store(&g_engine_bits, bits, MO_RELAXED);
    atomic_store(&g_engine_on, on ? 1u : 0u, MO_RELAXED);
}
// The last-lap lift: while on, the song plays 12% faster (rows come sooner; pitch is untouched).
// It ends by itself when the music changes or stops, so a new song always starts at its own tempo.
void snd_volume(f32 gain) { g_master = gain < 0 ? 0 : (gain > 1 ? 1 : gain); }
void snd_final_lap(bool on) { push_cmd((CMD_FINAL_LAP << 16) | (on ? 1u : 0u)); }

} // namespace sr
