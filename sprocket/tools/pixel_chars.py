#!/usr/bin/env python3
"""Pixel-art character trial for Sprocket Rally (NOT wired into the game).

Hand-authored, outline-free pixel art for one racer -- Bruno Wrench, the bulldog mechanic in
the orange tow truck -- as a trial of replacing the pre-rendered 3D karts in src/art.h:

  * 24 in-race view angles, 32x32, same scale / framing / ground anchor (16,25) and the same
    angle convention as bake_art.py: frame 0 is seen from directly behind, frame 6 has the
    nose pointing screen-right, frame 12 faces the camera.
  * a 32x32 character-select portrait (the one part that IS in the game: its pixels live in
    tools/portraits.py with the other racers' portraits and are baked by bake_art.py).

Everything is explicit pixel data typed in below (one character per pixel, see PALETTE).
Views 0..12 are drawn; views 13..23 are mirror images of 11..1 (the truck, the crane on its
centre line and the driver are all left/right symmetric, and the light comes from above).
Each view is a truck cel (BODY) with a separately drawn head cel (HEAD) laid on top at an
explicit position, which is what keeps the head from jittering through the rotation.

Proportions were blocked in from a turntable guide of key points (box corners, wheel centres)
worked out separately; every pixel and every colour choice below was then placed by hand.
Nothing here renders 3D geometry, and there is no ink outline: forms are separated by value
(light top planes, mid camera-facing planes, dark undersides / tub / tyres).

Run:  python3 tools/pixel_chars.py [OUTDIR]
Needs pillow only. It reads src/art.h (read-only) purely to put the current baked sprites
next to the new ones in the comparison sheets; it writes nothing inside the game tree.
"""
import os, re, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ART_H = os.path.join(HERE, "..", "src", "art.h")
DEFAULT_OUT = "/tmp/claude-1000/-home-matt-games/27855b4e-bfb8-42fa-860d-a1f2554135c1/scratchpad/pixel_trial"

SPR, N_ANGLES = 32, 24
ROAD, GRASS = (96, 96, 104), (72, 152, 72)

# 16 colours including transparent; every channel is a multiple of 8 (15-bit SNES colour).
PALETTE = [
    (".", None),                # 0  transparent
    ("o", (255, 184, 96)),      # 1  orange, top planes
    ("O", (240, 128, 48)),      # 2  orange, camera-facing planes
    ("r", (192, 80, 32)),       # 3  orange, planes turning away
    ("R", (112, 48, 40)),       # 4  rust shadow: tub interior, underside, wheel wells
    ("y", (255, 224, 104)),     # 5  crane yellow / lamps
    ("Y", (208, 144, 40)),      # 6  crane shade
    ("k", (40, 40, 56)),        # 7  tyre, eyes, nose
    ("g", (80, 80, 104)),       # 8  tyre tread light / steel shade
    ("w", (208, 208, 224)),     # 9  steel light: hubs, bumpers, hook, teeth
    ("f", (248, 216, 168)),     # 10 fur light
    ("F", (216, 160, 104)),     # 11 fur mid
    ("e", (128, 80, 56)),       # 12 ears / fur shadow
    ("b", (104, 160, 248)),     # 13 cap + overalls light
    ("B", (56, 96, 200)),       # 14 cap + overalls mid
    ("n", (32, 48, 128)),       # 15 cap + overalls shade
]
IDX = {c: i for i, (c, _) in enumerate(PALETTE)}
RGB = [rgb for _, rgb in PALETTE]

# ================================================================================================
# Truck cels: BODY[k] = (first row, rows). Rows above/below are empty.
# ================================================================================================
BODY = {}
HEAD = {}

