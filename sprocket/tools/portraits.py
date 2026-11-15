"""Hand-drawn 32x32 racer portraits for Sprocket Rally, shared by bake_art.py and pixel_chars.py.

Every portrait is explicit pixel data: 32 rows of 32 characters, one character per pixel, '.'
for transparent, each other character a colour from that portrait's own `pal`. They are drawn
without an ink outline: forms are separated by value and hue, and each head's own light values
carry its silhouette against the dark menu backgrounds. All eight share one scale and framing
(big head filling the cell, shoulders along the bottom edge, light from above).

Rules the data is checked against on import:
  * at most 16 colours per portrait including transparent (the game stores 32 per portrait);
  * every channel a multiple of 8, or 255 (15-bit SNES colour).
The results table and the HUD running order show these at half size by sampling even x / even y
only, so pupils, glints and mouth lines are kept on even coordinates where they can be.

PORTRAITS is in racer index order (RACERS in bake_art.py, k_chars in src/race.cc).
Run `python3 tools/portraits.py OUTDIR` to write preview sheets (needs pillow); it writes
nothing inside the game tree.
"""
SIZE = 32

# Bruno Wrench: bulldog mechanic, blue cap, scowl, underbite
_BRUNO = dict(
    name="BRUNO",
    pal={
        "y": (255, 224, 104),  # cap badge / buttons
        "Y": (208, 144, 40),   # badge shade
        "k": (40, 40, 56),     # eyes, nose
        "w": (208, 208, 224),  # eye glint, teeth
        "f": (248, 216, 168),  # fur light
        "F": (216, 160, 104),  # fur mid
        "e": (128, 80, 56),    # ears, brows, jowl lines
        "b": (104, 160, 248),  # cap + overalls light
        "B": (56, 96, 200),    # cap + overalls mid
        "n": (32, 48, 128),    # cap + overalls shade
    },
    rows=[
    #    01234567890123456789012345678901
        "................................",
        "...........bbbbbbbbbb...........",
        ".........bbbbbbbbbbbbbb.........",
        "........bbbbbbbbbbbbbbbbB.......",
        ".......bbbbbbbbbbbbbbbbbBB......",
        ".......bbbbbbbyyyybbbbbBBB......",
        ".......bbbbbbbyYYybbbbBBBB......",
        ".......BBBBBBBBBBBBBBBBBBn......",
        ".....bbbbbbbbbbbbbbbbbbbbbbb....",
        "....BBBBBBBBBBBBBBBBBBBBBBBBB...",
        "....nnnnnnnnnnnnnnnnnnnnnnnnn...",
        "...eeeFFFFFFFFFFFFFFFFFFFFeee...",
        "..eeeeFeeeeeFFFFFFFFeeeeeFeeee..",
        "..eeeeFFeeeeeFFFFFFeeeeeFFeeee..",
        "..eeeeFfwkkffFFFFFFffwkkfFeeee..",
        "..eeeeFfkkkffffffffffkkkfFeeee..",
        "..eeeFfffffffkkkkkkfffffffFeee..",
        "..eeeFfffffffkwkkkkfffffffFeee..",
        "..eeeFffffffffkkkkffffffffFeee..",
        "...eeFfffffffffeeffffffffffee...",
        "...eFffffffffeeeeeeffffffffFe...",
        "...FfffffffeeffffffeefffffffF...",
        "...FffffwweffffffffffewwffffF...",
        "...FFfffwwffffffffffffwwfffFF...",
        "....FFfffFFffffffffffFFfffFF....",
        ".....FFFFFeFFffffffFFeFFFFF.....",
        "....BBnFFeeeFFFFFFFFeeeFFnBB....",
        "..bBBBBBnnFFeeeeeeeeFFnnBBBBBb..",
        ".bbBBBBBBBnnnnnnnnnnnnBBBBBBBbb.",
        ".bBBByyBBBBBBBBBBBBBBBBBByyBBBb.",
        ".BBBByyBBBBBBBBBBBBBBBBBByyBBBB.",
        ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.",
    ],
)

