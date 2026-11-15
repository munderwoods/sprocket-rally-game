# Sprocket Rally

An original SNES-style Mode 7 kart racer on the Korvid engine. It really renders at
**256×224** (4:3) or **400×224** (16:9): the CPU draws every pixel into a tiny framebuffer,
which is uploaded once per tick and scaled to the window through a CRT shader.

## Build and run

```bash
. ~/games/korvid-env.sh
cd ~/games/sprocket
kormake sprocket dev        # name the module; a bare `kormake` builds the whole engine
build/linux-dev/bin/sprocket
```

## Controls

| | Keyboard | Gamepad |
|---|---|---|
| Steer | ← → | Left stick / D-pad |
| Gas / brake | Z or ↑ / X or ↓ | A / X (or triggers) |
| Hop, hold while turning to drift | Shift or C | R1 |
| Use item | Space or V | L1 / B |
| Menus | arrows, Enter, Esc | D-pad, A/Start, B |
| 4:3 / 16:9 | Tab or F1 | Back |
| Previous / next CRT shader | Q / E (or F2 / F3) | L3 / R3 |
| Window / fullscreen | F11 | |
| Pause (race) / quit (title) | Esc | Start |

**One to four players:** GRAND PRIX and SINGLE RACE both ask how many. Two share the screen
side by side or top and bottom (OPTIONS); three or four take a quarter each. Players one
and two have keys (arrows / WASD by default, see CONTROLS); gamepads go to the last
players first, so three and four need one each. The WHO USES WHAT screen in the
multiplayer setup lets each player choose instead: automatic, keyboard only, or one pad
by name (which is also how an analog keyboard's gamepad mode is used).

**TIME TRIAL:** alone on any hand-made track with three nitros, against the ghost of your
best run there. Ghosts are kept beside the records as `ghost_<track>.bin`.

**Options** (main menu, or the pause menu during a race): aspect, CRT shader, display
(window, borderless, fullscreen), which monitor, the split, and CONTROLS, where every key
and pad button can be rebound. They are kept in `settings.txt` beside the records.

Drift: hold hop and press a direction; the drift starts as soon as there is one, on landing
or after. Keep holding. Steering only bends the drift. Sparks
go blue, then orange, then purple; release for a mini-turbo (purple is the long one).

- **Flying start:** press the gas just after the second light. Hold it through the whole
  countdown and the engine floods.
- **Drafting:** sit close behind another kart for a second and you get a tow.
- **Bolts** raise your top speed (max 10). A hit knocks up to three out of you onto the
  road, where anyone can grab them for a few seconds.
- **Items:** Nitro, Triple Nitro, Bomb, Rocket (homing), Oil, Fake Crystal, Shield, EMP
  (shocks everyone ahead) and Magnet (pulls you to the kart ahead and steals its item).
  The back of the pack gets better ones.
- **Ramps** throw you over gaps; open edges and deep water mean a short wait for the
  rescue claw. Mud, syrup and tar are slow; ice and butter are slippery.

## What's in it

- **Mode 7 floor**: per-scanline affine sampling of a 1024×1024 map compiled at load time
  from a track description. There are two parallax sky layers, sprites scaled with nearest
  sampling, palette cycling for water and boost pads, and SNES 15-bit color.
- **Eight racers**, each with their own vehicle, portrait, stats and CPU personality,
  baked from low-poly 3D models to 24 angles of 32×32 indexed sprites (`tools/bake_art.py`).
- **Twelve hand-laid tracks**, each built round one idea and one big moving set piece:
  - *Sprocket Park*: a lakeside sweeper, a creek to jump, and a blimp dropping crystals.
  - *Dune Drift*: two level crossings and a run beside the rails, racing the train.
  - *Frost Ring*: sheet ice, a frozen lake, and a giant snowball rolling down the straight at you.
  - *Crown Keep*: over a drawbridge with no rails, round the keep, under a dragon.
  - *Gullwater Cove*: a pier across the lagoon with a gap in it, and a galleon's cannon fire.
  - *Fossil Falls*: leap the falls, ford the river, drive between a dinosaur's legs.
  - *Neon Sprawl*: square city corners, rooftop gaps, and cross traffic that does not stop.
  - *Breakfast Run*: tiny karts on a 1990s kitchen table: round the cereal box, over a
    waffle, off a spoon across the cereal bowl, under fire from the toaster.
  - *Sewer Surf*: a storm-drain channel where a wave rolls down the pipe; catch it and
    surf, or get swept.
  - *Circuit Breaker*: tiny karts on an 1980s home computer's board: up the alley of the
    RAM bank, round the processor, over the expansion cutout. The chips, sockets and
    copper are painted flat into the map; arcs cross half the road at a time, and there
    is a moth loose on the board.
  - *Orbit Run*: a road with no edges round the solar system, in low gravity, with
    asteroids, a comet, and small flames spat out of the sun (each lands on a marked spot).
  - *Graveyard Shift*: a haunted cemetery, with a ghost carriage on
    its own slow lap, ghosts that steal your item while the moon is hidden, and hands
    that come up out of the graves when it is out.
- **RANDOM tracks**: a new circuit for every seed, laid out on a grid so it is always
  valid, then given the same kinds of features as the hand-made ones.
- **Track authoring** (`src/track.cc`): a track is a spline through nodes with a width at
  each, a list of features along it (open edges, ice, mud, fords, boost pads, launch ramps
  and the gaps they clear, lane dividers, crystals, bolts, barrels) and terrain stamped
  round it (lakes, rivers, moats, canyons, walls).
- **Grand Prix**: three cups of four races, with the themes spread across them
  (Sprocket Cup: Park, Cove, Breakfast, Dune; Turbo Cup: Fossil, Keep, Circuit, Neon;
  Star Cup: Sewer, Frost, Graveyard, Orbit), points for every place, standings, a trophy ceremony, three
  difficulties, and records saved to `~/.local/share/sprocket-rally/records.txt`.
- **Nine items**, drifting with three tiers of mini-turbo (after SRB2Kart's model), a
  start boost, drafting, falling off open edges and into water, and a rescue claw.
- **7 CPU racers**: they brake for corners, avoid mud and barrels, dodge what the set
  piece puts in the road, drift through long bends and use items by personality.
- **CRT shaders**: ported from [libretro/slang-shaders](https://github.com/libretro/slang-shaders)
  into one combined shader (`shaders/sprocket_crt.slang`, one `crt_*.slangh` per port):
  Lottes (aperture grille and shadow mask), Lottes Fast, CRT Aperture, EasyMode,
  crt-geom, guest gdv-mini, CRT Consumer, Hyllian, zfast, crt-pi, Caligari and Mattias.
  Only single-pass shaders could be ported. The engine has no multipass chains, LUT
  textures or frame history, so crt-royale, guest-advanced and Cyclon are out. The
  original licenses are kept in each file. Most are GPL, Lottes is public domain, and
  Mattias has no stated license.
- **Sound** (`src/snes_audio.cc`) is an S-DSP-shaped synth:
  - 32 kHz output and **8 voices total** (5 music, 1 engine, 2 effects).
  - BRR-encoded samples decoded the way the SNES does it, with Gaussian-style
    interpolation, ADSR, the noise generator, and echo with an 8-tap FIR filter.
  - The bank and echo buffer fit in 64 KB of audio RAM.
  - The music is original, written in a small tracker notation in that file.
  - The instrument samples are baked into `src/audio_bank.h` by `tools/bake_audio.py`
    from SoundFont files, listed in a local table that is not part of the repository
    (the script's header describes it).

## Testing

- `SPROCKET_TRACK=4 build/linux-dev/bin/sprocket` jumps straight into a race on that
  track (0–11; 12 is a random one). `SPROCKET_CHAR=n` picks the racer and
  `SPROCKET_AUTOPILOT=1` makes your kart drive itself.
- **Headless harness** (`sprocket/tools/headless/`): the renderer is all software, so the
  game's own track, race and drawing code can run with no window. Build the game, then
  `tools/headless/build.sh`.
  - `headless map <track> out.ppm` writes a top-down map with scenery, items and paths.
  - `headless race <track> <char> <seconds> <every> <prefix>` runs an autopilot race flat
    out, writes a frame every `<every>` seconds (0: none), and prints lap times, falls and
    hits per racer. Track numbers past 11 are random seeds.
  - `tools/headless/build_ui_shots.sh && tools/headless/ui_shots <dir>` writes every menu
    screen at both widths.
- **The real game, off-screen:** run it inside a nested X display with software Vulkan and
  muted audio, and drive it with xdotool:
  `Xephyr :3 -screen 1300x1000 &` then
  `DISPLAY=:3 SDL_AUDIO_DRIVER=dummy VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json build/linux-dev/bin/sprocket`.