#            01234567890123456789012345678901
BODY[0] = (12, [
            "..........oooooooooooo..........",   # 12 hood top
            "..........oooooooooooo..........",
            "..........oooooooooooo..........",
            "......ggg.oooooooooooo.ggg......",   # 15 front tyres
            "......kgkkOOOOOOOOOOOOkkgk......",
            "......kgkkOOnBByYBBnOOkkgk......",   # 17 overalls, boom
            "......kkkOORnBByYBBnROOkkk......",
            "......kkkOORnyyyyyynROOkkk......",   # 19 crane base top
            ".....ggggOORnyywwyynROOgggg.....",   # 20 hook; rear tyres
            ".....kkkkooooYYggYYooookkkk.....",   # 21 bed top
            ".....kgkkooooYYYYYYooookkgk.....",
            ".....kgkkooooooooooooookkgk.....",
            ".....kgkkOOOOOOOOOOOOOOkkgk.....",   # 24 tailgate
            ".....kgkkOyOOOOOOOOOOyOkkgk.....",
            ".....kgkkOOOOOOOOOOOOOOkkgk.....",
            ".....kkkkrrrrrrrrrrrrrrkkkk.....",
            ".....kkkk.wwwwwwwwwwww.kkkk.....",   # 28 bumper
            "......kk..gggggggggggg..kk......",
])
HEAD[0] = (11, 8, [
            "..bbbbbb..",
            ".bbbbbbbb.",
            ".BBBBBBBB.",
            "eBBBnnBBBe",
            "eeFFFFFFee",
            "eeFFFFFFee",
            "eeFFFFFFee",
            ".eFFFFFFe.",
            "..nBBBBn..",
])

#            01234567890123456789012345678901
BODY[6] = (10, [
            ".yy.............................",   # 10 boom tip
            ".wyY............................",
            ".w.yY...........................",
            ".w..yY.ggg......................",   # 13 far rear tyre
            "ww...yYkkkk.....................",
            ".w.kkkyYkkk..........ooooooooo..",   # 15 hood top
            ".ooooyyyyyoBBBB......oooooooooO.",   # 16 bed top
            ".ooooYYYYYoBBBBFFrrrroooooooooO.",
            ".ooooooooorBBBBnFFkrroooooooooO.",
            ".ooooooooorRRRRRRRkRRoooooooooO.",
            ".OOOkkkkkOrRRRRRRRRRROOOOOOOOOy.",   # 20 near side
            ".OOkkkkkkkOOOOOOOOOOOOOOOOOOOOy.",
            ".OkkkkkkkkkOOOOOOOOOOOOkkkkOOOw.",
            ".rkkkgggkkkOOOOOOOOOOOkkkkkkOwww",
            ".rkkgwwwgkkrrrrrrrrrrkkkggkkkwww",
            ".rkkgwwwgkkrrrrrrrrrrkkgwwgkkggg",
            "..kkgwwwgkkRRRRRRRRRRkkgwwgkk...",
            "..kkkgggkkk..........kkkggkkk...",
            "...kkkkkkk............kkkkkk....",
            "....kkkkk..............kkkk.....",
])
HEAD[6] = (7, 8, [
            "....bbbbb....",
            "...bbbbbbbb..",
            "...BBBBBBbbbb",
            "...BBBBBnnnnn",
            "...FFeeeffkf.",
            "...FFeeeffffk",
            "...FFeeeFffff",
            "....FFeeFFff.",
            ".....nBBn....",
])

#             01234567890123456789012345678901
BODY[12] = (5, [
            "...............yy...............",   # 5 boom behind the cap
            "...............yY...............",
            "...............yY...............",
            "................................",
            "................................",
            "................................",
            "................................",
            ".....gggg..............gggg.....",   # 12 rear tyres
            ".....kkkkooooooooooooookkkk.....",   # 13 bed top
            ".....kgkkooooooooooooookkgk.....",
            ".....kgkkoooooooooooooookgk.....",
            ".....kgkkOrrFFBBBBFFrrOkkgk.....",   # 16 tub, driver
            ".....kkkkORRRFFBBFFRRROkkkk.....",
            ".....kkkkORRRRFkkFRRRROkkkk.....",
            ".....kkkkORRRRRkkRRRRROkkkk.....",
            "......ggg.oooooooooooo.ggg......",   # 20 hood top
            "......kkk.oooooooooooo.kkk......",
            "......kkk.oooooooooooo.kkk......",   # 22 front tyres
            "......kkkkooooooooooookkkk......",
            "......kgkkyyOOOOOOOOyykkgk......",   # 24 lamps
            "......kgkkyywwwwwwwwyykkgk......",
            "......kgkkOOgwgwgwgwOOkkgk......",   # 26 grille
            "......kgkkOOgwgwgwgwOOkkgk......",
            "......kkwwwwwwwwwwwwwwwwkk......",   # 28 bumper
            ".......kggggggggggggggggk.......",
])
HEAD[12] = (11, 7, [
            "..bbbbbb..",
            ".bbbbbbbb.",
            ".bbbbbbbb.",
            "BBBBBBBBBB",
            "eFFFFFFFFe",
            "eFkffffkFe",
            "eFffkkffFe",
            "effffffffe",
            ".ffFFFFff.",
            "..nBBBBn..",
])