# Pip-8: boxy robot, screen face, orange ear discs, red antenna ball
_PIP = dict(
    name="PIP-8",
    pal={
        "W": (248, 248, 240),  # head top plane
        "w": (216, 216, 208),  # head front
        "g": (160, 168, 184),  # head / body shade
        "G": (104, 112, 136),  # bolts, underside
        "s": (72, 80, 112),    # antenna stalk, neck
        "k": (32, 40, 72),     # screen
        "c": (160, 240, 248),  # screen face light
        "C": (64, 184, 232),   # screen face mid
        "h": (255, 255, 255),  # glints
        "r": (248, 88, 88),    # antenna ball
        "R": (192, 40, 56),    # antenna ball shade
        "o": (255, 176, 72),   # ear discs light
        "O": (224, 112, 40),   # ear discs shade
    },
    rows=[
    #    01234567890123456789012345678901
        "...............rr...............",
        "..............rhrr..............",
        "..............rrrR..............",
        "...............RR...............",
        "...............ss...............",
        "..............GssG..............",
        "......WWWWWWWWWWWWWWWWWWWW......",
        ".....WWWWWWWWWWWWWWWWWWWWWW.....",
        ".....wwwwwwwwwwwwwwwwwwwwww.....",
        ".....wwwwwwwwwwwwwwwwwwwwww.....",
        ".....wwwkkkkkkkkkkkkkkkkwwg.....",
        ".....wwwkkkkkkkkkkkkkkkkwwg.....",
        "...oowwwkkcccckkkkcccckkwwgoo...",
        "..ooowwwkkchcckkkkchcckkwwgooo..",
        "..ooowwwkkcccckkkkcccckkwwgooo..",
        "..OOOwwwkkCCCCkkkkCCCCkkwwgOOO..",
        "..OOOwwwkkkkkkkkkkkkkkkkwwgOOO..",
        "..OOOwwwkkkckkkkkkkkckkkwwgOOO..",
        "...OOwwwkkkkcccccccckkkkwwgOO...",
        ".....wwwkkkkkCCCCCCkkkkkwwg.....",
        ".....wwwkkkkkkkkkkkkkkkkwwg.....",
        ".....wGwwwwwwwwwwwwwwwwwwGg.....",
        ".....wwwwwwwwwwwwwwwwwwwwwg.....",
        "......gggggggggggggggggggg......",
        "............ssssssss............",
        "...........GGGGGGGGGG...........",
        "......wwwwwwwwwwwwwwwwwwww......",
        "....wwwwwwwwwwwwwwwwwwwwwwww....",
        "...gggggggggkkkkkkkkggggggggg...",
        "...gggggggggkcccCCCkggggggggg...",
        "...gggggggggkkkkkkkkggggggggg...",
        "...GGGGGGGGGGGGGGGGGGGGGGGGGG...",
    ],
)

