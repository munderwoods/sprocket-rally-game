#!/usr/bin/env python3
"""Bakes Sprocket Rally's sample bank into src/audio_bank.h.

The instruments are taken from SoundFont (.sf2) files, except the engine's, which is
synthesized here. Each is BRR-encoded exactly as the SNES stores samples (9-byte
blocks of 16 4-bit samples with a shift and a prediction filter), so the game decodes the
same quantized data the SPC700 would play. Each song's instrument set must fit the 64 KB of
audio RAM alongside the effects' samples, the echo buffer and the driver.

Run: python3 tools/bake_audio.py [<dir holding the SoundFont files>] [--check-roots]
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sf2 import SF2

import math
from multiprocessing import Pool

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
CHECK_ROOTS = "--check-roots" in sys.argv
SRC = ARGS[0] if ARGS else os.path.expanduser(os.environ.get("SPROCKET_SF_DIR", "~/.local/src/sf2"))
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "audio_bank.h")

# Which SoundFont file, preset and (for drum kits) sample each instrument is taken from is
# kept in a local table that is not part of the repository: tools/audio_sources.local.json
#   {"fonts": {"A": "<path under the source dir>", ...},
#    "picks": {"<enum name>": ["<font key>", "<preset name>", "<drum sample name or null>"], ...}}
SRC_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audio_sources.local.json")
if not os.path.exists(SRC_TABLE):
    sys.exit("bake_audio: no tools/audio_sources.local.json (see the comment in this script); src/audio_bank.h is left as it is")
with open(SRC_TABLE) as _f: _SOURCES = json.load(_f)

# (enum name, the name shown in messages[, max rate])
# A max rate resamples the sample down (loop length kept whole, so it still loops cleanly):
# sources recorded at 44-58 kHz would otherwise waste audio RAM and run into the pitch
# register's 4x ceiling on high notes.
SAMPLES = [
    ("LEAD", "SYNTH LEAD 1"),
    ("GUITAR", "DISTORTED GUITAR 1"),
    ("SLAP", "SLAP BASS 1"),
    ("BRASS", "SYNTH BRASS 1"),
    ("STRINGS", "STRINGS 1"),
    ("SQUARE", "SQUARE WAVE 1"),
    ("EPIANO", "ELECTRIC PIANO 1"),
    ("ORCHHIT", "ORCHESTRA HIT 1"),
    ("SAW", "SAW WAVE 1"),
    ("OVERDRIVE", "DISTORTED GUITAR 2"),
    ("SYNBASS", "SYNTH BASS 1"),
    ("VIBES", "VIBRAPHONE 1"),
    ("GSYNTH", "SYNTH LEAD 2"),
    ("KICK", "KICK 1"),
    ("SNARE", "SNARE 1"),
    ("PSNARE", "SNARE 2"),
    ("HAT", "HI-HAT 1"),
    ("OHAT", "HI-HAT 2"),
    ("CRASH", "CRASH 1"),
    ("TOM", "TOM 1"),
    ("STOM", "TOM 2"),
    ("PORGAN", "PIPE ORGAN 1"),
    ("CHOIR", "CHOIR 1"),
    ("HARP", "HARP 1"),
    ("DBASS", "DOUBLE BASS 1"),
    ("TIMPANI", "TIMPANI 1"),
    ("ACCORD", "ACCORDION 1"),
    ("FLUTE", "FLUTE 1"),
    ("FIDDLE", "VIOLIN 1"),
    ("HORN", "HORNS 1"),
    ("PBASS", "PICKED BASS 1"),
    ("TAMB", "TAMBOURINE 1", 22050),
    ("CASTANET", "CASTANET 1", 16000),
    ("MARIMBA", "MARIMBA 1", 32000),
    ("PANFLUTE", "PAN FLUTE 1"),
    ("TRUMPET", "HORNS 2"),
    ("ABASS", "ACOUSTIC BASS 1"),
    ("BONGO", "BONGO 1"),
    ("JTOM", "TOM 3"),
    ("TAIKO", "TAIKO 1", 22050),
    ("SYNLEAD", "SYNTH LEAD 3"),
    ("PULSE", "PULSE WAVE 1"),
    ("MOOG", "SYNTH BASS 2"),
    ("CLAP", "CLAP 1", 24000),
    ("ENGINE", "ENGINE"),
]
LABEL = {s[0]: s[1] for s in SAMPLES}
# (enum name, font path, preset name, drum sample name or None[, max rate]); the engine's
# waveform is not from a font but built here (see synth_engine)
PICKS = []
for _s in SAMPLES:
    if _s[0] == "ENGINE": PICKS.append(("ENGINE", None, "engine", None)); continue
    _k, _preset, _samp = _SOURCES["picks"][_s[0]]
    PICKS.append((_s[0], os.path.join(SRC, _SOURCES["fonts"][_k]), _preset, _samp) + tuple(_s[2:]))


def synth_engine():
    """The kart engine's waveform: one loop of 2048 samples holding 32 cycles of a saw, 33
    cycles of a quieter second saw (so the pair beats once a loop: at engine pitches that is a
    lumpy two-to-eight beats a second that speeds up with the revs) and 16 cycles of a soft
    sub-octave. Every part is a whole number of cycles, so the loop is seamless; the harmonics
    stop at the 20th, so pitched far below its root it growls rather than buzzes.
    Returns (pcm, loop start, loop end, rate, root)."""
    n = 2048
    out = []
    for i in range(n):
        t = i / n
        v = 0.0
        for h in range(1, 21):
            roll = 1.0 / h * (1.0 if h < 12 else (21 - h) / 10.0)
            v += roll * math.sin(2 * math.pi * h * 32 * t)
            v += 0.45 * roll * math.sin(2 * math.pi * h * 33 * t + 0.7 * h)
        for h in (1, 2, 3):
            v += 0.55 / h * math.sin(2 * math.pi * h * 16 * t)
        out.append(v)
    peak = max(abs(v) for v in out)
    pcm = [int(round(v / peak * 12000)) for v in out]
    rate = 32000.0
    root = 69 + 12 * math.log2(rate / 64.0 / 440.0)  # the main saw: 64 samples a cycle
    return pcm, 0, n, rate, root

# Unpitched samples: played at their root by the drum tokens, so their pitch isn't checked.
DRUMS = {"ENGINE", "ORCHHIT", "KICK", "SNARE", "PSNARE", "HAT", "OHAT", "CRASH", "TOM", "STOM",
         "TAMB", "CASTANET", "BONGO", "JTOM", "TAIKO", "CLAP"}

# Roots measured from the samples themselves (autocorrelation of the loop, or of the body of
# a one-shot; `--check-roots` re-measures every melodic sample) where the font's root
# key is wrong, almost always by an octave; the font's value is used otherwise.
ROOT_FIX = {
    "SLAP": 39.27, "BRASS": 69.20, "SYNBASS": 35.02, "VIBES": 92.42,
    "PORGAN": 60.09,    # listed 72
    "CHOIR": 53.31,     # listed 41.3
    "DBASS": 48.00,     # listed 60
    "TIMPANI": 57.84,   # listed 70
    "FLUTE": 72.00,     # listed 60
    "PBASS": 60.00,     # listed 72
    "PANFLUTE": 72.09,  # listed 60
    "ABASS": 48.00,     # listed 60
    "MOOG": 47.76,      # listed 60
    # A lead that is a fifth of a semitone flat against its bass is out of tune, so the ones that
    # were merely close are pinned to what their loops actually play as well:
    "LEAD": 85.41,      # listed 85.53
    "SQUARE": 66.86,    # listed 67.42 (the loop is 33 samples, not 32)
    "EPIANO": 60.54,    # listed 60.73
    "OVERDRIVE": 72.06, # listed 72.44 (a 49-sample loop)
}

# ---- audio RAM, per song ------------------------------------------------------------------
# On the SNES each song uploads its own instruments to the 64 KB of audio RAM, next to the
# driver, the echo buffer and the sound effects' samples (which stay resident). The game
# keeps one global sample list but each song may only use its own set.
SONG_ORDER = ["TITLE", "SPROCKET PARK", "DUNE DRIFT", "FROST RING", "CROWN KEEP",
              "GULLWATER COVE", "FOSSIL FALLS", "NEON SPRAWL", "RESULTS",
              "MENU", "FINISH", "TROPHY", "BREAKFAST RUN",
              "SEWER", "CIRCUIT", "SPACE", "HAUNT"]  # = enum SongId
# Resident for every song: the effects' samples (songs may play them too) and the engine.
SFX_SAMPLES = ["SQUARE", "VIBES", "SAW", "GSYNTH", "ORCHHIT", "KICK", "BRASS", "STRINGS", "ENGINE"]
_KIT = ["KICK", "SNARE", "HAT", "OHAT", "CRASH"]
SONG_SETS = {
    "TITLE": ["LEAD", "BRASS", "SLAP", "TOM"] + _KIT,
    "SPROCKET PARK": ["LEAD", "BRASS", "SLAP", "TOM"] + _KIT,
    "DUNE DRIFT": ["OVERDRIVE", "SYNBASS", "TRUMPET", "TOM", "STOM"] + _KIT,
    "FROST RING": ["GSYNTH", "EPIANO", "SLAP", "TOM"] + _KIT,
    "CROWN KEEP": ["PORGAN", "CHOIR", "HARP", "STRINGS", "DBASS", "TIMPANI", "ORCHHIT"] + _KIT,
    "GULLWATER COVE": ["ACCORD", "FLUTE", "FIDDLE", "HORN", "PBASS", "TAMB", "CASTANET"] + _KIT,
    "FOSSIL FALLS": ["MARIMBA", "PANFLUTE", "TRUMPET", "ABASS", "BONGO", "JTOM", "TAIKO", "CASTANET"] + _KIT,
    "NEON SPRAWL": ["SYNLEAD", "PULSE", "SAW", "MOOG", "PSNARE", "CLAP"] + _KIT,
    "RESULTS": ["BRASS", "STRINGS", "SLAP"] + _KIT,
    "MENU": ["EPIANO", "SLAP"] + _KIT,
    "FINISH": ["TRUMPET", "BRASS", "SLAP"] + _KIT,
    "TROPHY": ["TRUMPET", "STRINGS", "SLAP", "TOM"] + _KIT,
    "BREAKFAST RUN": ["LEAD", "EPIANO", "SLAP", "TOM"] + _KIT,
    "SEWER": ["GUITAR", "PORGAN", "PBASS", "TOM"] + _KIT,
    "CIRCUIT": ["PULSE", "SYNBASS", "STOM", "CLAP"] + _KIT,
    "SPACE": ["SYNLEAD", "MOOG", "TOM", "STOM"] + _KIT,
    "HAUNT": ["FLUTE", "HARP", "DBASS", "TAMB", "CASTANET"] + _KIT,
}

ARAM = 65536
ECHO_BYTES = 4 * 2048  # EDL 4
DRIVER_BYTES = 2048


def measure_root(pcm, ls, le, loop, rate):
    """MIDI pitch of a sample by normalized autocorrelation (first strong peak)."""
    if loop and le - ls >= 16:
        lp = list(pcm[ls:le])
        seg = (lp * (1 + 4096 // len(lp)))[:4096]
    else:
        a = min(len(pcm) // 4, 2000)
        seg = list(pcm[a:a + 4096])
    n = len(seg)
    mean = sum(seg) / n
    seg = [x - mean for x in seg]
    minlag, maxlag = max(2, int(rate / 2500)), min(n // 2, int(rate / 25))
    w = n - maxlag
    e0 = sum(x * x for x in seg[:w]) or 1
    nacf = [0.0] * (maxlag + 2)
    for lag in range(minlag, maxlag + 1):
        s = e1 = 0.0
        for i in range(w):
            y = seg[i + lag]
            s += seg[i] * y
            e1 += y * y
        nacf[lag] = s / math.sqrt(e0 * (e1 or 1))
    best = max(range(minlag, maxlag + 1), key=lambda l: nacf[l])
    lag = best
    for l in range(minlag + 1, maxlag):
        if nacf[l] >= 0.88 * nacf[best] and nacf[l] >= nacf[l - 1] and nacf[l] >= nacf[l + 1]:
            lag = l
            break
    a, b, c = nacf[lag - 1], nacf[lag], nacf[lag + 1]
    d = a - 2 * b + c
    period = lag + (0.5 * (a - c) / d if d else 0)
    return 69 + 12 * math.log2(rate / period / 440.0)


def resample(pcm, ls, le, loop, rate, target):
    """Windowed-sinc downsample to <= target Hz. Returns (pcm, ls, le, rate)."""
    if rate <= target:
        return pcm, ls, le, rate
    f = target / rate
    if loop:
        nl = max(16, int(round((le - ls) * f)))
        f = nl / (le - ls)
        nls = int(round(ls * f))
        nle = nls + nl
        n_out = nle
    else:
        n_out = int(len(pcm) * f)
    fc = 0.9 * f  # cutoff, relative to the source rate
    W = int(math.ceil(6 / f))

    def src(k):
        if k < 0:
            return 0
        if loop and k >= le:
            k = ls + (k - ls) % (le - ls)
        return pcm[k] if k < len(pcm) else 0
    out = []
    for j in range(n_out):
        t = j / f
        k0 = int(t)
        acc = 0.0
        for k in range(k0 - W, k0 + W + 1):
            x = (t - k) * fc
            sinc = 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)
            win = 0.5 + 0.5 * math.cos(math.pi * (t - k) / (W + 1))
            acc += src(k) * fc * sinc * win
        out.append(max(-16384, min(16383, int(round(acc)))))
    if loop:
        return out, nls, nle, rate * f
    return out, -1, n_out, rate * f


def clamp16(v):
    return -32768 if v < -32768 else (32767 if v > 32767 else v)


def brr_decode_nibble(n, shift, filt, p1, p2):
    s = (n << shift) >> 1 if shift <= 12 else (-2048 if n < 0 else 0)
    if filt == 1:
        s += p1 + ((-p1) >> 4)
    elif filt == 2:
        s += (p1 << 1) + ((-((p1 << 1) + p1)) >> 5) - p2 + (p2 >> 4)
    elif filt == 3:
        s += (p1 << 1) + ((-(p1 + (p1 << 2) + (p1 << 3))) >> 6) - p2 + (((p2 << 1) + p2) >> 4)
    s = clamp16(s)
    s = ((s << 1) & 0xFFFF)
    if s & 0x8000:
        s -= 0x10000
    return s >> 1  # 15-bit wrap, as the DSP does


def brr_encode(pcm):
    """pcm: 15-bit range samples, len multiple of 16. Returns (bytes, decoded)."""
    out = bytearray()
    dec = []
    p1 = p2 = 0
    nblocks = len(pcm) // 16
    for b in range(nblocks):
        block = pcm[b * 16:(b + 1) * 16]
        best = None
        for filt in range(4):
            if b == 0 and filt:
                continue
            for shift in range(13):
                q1, q2 = p1, p2
                err = 0
                nibs = []
                ds = []
                for x in block:
                    # predict, then choose the nibble that decodes closest
                    bestn, beste, bests = 0, None, 0
                    for n in range(-8, 8):
                        s = brr_decode_nibble(n, shift, filt, q1, q2)
                        e = (s - x) * (s - x)
                        if beste is None or e < beste:
                            bestn, beste, bests = n, e, s
                    err += beste
                    nibs.append(bestn); ds.append(bests)
                    q2, q1 = q1, bests
                    if best is not None and err >= best[0]:
                        break
                if len(nibs) == 16 and (best is None or err < best[0]):
                    best = (err, filt, shift, nibs, ds)
        err, filt, shift, nibs, ds = best
        last = b == nblocks - 1
        out.append((shift << 4) | (filt << 2) | (1 if last else 0))
        for i in range(0, 16, 2):
            out.append(((nibs[i] & 15) << 4) | (nibs[i + 1] & 15))
        dec.extend(ds)
        p2, p1 = ds[-2], ds[-1]
    return bytes(out), dec


def prepare(pick):
    name, path, preset, sample = pick[:4]
    maxrate = pick[4] if len(pick) > 4 else None
    if path is None:
        pcm, ls, le, rate, root = synth_engine()
        data, dec = brr_encode(pcm)
        return (name, data, len(pcm), ls, le, rate, root, 0.0, root, None)
    sf = SF2(path)
    pr = next(p for p in sf.presets() if p["name"] == preset)
    zones = pr["zones"]
    z = next(z for z in zones if z["sample"] == sample) if sample else min(zones, key=lambda z: abs((z["lo"] + z["hi"]) / 2 - 60))
    pcm = [v >> 1 for v in z["pcm"]]
    loop = z["loop"]
    ls, le = (z["ls"], z["le"]) if loop else (-1, len(pcm))
    if loop:
        pcm = pcm[:le]  # nothing after the loop end is ever heard
    rate = z["rate"]
    if maxrate:
        pcm, ls, le, rate = resample(pcm, ls, le, loop, rate, maxrate)
    listed = z["root"] - z["tune"] / 100.0
    measured = None
    if CHECK_ROOTS and name not in DRUMS:
        measured = measure_root(pcm, ls, le, loop, rate)
    pad = (-len(pcm)) % 16
    pcm = pcm + [0] * pad
    data, dec = brr_encode(pcm)
    root = ROOT_FIX.get(name, listed)
    return (name, data, len(pcm), ls, le, rate, root, z["atten"], listed, measured)


def main():
    with Pool() as pool:
        res = pool.map(prepare, PICKS)
    insts = []
    blob = bytearray()
    sizes = {}
    bad_roots = []
    for (name, data, n, ls, le, rate, root, att, listed, measured) in res:
        insts.append((name, len(blob), n, ls, le, rate, root, att))
        sizes[name] = len(data)
        blob += data
        m = "" if measured is None else "  measured %.2f (listed %.2f)" % (measured, listed)
        print("%-10s %6d samples %6d bytes  root %.2f  %5d Hz  %s%s" % (name, n, len(data), root, rate, "loop %d-%d" % (ls, le) if ls >= 0 else "one-shot", m))
        if measured is not None and abs(measured - root) > 0.6:
            bad_roots.append("%s: root %.2f but measured %.2f" % (name, root, measured))
    for b in bad_roots:
        print("ROOT MISMATCH", b)
    assert not bad_roots, "fix ROOT_FIX"
    total = len(blob)
    print("bank: %d bytes of BRR in all (the game holds every song's set; the SNES loads one at a time)" % total)

    # every song's instruments, plus the resident effect samples, the echo buffer and the driver
    names = [i[0] for i in insts]
    assert len(names) <= 64, "song sets are 64-bit masks"
    assert list(SONG_SETS) == SONG_ORDER, "SONG_SETS must list every song, in SongId order"
    sfx = set(SFX_SAMPLES)
    for song in SONG_ORDER:
        for s in SONG_SETS[song]:
            assert s in sizes, "%s: unknown sample %s" % (song, s)
        resident = sfx | set(SONG_SETS[song])
        used = sum(sizes[s] for s in resident) + ECHO_BYTES + DRIVER_BYTES
        print("  %-15s %2d samples  %5d bytes of BRR + echo %d + driver %d = %5d / %d" % (
            song, len(resident), used - ECHO_BYTES - DRIVER_BYTES, ECHO_BYTES, DRIVER_BYTES, used, ARAM))
        assert used <= ARAM, "song %s does not fit audio RAM" % song

    with open(OUT, "w") as f:
        f.write("// Generated by tools/bake_audio.py. Do not edit.\n#pragma once\n#include \"korvid/core/types.h\"\n\nnamespace sr {\n\n")
        f.write("enum SampleId : int {\n" + "".join("    SMP_%s,\n" % i[0] for i in insts) + "    SMP_COUNT\n};\n\n")
        f.write("// the short names the songs' notation picks instruments by (\"@LEAD\"), and the fuller ones for messages\n")
        f.write("inline const char* const k_smp_names[SMP_COUNT] = {\n    " + ", ".join('"%s"' % n for n in names) + "};\n")
        f.write("inline const char* const k_smp_labels[SMP_COUNT] = {\n    " + ", ".join('"%s"' % LABEL[n] for n in names) + "};\n\n")
        f.write("struct BankSample { kv::u32 brr_offset, samples; kv::i32 loop_start; kv::u32 loop_end; kv::f32 rate, root, atten_db; };\n\n")
        f.write("inline const BankSample k_bank[SMP_COUNT] = {\n")
        for (name, off, n, ls, le, rate, root, att) in insts:
            f.write("    {%d, %d, %d, %d, %.1ff, %.3ff, %.1ff}, // %s\n" % (off, n, ls, le, rate, root, att, name))
        f.write("};\n")
        f.write("constexpr kv::u32 k_bank_pcm_total = %d; // decoded samples, all instruments\n\n" % sum(i[2] for i in insts))
        f.write("// Each song's instrument set (a bit per SampleId), in SongId order; the sound effects'\n// samples are resident for every song.\n")
        f.write("constexpr int k_song_set_count = %d;\n" % len(SONG_ORDER))
        f.write("struct SongSet { const char* name; kv::u64 samples; };\n")
        f.write("inline const SongSet k_song_sets[%d] = {\n" % len(SONG_ORDER))
        for song in SONG_ORDER:
            mask = 0
            for s in SONG_SETS[song]:
                mask |= 1 << names.index(s)
            f.write('    {"%s", 0x%016xull},\n' % (song, mask))
        f.write("};\n")
        mask = 0
        for s in SFX_SAMPLES:
            mask |= 1 << names.index(s)
        f.write("constexpr kv::u64 k_sfx_samples = 0x%016xull;\n\n" % mask)
        f.write("inline const kv::u8 k_brr[%d] = {\n" % total)
        for i in range(0, total, 24):
            f.write(",".join(str(b) for b in blob[i:i + 24]) + ",\n")
        f.write("};\n\n} // namespace sr\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