#            01234567890123456789012345678901
BODY[3] = (12, [
            ".....................ooo........",   # 12 hood top
            "...................ooooooo......",
            "................oooooooooooo....",
            ".....yy.........roooooooooooooo.",   # 15 boom tip
            "....wyy....BBBBBFFkroooooooooooo",
            ".gg.w.yY..BBBBBBBnrrrrooooooooOO",
            "kkkww.oyY.BBBBBBBnRRRrrrooOOOOO.",   # 18 bed top starts
            "kkkoooooyYoBBBBBBnRRRRRrrOOgggg.",
            "koooooooyyyYoBBBBnRRRRRROOkkkkkk",
            "krooooooYYYYYooBBnRRROOOOkkkkwwk",
            "krrroooooooooooggggOOOOOOkkkkwwk",   # 22 rear tyre top
            "krrrrrrrroooogkkkkkkOOOOOkkkkkkk",
            ".rrrrrrrrrrrggkkkggkkOOOrkkkkkkk",
            ".wwrrrrrrrrggkkkgwwkkr.....kkkk.",
            "...wwrrrrrrggkkkgwwkk...........",
            ".....wwwrrrggkkkgwwkk...........",
            "........wwrggkkkkggkk...........",
            "..........wwgkkkkkkkk...........",
            "..............kkkkk.............",
])
HEAD[3] = (9, 8, [
            "...bbbbb....",
            "..bbbbbbbn..",
            "..BBBBBBBnn.",
            ".eBBBBBBBnn.",
            ".eFFFFFeeffk",
            ".eFFFFFeefff",
            "..FFFFFeeff.",
            "..FFFFFFeF..",
            "...nBBBn....",
])

#            01234567890123456789012345678901
BODY[9] = (7, [
            ".....yy.........................",   # 7 boom tip
            "....gyY.........................",
            "....g.yY........................",
            "....w.yY........................",
            "...ww.yY........................",
            ".......yY.o.....................",
            ".......yyyyooo..................",   # 13 far rear tyre
            ".....oyyyBBBBBBB...gg...........",
            "...ooooYYnBBBBBBB..kkk..........",
            ".oooooooonnBBBBBBB.kkkk.........",
            ".ggggggoonnnBFFFBBFkkkk.........",   # 17 near rear tyre top
            "kkkkkkkggnnnBBBBFFFkkOOooo......",
            "kkkggkkkgknnBBBBBBBkoooooooo....",
            "kkgwwkkkgkRnBBRRRRoooooooooooo..",
            "kkgwwkkkgkoRRRRRoooooooooooyyO..",
            "kkgwwkkkgkOoRRRROOoooooooOOOyy..",
            "kkkggkkkgkOOooRROOOOOooOOwgwgO..",
            "kkkkkkkkgkrOOOggggOOOOyywgwgwOww",   # 24 front tyre top, near lamp
            ".kkkkkkkgk.rOkkkkkkkOOyywgwgOwww",
            "....kkkkk...rkgwwkkgkOOwgwwwggg.",
            ".............kgwwkkgkrrwwwgg....",
            ".............kkkkkkgkwwggg......",
            "..............kkkkkkkgg.........",
            "...............kkkkk............",
])
HEAD[9] = (7, 7, [
            "....bbbbb....",
            "...bbbbbbbb..",
            "...BBbbbbbbb.",
            "...BBBBBBBBBB",
            "..eeFFFFFFFe.",
            "..eeeFkffkFf.",
            "..eeeFfffkkf.",
            "..eeeFffffff.",
            "...eeFFfFFf..",
            ".....nBBn....",
])

#            01234567890123456789012345678901
BODY[1] = (12, [
            ".............oooooooo...........",   # 12 hood top
            "............ooooooooooooo.......",
            ".........gg.ooooooooooooor......",
            "........kkkoooooooooooooorr.....",
            "........kkkbbbbbbbbnooorgggg....",
            "........kkByYBBBBBnnOOOrkkkk....",
            "........kkOyYBBBBBnnRRRrkkkk....",
            "...gg...kOyyyYyyBBnnRRrrkkkk....",
            "...kkk.oooywyYyyBBnggggrkkkk....",
            "...kkk.oooYYYYYYookgkkkkkkkk....",
            "...kkkooooYYYYYYookgkkkkkkk.....",
            "...kkkooooooooooookgkkkk........",
            "...kkOOOOOOOooooookgkkkk........",
            "...kkOyOOOOOOOOOyOkgkkkk........",
            "...kkOOOOOOOOOOOOOkgkkkk........",
            "...kkrwwwwwwrrrrrrkgkkkk........",
            "....k.ggggggwwwwwwkgkkkk........",
            "............ggggggkkkkkk........",
            "...................kkkk.........",
])
HEAD[1] = (11, 8, [
            "..bbbbbb...",
            ".bbbbbbbb..",
            ".BBBBBBBB..",
            "eBBBBnBBBe.",
            "eeFFFFFFFee",
            "eeFFFFFFFee",
            ".eFFFFFFFee",
            ".eFFFFFFFe.",
            "..nBBBBn...",
])