# Madame Hex: green witch, purple hat with a gold band
_HEX = dict(
    name="MADAME HEX",
    pal={
        "p": (176, 112, 232),  # hat + robe light
        "P": (136, 72, 200),   # hat + robe mid
        "d": (96, 48, 152),    # hat shade, hair
        "y": (255, 216, 80),   # hat band, brooch
        "Y": (208, 152, 40),   # band shade
        "l": (168, 240, 120),  # skin light (nose)
        "L": (112, 208, 88),   # skin mid
        "G": (64, 152, 72),    # skin shade
        "k": (40, 24, 56),     # brows, pupils, mouth
        "w": (255, 255, 255),  # eye whites, tooth
    },
    rows=[
    #    01234567890123456789012345678901
        ".................pp.............",
        "................ppP.............",
        "...............ppPP.............",
        "..............pppPP.............",
        ".............ppppPPd............",
        "............pppppPPd............",
        "...........ppppppPPPd...........",
        "..........pppppppPPPPd..........",
        ".........yyyyyyyyyyYYYY.........",
        "........yyyyyyyyyyyyYYYY........",
        "...ppppppppppppppppppppPPPPPP...",
        ".pPPPPPPPPPPPPPPPPPPPPPPPPPPddd.",
        "...ddddGGGGGGGGGGGkkkkkGGdddd...",
        "....dPdLkkkkkLLLLLLLLLLLLdPd....",
        "....dPdLLwwkkLLllLLwwkkLLdPd....",
        "....dPdLLwwkkLllllLwwkkLLdPd....",
        "....dPdLLLLLLLllllGLLLLLLdPd....",
        "....dPdLLLLLLlllllGGLLLLLdPd....",
        "....dPdLGLLLlllllllGLLLLLdPd....",
        "....dPdLLLLLLGGGGGGGLLGkLdPd....",
        "....dPdLLLkkkkkkkkkkkkkLLdPd....",
        "....ddd.LLLLLwwLLLLLLLLL.ddd....",
        ".....dd..LLLLLLLLLLLLLL..dd.....",
        ".....d....GLLLLLLLLLLG....d.....",
        "............GLLLLLLG............",
        "..............GLLG..............",
        "........PPPPPPGLLGPPPPPP........",
        ".....PPPPPPPPPdGGdPPPPPPPPP.....",
        "...pPPPPPPPPPPPddPPPPPPPPPPPd...",
        "..pPPPPPPPPPPPPyyPPPPPPPPPPPPd..",
        ".pPPPPPPPPPPPPPyyPPPPPPPPPPPPPd.",
        ".PPPPPPPPPPPPPPPPPPPPPPPPPPPPPd.",
    ],
)

# Cap'n Gullbeard: seagull pirate, gold-trimmed hat, red coat
_GULL = dict(
    name="CAP'N GULLBEARD",
    pal={
        "n": (88, 96, 160),    # hat light
        "N": (56, 64, 120),    # hat mid
        "y": (255, 216, 80),   # gold trim
        "Y": (208, 152, 40),   # gold shade
        "w": (255, 255, 255),  # feathers light
        "W": (208, 216, 232),  # feathers mid
        "g": (144, 160, 192),  # feathers shade
        "o": (255, 176, 56),   # beak light
        "O": (232, 120, 32),   # beak shade
        "k": (32, 32, 56),     # eyes, brows
        "r": (240, 72, 64),    # coat, hat feather
        "R": (176, 40, 48),    # coat shade
    },
    rows=[
    #    01234567890123456789012345678901
        "......................rr........",
        ".....................rrR........",
        "............nnnnnnnnrrR.........",
        "..........nnnnnnnnnnrRR.........",
        ".........nnnnnnnnnnNrRN.........",
        ".nn.....nnnnnnwwwnnNNNNN.....NN.",
        ".nnnn..nnnnnnwkwkwnNNNNNN..NNNN.",
        ".ynnnnnnnnnnnnnwwwnNNNNNNNNNNNY.",
        ".yyynnnnnnnnnnnnnNNNNNNNNNNNYYY.",
        "...yyyyyyyyyyyyyyyyyyyyyYYYYY...",
        ".......WWWWWWWWWWWWWWWWWW.......",
        "......wwwwwwwwwwwwwwwwwWW.......",
        ".....wwwwwwkkkkwwwwkkkwWW.......",
        ".....wwwwwwwkkkwwwwwkkwWW.......",
        ".....wwwwwwwkkkwwwwwkkwWW.......",
        ".....wwwwwwwwwwoooooowwWW.......",
        ".....wwwwwwwwwoooooooooooo......",
        ".....wwwwwwwwwooooooooooooOO....",
        ".....wwwwwwwwWOOOOOOOOOOOOooO...",
        ".....wwwwwwwwWoooooorroO..OO....",
        ".....wwwwwwwwWWOOOOOOWWW........",
        "......wwwwwwwwwWWWWWWWWg........",
        "......wwwwwwwwwwwWWWWWWg........",
        ".......wwwwwwwwwwwwWWWg.........",
        ".......WwwWwwwWwwwWwWWg.........",
        "........WwwwWwwwWwwwWg..........",
        ".....rrrrrWwWwwWwwWwWWrrrrR.....",
        "...rrrrrrrrWwwwWwwwWWrrrrrrRR...",
        "..yyrrrrrrrrrWwwwWWrrrrrrrrRyy..",
        ".yyyrrrrrrrrrrRWWRrrrrrrrrrRyyy.",
        ".YYYrrrrrrrrrrrRRrrrrrrrrrrRYYY.",
        ".rrrrrrrrrrrrrrRRrrrrrrrrrrRRRR.",
    ],
)

