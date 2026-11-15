"""Minimal SoundFont 2 reader: presets -> instrument zones -> samples (16-bit PCM).

Enough for Sprocket Rally's sample baker: for each preset, the sample zones with their key
ranges, root key, fine tune, loop points and sample rate.
"""
import struct

GEN_KEYRANGE = 43
GEN_INSTRUMENT = 41
GEN_SAMPLEID = 53
GEN_OVERRIDING_ROOT = 58
GEN_FINETUNE = 52
GEN_COARSETUNE = 51
GEN_SAMPLEMODES = 54
GEN_STARTLOOP_OFS = 2
GEN_ENDLOOP_OFS = 3
GEN_STARTLOOP_COARSE = 45
GEN_ENDLOOP_COARSE = 50
GEN_ATTENUATION = 48
GEN_RELEASE = 38
GEN_PAN = 17


def _chunks(data, off, end):
    while off + 8 <= end:
        cid, size = struct.unpack_from("<4sI", data, off)
        yield cid, off + 8, size
        off += 8 + size + (size & 1)


class SF2:
    def __init__(self, path):
        data = open(path, "rb").read()
        assert data[:4] == b"RIFF" and data[8:12] == b"sfbk"
        self.smpl = b""
        pd = {}
        for cid, off, size in _chunks(data, 12, len(data)):
            if cid != b"LIST":
                continue
            kind = data[off:off + 4]
            for sid, soff, ssize in _chunks(data, off + 4, off + size):
                if kind == b"sdta" and sid == b"smpl":
                    self.smpl = data[soff:soff + ssize]
                elif kind == b"pdta":
                    pd[sid.decode()] = data[soff:soff + ssize]
        self.phdr = [struct.unpack_from("<20sHHHIII", pd["phdr"], i) for i in range(0, len(pd["phdr"]), 38)]
        self.pbag = [struct.unpack_from("<HH", pd["pbag"], i) for i in range(0, len(pd["pbag"]), 4)]
        self.pgen = [struct.unpack_from("<Hh", pd["pgen"], i) for i in range(0, len(pd["pgen"]), 4)]
        self.inst = [struct.unpack_from("<20sH", pd["inst"], i) for i in range(0, len(pd["inst"]), 22)]
        self.ibag = [struct.unpack_from("<HH", pd["ibag"], i) for i in range(0, len(pd["ibag"]), 4)]
        self.igen = [struct.unpack_from("<Hh", pd["igen"], i) for i in range(0, len(pd["igen"]), 4)]
        self.shdr = [struct.unpack_from("<20sIIIIIBbHH", pd["shdr"], i) for i in range(0, len(pd["shdr"]), 46)]

    @staticmethod
    def _name(b):
        return b.split(b"\0")[0].decode("latin-1").strip()

    def _gens(self, gens, a, b):
        out = {}
        for i in range(a, b):
            op, amt = gens[i]
            out[op] = amt
        return out

    def presets(self):
        res = []
        for i in range(len(self.phdr) - 1):
            name, prog, bank, bag0 = self._name(self.phdr[i][0]), self.phdr[i][1], self.phdr[i][2], self.phdr[i][3]
            bag1 = self.phdr[i + 1][3]
            zones = []
            for b in range(bag0, bag1):
                g = self._gens(self.pgen, self.pbag[b][0], self.pbag[b + 1][0])
                if GEN_INSTRUMENT in g:
                    zones.extend(self.instrument_zones(g[GEN_INSTRUMENT], g))
            res.append({"name": name, "program": prog, "bank": bank, "zones": zones})
        return res

    def instrument_zones(self, idx, pgens):
        name = self._name(self.inst[idx][0])
        b0, b1 = self.inst[idx][1], self.inst[idx + 1][1]
        glob = {}
        zones = []
        for b in range(b0, b1):
            g = self._gens(self.igen, self.ibag[b][0], self.ibag[b + 1][0])
            if GEN_SAMPLEID not in g:
                glob = g
                continue
            gg = dict(glob); gg.update(g)
            s = self.shdr[gg[GEN_SAMPLEID]]
            sname, start, end, ls, le, rate, pitch, corr = self._name(s[0]), s[1], s[2], s[3], s[4], s[5], s[6], s[7]
            kr = gg.get(GEN_KEYRANGE, 0x7F00)
            lo, hi = kr & 0xFF, (kr >> 8) & 0xFF
            root = gg.get(GEN_OVERRIDING_ROOT, -1)
            if root < 0:
                root = pitch
            ls += gg.get(GEN_STARTLOOP_OFS, 0) + 32768 * gg.get(GEN_STARTLOOP_COARSE, 0)
            le += gg.get(GEN_ENDLOOP_OFS, 0) + 32768 * gg.get(GEN_ENDLOOP_COARSE, 0)
            pcm = struct.unpack_from("<%dh" % (end - start), self.smpl, start * 2)
            zones.append({
                "inst": name, "sample": sname, "lo": lo, "hi": hi, "root": root,
                "tune": corr + gg.get(GEN_FINETUNE, 0) + 100 * gg.get(GEN_COARSETUNE, 0),
                "loop": (gg.get(GEN_SAMPLEMODES, 0) & 1) == 1, "ls": ls - start, "le": le - start,
                "rate": rate, "pcm": pcm, "atten": gg.get(GEN_ATTENUATION, 0) / 10.0,
            })
        return zones


if __name__ == "__main__":
    import sys
    for path in sys.argv[1:]:
        sf = SF2(path)
        print("==", path)
        for p in sf.presets():
            zs = p["zones"]
            desc = ", ".join("%s[%d-%d r%d %s %dHz %d]" % (z["sample"][:14], z["lo"], z["hi"], z["root"], "L" if z["loop"] else "-", z["rate"], len(z["pcm"])) for z in zs[:3])
            print("  %3d:%3d %-22s %s%s" % (p["bank"], p["program"], p["name"], desc, " ..." if len(zs) > 3 else ""))