#            01234567890123456789012345678901
BODY[2] = (12, [
            "................ooooo...........",
            "...............oooooooooo.......",
            "...........ggooooooooooooooo....",
            "..........kkkOOOOooooooooorrr...",
            "........yykbbbbbbbnnOoooorggg...",
            ".......gyYBBBBBBnnnOOOOOrkkkkk..",
            "..gg...g.yYBBBBBnnnRROOOkkkkkk..",
            ".kkkk.owyyYyyBBBnnnRRRRrkkkwwk..",
            ".kkkkowooyyyyyBBnnnRRRrrkkkwwk..",
            ".kkooooooYYYYYooogggg"+"rr"+"kkkkwwk..",
            ".kkOOooooYYYYYooggkkkkkrkkkkkkk.",
            ".kkOOOOOOoooookgkkkkkk..kkkkk...",
            ".kkOyOOOOOOOyokgkkwwkk..........",
            ".kkOOOOOOOOOOOkgkkwwkk..........",
            "..krwwwwOOOOOOkgkkwwkk..........",
            "....ggggwwwwrrkgkkkkkk..........",
            "........ggggwwkgkkkkkk..........",
            "............gggkkkkkkk..........",
            ".................kkkk...........",
])
HEAD[2] = (9, 8, [
            "...bbbbbb...",
            "..bbbbbbbb..",
            "..BBBBBBBBn.",
            ".eBBBBBBBBn.",
            ".eFFFFFFFeef",
            ".eFFFFFFFeef",
            "..FFFFFFFeef",
            "..FFFFFFFee.",
            "...nBBBBn...",
])

#            01234567890123456789012345678901
BODY[4] = (13, [
            "......................oooo......",
            "..yy..............ooooooooo.....",
            "..gyY.............roooooooooo...",
            "..g.yY...nnnbbbbFFkroooooooooo..",
            "..w..yyyynnnBBBBBFkrrrooooooooO.",
            ".wwooyyyyynnBBBBBBRRRrroooOOOOO.",
            "rooooYYYYYYnBBBBBBRRRRRrOOggggO.",
            "rroooYYYYggggBBBBBRRRRRRkkkkkkk.",
            "rrrrooooggkkkkkkBBRRRROgkkkkkkk.",
            "rrrrrooggkkkkkkkkRROOOOgkkkwwkk.",
            "wwrrrrrggkkkggkkkOOOOOOgkkkwwkk.",
            "ggwwrrrggkkgwwgkkOOOOOrgkkkkkkk.",
            "..ggwwrggkkgwwgkkrrrr...kkkkkkk.",
            "....ggwggkkgwwgkk.........kkkk..",
            "......ggkkkkggkkk...............",
            "........kkkkkkkkk...............",
            ".........kkkkkkk................",
            "...........kkkk.................",
])
HEAD[4] = (8, 8, [
            "....bbbbb....",
            "...bbbbbbbb..",
            "...BBBBBBBbbn",
            "..eBBBBBnnnn.",
            "..eFFFFeefkf.",
            "...FFFFeeeffk",
            "...FFFFeeefff",
            "....FFFFeFff.",
            ".....nBBn....",
])