# Tiny Rex: baby T-rex, big eye, toothy grin, orange spikes
_REX = dict(
    name="TINY REX",
    pal={
        "l": (160, 232, 104),  # hide light
        "L": (96, 192, 72),    # hide mid
        "G": (48, 136, 64),    # hide shade
        "c": (248, 240, 208),  # jaw + belly
        "C": (208, 192, 152),  # jaw + belly shade
        "o": (255, 176, 72),   # spikes light
        "O": (224, 112, 40),   # spikes shade
        "w": (255, 255, 255),  # eye white, teeth, claws
        "k": (32, 40, 48),     # pupil, mouth
    },
    rows=[
    #    01234567890123456789012345678901
        "................................",
        "...........llllll...............",
        ".........llllllllll.............",
        ".......lllllllllllll............",
        "....o.lllllllllllllllL..........",
        "...oollLLlllllllllllllLL........",
        "..ooOllllllllLLLLLLlllLL........",
        "...OOLLlllllllwwwwlllLLL........",
        ".....LLllLlllwwkklllLLLL........",
        "...o.LLlllLLlwwkklllLLLL........",
        "..ooLLLlllllllwwwwllllllllll....",
        ".ooOLLLLllllllllllllllllllllL...",
        "..OOLLLLLlllllllllllllllllGllL..",
        "....LLLLLLLLllllllllllllllGllL..",
        "...oLLLLLLLLLLLLLLLLLLLLLLLLLG..",
        "..oOLLLLLLLLGGGGGGGGGGGGGGGGGG..",
        "...OLLLLLLLkwkwkwkwkwkwkwkwkwk..",
        "....LLLLLLLLkkkkkkkkkkkkkkkkk...",
        "....LLLLLLLccccccccccccccccC....",
        "....GLLLLLLLcccccccccccccCC.....",
        ".....GLLLLLLLCCCCCCCCCCCC.......",
        "......GLLLLLLcccccccc...........",
        ".......GLLLLLcccccccc...........",
        ".......LLLLLLcccccccc...........",
        ".....lllllllccccccccccll........",
        "....llllllllcccccccccclll.......",
        "...lllllllllcccccccccclllllLw...",
        "..llllllllllccccccccccLLLLLLw...",
        "..LLllllllllccccccccccLLL.......",
        ".LLLllllllllCCCCCCCCCCLLLL......",
        ".LLLLlllllllccccccccccLLLL......",
        ".GLLLLLLLLLLCCCCCCCCCCLLLG......",
    ],
)

# Volt Vega: white helmet, pink visor and spiked hair, neon suit
_VOLT = dict(
    name="VOLT VEGA",
    pal={
        "h": (255, 255, 255),  # helmet light, visor glare
        "H": (200, 208, 232),  # helmet mid
        "g": (136, 144, 184),  # helmet shade
        "m": (255, 136, 232),  # hair + visor light
        "M": (240, 56, 200),   # hair + visor mid
        "D": (160, 24, 136),   # hair + visor shade
        "c": (136, 248, 255),  # neon light
        "C": (0, 200, 240),    # neon mid
        "s": (248, 200, 160),  # skin
        "S": (216, 144, 112),  # skin shade
        "p": (144, 112, 224),  # suit light
        "P": (96, 64, 168),    # suit mid
        "k": (40, 24, 64),     # eyes behind visor, mouth
    },
    rows=[
    #    01234567890123456789012345678901
        "...............mm...............",
        ".........m.....mm.....m.........",
        ".........mm...mmMM...mM.........",
        "....m....mmM..mmMM..mmM....m....",
        "....mm...mmMMmmmMMMmmMM...mM....",
        "....mmM.mmmMmmmmMMMmmMMD.mMM....",
        ".....mMMmmMMmmmMMMmmMMDmMMD.....",
        "......hhhhhhhhhhhhhhhhhhHH......",
        ".....hhhhhhhhhhhhhhhhhhhHHH.....",
        "....hhhhhhhhhhhhhhhhhhhhHHHg....",
        "....HHHHHHHHHHHHHHHHHHHHHHHg....",
        "....hmmmmmmmmmmmmmmmmmmmmmMg....",
        "....hmMMMMMMMMMMMMMMMhhMMMDg....",
        "....hMMMkkkkMMMMkkkkhhMMMMDg....",
        "....hMMMMkkkMMMMkkkhhMMMMMDg....",
        "....hMMMMMMMMMMMMMhhMMMMMDDg....",
        "....hDDDDDDDDDDDDDDDDDDDDDDg....",
        "....hcchhHsssssssssssshccHHg....",
        ".....cchhHsssssssssssshccHg.....",
        ".....hhhhHsssssssssskshhhHg.....",
        "......hhhHsssskkkkkksshhHg......",
        ".......hhHSssssssssssShHg.......",
        "........HHSSssssssssSSHg........",
        "...........SSssssssSS...........",
        "............SSSSSSSS............",
        "........ppppcccSScccpppp........",
        ".....ppppppppccccccppppppPP.....",
        "...ppppppppppPccccPppppppppPP...",
        "..pPPPPPPPPPPPPccPPPPPPPPPPPPP..",
        ".pPPPPPPPPPPPPPccPPPPPPPPPPPPPP.",
        ".PPPPPPPPPPPPPPccPPPPPPPPPPPPPP.",
        ".PPPPPPPPPPPPPPCCPPPPPPPPPPPPPP.",
    ],
)

# Sir Clanks: great helm, brass cross, glowing eyes, red plume
_CLANKS = dict(
    name="SIR CLANKS",
    pal={
        "s": (232, 240, 248),  # steel highlight
        "S": (184, 192, 208),  # steel light
        "g": (128, 136, 160),  # steel mid
        "G": (80, 88, 120),    # steel shade, breath holes
        "k": (24, 24, 48),     # visor slit
        "e": (136, 224, 255),  # eyes
        "y": (255, 216, 80),   # brass
        "Y": (208, 152, 40),   # brass shade
        "r": (248, 88, 72),    # plume + tabard light
        "R": (200, 40, 48),    # plume + tabard mid
        "D": (128, 24, 40),    # plume shade
    },
    rows=[
    #    01234567890123456789012345678901
        ".............rrrrrr.............",
        "...........rrrrrrrrrrr..........",
        "..........rrrrrrrrrRRRRR........",
        "..........rrrrrrrRRRRRRRRR......",
        "...........rrrrRRRRRRRRRRRRD....",
        "............rrRRRRDD..RRRRRRD...",
        ".........sssssrRRDsssSS.RRRDD...",
        ".......sssssssssssssssSSS.RDD...",
        "......sSSSSSSSSSSSSSSSSSgg.DD...",
        "......sSSSSSSSSyySSSSSSSgg......",
        "......sSSSSSSSSyYSSSSSSSgg......",
        ".....yyyyyyyyyyyyyyyyyyyYYY.....",
        ".....SkkkkkkkkkyYkkkkkkkkkg.....",
        ".....SkkeeekkkkyYkkkkeeekkg.....",
        ".....SkkkeeekkkyYkkkeeekkkg.....",
        ".....SsssssssssyYsssssssSSg.....",
        "......SSSSSSSSSyYSSSSSSSgg......",
        "......SSGSGSSSSyYSSSSGSGgg......",
        "......SSSSSSSSSyYSSSSSSSgg......",
        "......SSGSGSSSSyYSSSSGSGgg......",
        "......SSSSSSSSSyYSSSSSSSgg......",
        "......SSGSGSSSSyYSSSSGSGgg......",
        ".......ggggggggyYgggggggG.......",
        "........ggggggggggggggGG........",
        ".........GGGGGGGGGGGGGG.........",
        "...ssssssggggggggggggggssssSS...",
        ".sssssssssSrrrrrrrrrrSsssssSSSg.",
        ".sSSSSSSSSgrrrrrrrrrrgSSSSSSSgg.",
        ".SSSSSSSSSgrrrryyrrrrgSSSSSSSgg.",
        ".SSSSSSSSggrryyyyyyrrgSSSSSSggg.",
        ".gSSSSSSgggRRRRyyRRRRgSSSSSgggG.",
        ".gggggggggGRRRRyyRRRRGggggggggG.",
    ],
)