#            01234567890123456789012345678901
BODY[5] = (12, [
            ".yy.............................",
            "gyY.............................",
            "g.yYgggg...............ooooo....",
            "gkkyYkkkk...........ooooooooo...",
            "wwkkyYyyynnbbbbbFFFkrooooooooo..",
            "wwooyyyyynnBBBBFFFFkkroooooooo..",
            "roooyyyyynnBBBBBBRRkRrooooooooo.",
            "rrooYYYggggBBBBBBRRRRRrooOOOOOO.",
            "rrroYggkkkkkkBBBBRRRRRROOggggOO.",
            "rrrrggkkkkkkkkBBBRRRRROgkkkkkkk.",
            "wrrrggkkkkkkkkkOOOOOOOgkkkkkkkk.",
            "gwrrggkkkgggkkkOOOOOOOgkkgwwgkk.",
            ".gwrggkkgwwwgkkOOOOOOOgkkgwwgkk.",
            "..gwggkkgwwwgkkrrrrrrrgkkgwwgkk.",
            "...gggkkgwwwgkk.......gkkkkkkkk.",
            "....gkkkkgggkkk........kkkkkkk..",
            ".....kkkkkkkkk...........kkkk...",
            "......kkkkkkk...................",
            "........kkkk....................",
])
HEAD[5] = (7, 8, [
            "....bbbbb....",
            "...bbbbbbbb..",
            "...BBBBBBbbbb",
            "...BBBBBnnnnn",
            "...FFFeeffkf.",
            "...FFFeeefffk",
            "...FFFeeeffff",
            "....FFFeeFff.",
            ".....nBBn....",
])

#            01234567890123456789012345678901
BODY[7] = (9, [
            ".yy.............................",
            "gyY.............................",
            "g.yY............................",
            "g..yY.gg........................",
            "ww.yYkkkk.......................",
            "w...yYykk.......................",
            "...oyYyyyoo.....................",
            "..ooyyyyybbbbbBB......ooooo.....",
            "..ooYYYYYBBBBBBFFFFkkOooooooooo.",
            ".ooggggggBBBBBBrrrrkkoooooooooy.",
            "okkkkkkkggBBBBBRRRRRRoooooooooy.",
            "kkkkkkkkkggBBBBRRRRRooooooooorrw",
            "kkkkgggkkggRRRRRRRROOOOOOoooorrw",
            "kkkgwwwgkggOOOOORRROOgggggOOyrrw",
            "kkkgwwwgkggOOOOOOOOkkkkkkkgOyrwg",
            "kkkgwwwgkgkOOOOOOOkkkkkkkkgOrwg.",
            "kkkkgggkkkkrrrrrrrkkgwwgkkkrwg..",
            "kkkkkkkkkkk.......kkgwwgkkkwg...",
            ".kkkkkkkkk........kkkkkkkkk.....",
            "...kkkkk...........kkkkkkk......",
            "....................kkkkk.......",
])
HEAD[7] = (7, 7, [
            "....bbbbb....",
            "...bbbbbbbb..",
            "...BBBbbbbbb.",
            "...BBBBBBBBBB",
            "...FeeFFFFF..",
            "...Feeefkff..",
            "...Feeeffffk.",
            "...FFeeFffff.",
            "....FFeFFfff.",
            ".....nBBn....",
])

#            01234567890123456789012345678901
BODY[8] = (8, [
            "..yy............................",
            "..gyY...........................",
            "..gyY...........................",
            ".ww.yY..........................",
            ".w...yY.........................",
            ".....yYyy.......................",
            "....yyyyyyooo...................",
            "...ooyyybbbbbbbbbb..............",
            "..oooYYYBBBBBbbbbFF.............",
            ".ooggggggBBBBFFFFbFkk..ooo......",
            ".kkkkkkkggBBBbbbbFFkkOooooooooo.",
            "kkkkkkkkkggBBBBBBBRkooooooooooy.",
            "kkkggkkkkggRRBBRRRRooooooooooyy.",
            "kkgwwgkkkggRRRRRRRooooooooorrrr.",
            "kkgwwgkkkggOORRRRROOgggooorwgwrw",
            "kkgwwgkkkggOOOOORRkkkkkgOyywgrww",
            "kkkggkkkkgkOOOOOOkkkkkkggyywrwgg",
            "kkkkkkkkkkkrrOOOkkkkkkkkgrrrwg..",
            ".kkkkkkkkk...rrrkkgwwgkkkrwwg...",
            "..kkkkkk........kkgwwgkkkwgg....",
            "................kkkkkkkkkg......",
            ".................kkkkkkk........",
            "..................kkkk..........",
])
HEAD[8] = (7, 7, [
            "....bbbbb....",
            "...bbbbbbbb..",
            "...BBbbbbbbb.",
            "...BBBBBBBBBB",
            "...eeFFFFFF..",
            "..eeeFfkffk..",
            "..eeeFffffkk.",
            "..eeeFFfffff.",
            "...eeFFFfff..",
            ".....nBBn....",
])

#             01234567890123456789012345678901
BODY[10] = (6, [
            "........yy......................",
            ".......gyY......................",
            ".......gyY......................",
            "......ww.yY.....................",
            "......w..yY.....................",
            ".........yY.....................",
            "........yyy........ggg..........",
            "........yyy.......kkkkk.........",
            "....oogggnnnBBBBBBkkkkk.........",
            "..gggkgkgknnBBBBBBBkkkk.........",
            ".kkkkkgkgknnFBBBBBFkkkk.........",
            ".kkkkkgkgknnBFFBBFFkkkk.........",
            ".kkkkkgkgknnBBFFkkkrrOk.........",
            ".kkwwkgkgknnBBBBkkRRRoooo.......",
            ".kkwwkgkgkRnBBRRRooooooooooogg..",
            ".kkkkkgkgkORRRooooooooooooookk..",
            ".kkkkkgkgkrORRRooooooooooOyykk..",
            ".kkkkkgkg.rrOggggoooOOOOOOyykk..",
            "..kkkkk....kkkkkgkyywgwgwgOOkk..",
            "...........kkkkkgkyywgwgwgOrww..",
            "...........kwwkkgkOOwgwgwwwwgg..",
            "...........kwwkkgkrrrwwwwggg....",
            "...........kwwkkgkwwwgggg.......",
            "...........kkkkkgkggg...........",
            "............kkkk................",
])
HEAD[10] = (9, 7, [
            "...bbbbbb...",
            "..bbbbbbbb..",
            "..bbbbbbbbb.",
            "..BBBBBBBBBB",
            ".eeFFFFFFFe.",
            ".eeFkfffkFe.",
            ".eeFfffkkff.",
            ".eeFfffffff.",
            "..eeFffFFff.",
            "....nBBBn...",
])

#             01234567890123456789012345678901
BODY[11] = (5, [
            "...........yy...................",
            "..........gyY...................",
            "..........gyY...................",
            ".........wwyY...................",
            ".........w.yY...................",
            "...........yY...................",
            "...........yY...................",
            "...............oooo.ggg.........",
            ".....ggggoooooooooookkkk........",
            "...ggkgkgknnbbbbbbbbokkk........",
            "...kkkgkgknnFBBBBBBFOkkk........",
            "...kkkgkgknnFFBBBBFFOkkk........",
            "...kkkgkgknnBFFBBFFBrkkk........",
            "...kkkgkgknnBBkkkkBBrkkk........",
            "...kkkgkgkRnBBBkkBBBrOkk........",
            "...kkkgkgkRRRRRooooooooo........",
            "...kkkgkgkORooooooooooooogg.....",
            "...kkkgkgkrOoooooooooooookkk....",
            "....kkkgkgggggoooooOOOOOOkkk....",
            "........kkkgkgkyywgwgwgyyOkk....",
            "........kkkgkgkyywgwgwgyyrkk....",
            "........kkkgkgkOOwgwgwgOOrkk....",
            "........kkkgkgkrrrrrwwwwwwwk....",
            "........kkkgkgkwwwwwggggggg.....",
            "........kkkgkgkggggg............",
            ".........kkkkk..................",
])
HEAD[11] = (10, 7, [
            "..bbbbbb...",
            ".bbbbbbbb..",
            ".bbbbbbbb..",
            ".BBBBBBBBBn",
            "eFFFFFFFFe.",
            "eeFkfffkFe.",
            "eeFffkkffe.",
            "eeffffffff.",
            ".eFffFFFff.",
            "...nBBBn...",
])

DRAWN = sorted(BODY)

# ================================================================================================
# Assembly
# ================================================================================================
def _check(rows, width, what):
    for i, r in enumerate(rows):
        if len(r) != width:
            sys.exit("%s row %d is %d wide, want %d: %r" % (what, i, len(r), width, r))
        for ch in r:
            if ch not in IDX:
                sys.exit("%s row %d has unknown colour %r" % (what, i, ch))

def compose(k):
    """Index grid (32 rows of 32 ints) for a drawn view."""
    top, rows = BODY[k]
    _check(rows, SPR, "BODY[%d]" % k)
    g = [[0]*SPR for _ in range(SPR)]
    for i, r in enumerate(rows):
        for x, ch in enumerate(r):
            g[top + i][x] = IDX[ch]
    hx, hy, hrows = HEAD[k]
    _check(hrows, len(hrows[0]), "HEAD[%d]" % k)
    for i, r in enumerate(hrows):
        for j, ch in enumerate(r):
            if ch != ".":
                g[hy + i][hx + j] = IDX[ch]
    return g