# Granny Goggles: white curls, brass goggles, grin, red scarf
_GRANNY = dict(
    name="GRANNY GOGGLES",
    pal={
        "w": (255, 255, 255),  # hair light, teeth, lens glint
        "W": (216, 224, 240),  # hair mid
        "g": (160, 168, 200),  # hair shade
        "s": (248, 208, 176),  # skin
        "S": (216, 152, 128),  # skin shade
        "y": (255, 216, 80),   # goggle brass
        "Y": (208, 152, 40),   # brass shade
        "c": (176, 248, 255),  # lens light
        "C": (64, 200, 240),   # lens mid
        "k": (48, 32, 56),     # pupils, strap, mouth
        "r": (248, 80, 72),    # scarf
        "R": (192, 40, 48),    # scarf shade
        "p": (184, 152, 232),  # cardigan
        "P": (136, 104, 192),  # cardigan shade
    },
    rows=[
    #    01234567890123456789012345678901
        "........www...wwww...www........",
        "......wwwwww.wwwwww.wwwwwW......",
        ".....wwwwwwwwwwwwwwwwwwwwWW.....",
        "...wwwwwwwWwwwwwwWwwwwwwwwWWW...",
        "..wwwwwwWWwwwwwWWwwwwwwWWwwWWW..",
        "..wwwwwwwwwwwwwwwwwwwwwwwwWWWg..",
        ".wwwwWwwwwwwWWwwwwwWWwwwwwwWWWg.",
        ".wwwWWwwwwwwwwwwwwwwwwwwwwWWWgg.",
        ".WwwwwwWWWwwWWWwwWWWwwWWWwwwWWg.",
        ".wwwwwwWssssssssssssssssWwwwWWg.",
        ".wwwwwWssssssssssssssssssWwwWWg.",
        ".wwwwwWsyyyyyssssssyyyyysWwwWWg.",
        ".wwwwwWywwcccyssssywwcccyWwwWWg.",
        ".wwwWkkywckkCyyyyyywckkCykkWWWg.",
        ".WwwWkkycckkCyYYYYycckkCykkwWWg.",
        ".wwwwwWyCCCCCYssssyCCCCCYWwwWgg.",
        "..wwwwWYYCCCYYssssYYCCCYYWwWWg..",
        "..wwWWWsYYYYYssssssYYYYYswWWgg..",
        "..wWWWgssSSssssssSsssSSsswWWgg..",
        "...WWWgsssssssSSSSsssssssWWgg...",
        "....WggsskwwwwwwwwwwwwkssWgg....",
        ".......ssskwwwkkwwwwwksss....rrr",
        ".......sssskkRRRRRRkkssss..rrrrR",
        "........SsssssssssssssSS.rrrrrRR",
        ".........SSssssssssSSSSrrrrrRR..",
        "......rrrrrrrrrrrrrrrrrrrrRR....",
        ".....rrRrrrRrrrrRrrrrRrrRRR.....",
        ".....RRRRRRRRRRRRRRRRRRRRRR.....",
        "...pppppppppprrrRppppppppppPP...",
        "..ppppppppppprrrRpppppppppppPP..",
        ".pPPPPPPPPPPPrrRRPPPPPPPPPPPPPP.",
        ".PPPPPPPPPPPPrRRRPPPPPPPPPPPPPP.",
    ],
)