def frame(k):
    k %= N_ANGLES
    if k <= N_ANGLES//2:
        return compose(k) if k in BODY else None
    src = frame(N_ANGLES - k)
    return None if src is None else [row[::-1] for row in src]

def to_image(g):
    im = Image.new("RGBA", (len(g[0]), len(g)), (0, 0, 0, 0))
    px = im.load()
    for y, row in enumerate(g):
        for x, v in enumerate(row):
            if v: px[x, y] = RGB[v] + (255,)
    return im

def split_bg(w, h):
    bg = Image.new("RGBA", (w, h), ROAD + (255,))
    bg.paste(GRASS + (255,), (w//2, 0, w, h))
    return bg

def up(im, s):
    return im.resize((im.width*s, im.height*s), Image.NEAREST)

# ================================================================================================
# Current baked art, for comparison only
# ================================================================================================
def _ints(text, n):
    out = []
    for m in re.finditer(r"0x[0-9a-fA-F]+|\d+", text):
        out.append(int(m.group(), 0))
        if len(out) >= n: break
    return out

def _abgr(v):
    return (v & 255, (v >> 8) & 255, (v >> 16) & 255, 255 if v >> 24 else 0)

def load_old():
    """(24 RGBA frames, portrait) of the baked Bruno from src/art.h, or (None, None)."""
    try:
        src = open(ART_H).read()
        a = src.index("{", src.index("k_kart_px[CHAR_COUNT]"))
        px = _ints(src[a:a + 400000], N_ANGLES*SPR*SPR)
        b = src.index("{", src.index("k_kart_pal[CHAR_COUNT]"))
        pal = [_abgr(v) for v in _ints(src[b:b + 2000], 32)]
        frames = []
        for k in range(N_ANGLES):
            im = Image.new("RGBA", (SPR, SPR), (0, 0, 0, 0))
            im.putdata([pal[v] if v else (0, 0, 0, 0) for v in px[k*1024:(k + 1)*1024]])
            frames.append(im)
        c = src.index("{", src.index("k_portrait0_px[]"))
        ppx = _ints(src[c:c + 20000], SPR*SPR)
        d = src.index("{", src.index("k_portrait0_pal[32]"))
        ppal = [_abgr(v) for v in _ints(src[d:d + 2000], 32)]
        port = Image.new("RGBA", (SPR, SPR), (0, 0, 0, 0))
        port.putdata([ppal[v] if v else (0, 0, 0, 0) for v in ppx])
        return frames, port
    except (OSError, ValueError, IndexError) as e:
        print("note: could not read current art from art.h (%s)" % e)
        return None, None

# ================================================================================================
# Portrait (32x32, same palette, no outline)
# ================================================================================================
# The portrait now lives with the other seven in tools/portraits.py, which bake_art.py bakes
# into src/art.h. Its colour letters and RGB values are the same as PALETTE above.
sys.path.insert(0, HERE)
from portraits import PORTRAITS
PORTRAIT = PORTRAITS[0]["rows"]
assert all(RGB[IDX[ch]] == rgb for ch, rgb in PORTRAITS[0]["pal"].items())

def portrait():
    _check(PORTRAIT, SPR, "PORTRAIT")
    assert len(PORTRAIT) == SPR
    return [[IDX[ch] for ch in r] for r in PORTRAIT]

# ================================================================================================
# Outputs
# ================================================================================================
SCALE = 6

def on_bg(im, s=SCALE, dy=0, pad=0):
    """Sprite on the split road/grass background, upscaled s times."""
    w, h = im.width + 2*pad, im.height + 2*pad
    bg = split_bg(w, h)
    bg.alpha_composite(im, (pad, pad + dy))
    return up(bg, s).convert("RGB")

def save_gif(path, frames, ms):
    """Exact-colour GIF: one global palette of the 15 sprite colours plus the two backgrounds."""
    cols = [c for c in RGB if c] + [ROAD, GRASS]
    lut = {c: i for i, c in enumerate(cols)}
    flat = [v for c in cols for v in c] + [0]*(768 - 3*len(cols))
    q = []
    for f in frames:
        im = Image.new("P", f.size); im.putpalette(flat)
        im.putdata([lut[c] for c in f.getdata()])
        q.append(im)
    q[0].save(path, save_all=True, append_images=q[1:], duration=ms, loop=0, disposal=1, optimize=False)

def sheet_row(ims, bg, s):
    row = Image.new("RGBA", (len(ims)*SPR, SPR), bg + (255,))
    for i, im in enumerate(ims):
        row.alpha_composite(im, (i*SPR, 0))
    return up(row, s)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else DEFAULT_OUT
    os.makedirs(out, exist_ok=True)
    J = lambda n: os.path.join(out, n)
    new = [to_image(frame(k)) for k in range(N_ANGLES)]
    port = to_image(portrait())
    used = sorted({v for k in range(N_ANGLES) for row in frame(k) for v in row} | {v for row in portrait() for v in row})
    assert len(PALETTE) <= 16 and max(used) < 16
    old, old_port = load_old()

    # (a) turntable, ~8 fps
    save_gif(J("bruno_turntable.gif"), [on_bg(f, pad=4) for f in new], 125)
    # (b) drift wobble at the rear 3/4 views the player sees while sliding: the kart leans into
    #     the drift angle, bobs one pixel, then swings out the other way
    seq = [0, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0, 23, 22, 21, 21, 21, 21, 21, 21, 21, 21, 22, 23]
    fr = []
    for i, k in enumerate(seq):
        drifting = k in (3, 21)
        fr.append(on_bg(new[k], pad=4, dy=1 if drifting and i % 2 else 0))
    save_gif(J("bruno_drift.gif"), fr, 70)
    # (c) spin-out: two full turns, then settle
    fr = [on_bg(new[0], pad=4)]*4 + [on_bg(new[k % N_ANGLES], pad=4) for k in range(0, 2*N_ANGLES + 1, 2)] + [on_bg(new[0], pad=4)]*6
    save_gif(J("bruno_spin.gif"), fr, 45)

    # portrait: 1x, 8x, and the HUD half-size (every other pixel) at 6x
    port.save(J("bruno_portrait_1x.png"))
    for name, im, s in (("bruno_portrait_8x.png", port, 8), ("bruno_portrait_half_6x.png", port.resize((16, 16), Image.NEAREST, box=None).copy(), 6)):
        if im.width == 16:
            im = Image.new("RGBA", (16, 16)); im.putdata([port.getpixel((x*2, y*2)) for y in range(16) for x in range(16)])
        on_bg(im, s, pad=2).save(J(name))

    # sheet: 24 new frames (road row, grass row), then the 24 current baked frames the same way
    S = 4
    rows = [sheet_row(new, ROAD, S), sheet_row(new, GRASS, S)]
    if old: rows += [sheet_row(old, ROAD, S), sheet_row(old, GRASS, S)]
    sheet = Image.new("RGBA", (rows[0].width, sum(r.height for r in rows) + 8*(len(rows) - 1)), (24, 24, 32, 255))
    y = 0
    for r in rows:
        sheet.alpha_composite(r, (0, y)); y += r.height + 8
    sheet.convert("RGB").save(J("bruno_sheet_4x.png"))

    # old vs new portrait at 8x
    pair = [old_port, port] if old_port else [port]
    cmp_ = Image.new("RGB", (len(pair)*(SPR + 4)*8 + 8, (SPR + 4)*8), (24, 24, 32))
    for i, im in enumerate(pair):
        cmp_.paste(on_bg(im, 8, pad=2), (i*((SPR + 4)*8 + 8), 0))
    cmp_.save(J("bruno_compare.png"))

    if "--work" in sys.argv:      # big contact sheets + 1x strip for eyeballing while drawing
        work = J("work"); os.makedirs(work, exist_ok=True)
        for name, sel in (("new_a", range(0, 7)), ("new_b", range(6, 13))):
            sh = Image.new("RGBA", (len(sel)*SPR*10, 2*SPR*10))
            sh.alpha_composite(sheet_row([new[k] for k in sel], ROAD, 10), (0, 0))
            sh.alpha_composite(sheet_row([new[k] for k in sel], GRASS, 10), (0, SPR*10))
            sh.save(os.path.join(work, name + ".png"))
        one = Image.new("RGBA", (N_ANGLES*SPR, 4*SPR))
        one.alpha_composite(sheet_row(new, ROAD, 1), (0, 0)); one.alpha_composite(sheet_row(new, GRASS, 1), (0, SPR))
        if old:
            one.alpha_composite(sheet_row(old, ROAD, 1), (0, 2*SPR)); one.alpha_composite(sheet_row(old, GRASS, 1), (0, 3*SPR))
        up(one, 2).save(os.path.join(work, "one_2x.png"))
    print("wrote %d frames, %d colours used (incl. transparent) -> %s" % (N_ANGLES, len(used), out))

if __name__ == "__main__":
    main()