PORTRAITS = [_BRUNO, _PIP, _HEX, _GULL, _REX, _VOLT, _CLANKS, _GRANNY]

def _check(p):
    rows, pal = p["rows"], p["pal"]
    assert len(pal) <= 15, "%s: %d colours + transparent, want <= 16" % (p["name"], len(pal))
    assert "." not in pal
    for ch, rgb in pal.items():
        assert all(v == 255 or (0 <= v < 256 and v % 8 == 0) for v in rgb), "%s: colour %r %r is not 15-bit" % (p["name"], ch, rgb)
    assert len(rows) == SIZE, "%s: %d rows" % (p["name"], len(rows))
    for y, r in enumerate(rows):
        assert len(r) == SIZE, "%s row %d is %d wide: %r" % (p["name"], y, len(r), r)
        for ch in r:
            assert ch == "." or ch in pal, "%s row %d: unknown colour %r" % (p["name"], y, ch)

for _p in PORTRAITS:
    _check(_p)

def indexed(i):
    """Portrait i as (px, pal): 32 rows of 32 palette indices and a list of RGBA tuples whose
    entry 0 is transparent -- the form bake_art.py writes into src/art.h."""
    p = PORTRAITS[i]
    chars = list(p["pal"])
    idx = {".": 0}
    idx.update((ch, n + 1) for n, ch in enumerate(chars))
    pal = [(0, 0, 0, 0)] + [tuple(p["pal"][ch]) + (255,) for ch in chars]
    return [[idx[ch] for ch in r] for r in p["rows"]], pal

def _main(out):
    import os
    from PIL import Image
    os.makedirs(out, exist_ok=True)
    NAVY = (24, 24, 48, 255)
    ims = []
    for i in range(len(PORTRAITS)):
        px, pal = indexed(i)
        im = Image.new("RGBA", (SIZE, SIZE))
        im.putdata([pal[v] for row in px for v in row])
        ims.append(im)
    def strip(cells, scale, gap=2):
        w = cells[0].width
        sh = Image.new("RGBA", (len(cells)*(w + gap) + gap, cells[0].height + 2*gap), NAVY)
        for i, c in enumerate(cells):
            sh.alpha_composite(c, (gap + i*(w + gap), gap))
        return sh.resize((sh.width*scale, sh.height*scale), Image.NEAREST).convert("RGB")
    half = [im.resize((SIZE//2, SIZE//2), Image.NEAREST, box=(0, 0, SIZE, SIZE)) for im in ims]
    for h, im in zip(half, ims):      # exactly the game's decimation: even x, even y
        h.putdata([im.getpixel((x*2, y*2)) for y in range(SIZE//2) for x in range(SIZE//2)])
    strip(ims, 8).save(os.path.join(out, "all8_8x.png"))
    strip(ims, 1).save(os.path.join(out, "all8_1x.png"))
    strip(half, 6).save(os.path.join(out, "all8_half_6x.png"))
    print("wrote portrait previews -> %s" % out)

if __name__ == "__main__":
    import sys
    _main(sys.argv[1] if len(sys.argv) > 1 else ".")
