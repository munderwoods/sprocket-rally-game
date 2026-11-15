// The art file: every sprite's pixels and palette, in assets/art.bin, loaded into the names
// art.h declares. Loading again repoints the same names at the new file's data, which is
// how a sprite changes while the game runs. No engine here: the test tools load it too.
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace sr {

namespace {

// One entry of the file's table (tools/bake_art.py writes it).
struct ArtEntry {
    char name[32];
    u16  kind, w, h, frames;   // kind 0 a prop, 1 a set piece, 2 a racer's kart frames
    i16  anchor_x, anchor_y;
    f32  world_per_px;
    u32  pal_count, px_offset, px_size, pal_offset;
};
static_assert(sizeof(ArtEntry) == 64, "the art file's entry is 64 bytes");

u8* s_art = nullptr; // the file in hand; the sprites point into it

} // namespace

bool art_load(const char* path, char* err, int cap) {
    auto fail = [&](const char* why, const char* what = "") {
        if (err && cap > 0) snprintf(err, (size_t)cap, "%s: %s%s", path, why, what);
        return false;
    };
    FILE* fp = fopen(path, "rb");
    if (!fp) return fail("cannot be opened");
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 16) { fclose(fp); return fail("is too short to be an art file"); }
    u8* buf = (u8*)malloc((size_t)size);
    if (!buf) { fclose(fp); return fail("is too large to load"); }
    bool whole = fread(buf, 1, (size_t)size, fp) == (size_t)size;
    fclose(fp);
    u32 head[4];
    memcpy(head, buf, sizeof head);
    if (!whole || memcmp(buf, "SRAT", 4) != 0 || head[1] != 1 || (u64)16 + (u64)head[2] * 64 > (u64)size) { free(buf); return fail("is not an art file this build reads"); }
    const u32 count = head[2];
    const int nslots = (int)(sizeof k_art_slots / sizeof k_art_slots[0]);

    // First every name the code draws with is found and checked, so a file that is short of
    // one, or damaged, changes nothing.
    static ArtEntry found[sizeof k_art_slots / sizeof k_art_slots[0]];
    for (int s = 0; s < nslots; ++s) {
        const ArtSlot& slot = k_art_slots[s];
        bool got = false;
        for (u32 i = 0; i < count && !got; ++i) {
            ArtEntry e;
            memcpy(&e, buf + 16 + (usize)i * 64, sizeof e);
            e.name[31] = 0;
            if (strcmp(e.name, slot.name) != 0) continue;
            got = true;
            bool ok = (int)e.kind == slot.kind && e.w > 0 && e.h > 0 && e.frames > 0 && e.pal_count > 0 && e.pal_count <= 256 &&
                      (u64)e.px_size == (u64)e.w * e.h * e.frames && (u64)e.px_offset + e.px_size <= (u64)size &&
                      e.pal_offset % 4 == 0 && (u64)e.pal_offset + (u64)e.pal_count * 4 <= (u64)size;
            if (ok && e.kind != 0) ok = e.pal_count == 32;
            if (ok && e.kind == 2) ok = e.w == KART_SIZE && e.h == KART_SIZE && e.frames == KART_ANGLES;
            for (u32 p = 0; ok && p < e.px_size; ++p) if (buf[e.px_offset + p] >= e.pal_count) ok = false; // a color it has no entry for
            if (!ok) { free(buf); return fail("has a damaged sprite: ", slot.name); }
            found[s] = e;
        }
        if (!got) { free(buf); return fail("has no sprite named ", slot.name); }
    }
    // Then the names are pointed at it, and the file they pointed into before is let go.
    for (int s = 0; s < nslots; ++s) {
        const ArtSlot& slot = k_art_slots[s];
        const ArtEntry& e = found[s];
        const u8* px = buf + e.px_offset;
        const u32* pal = (const u32*)(const void*)(buf + e.pal_offset);
        if (slot.kind == 0) *(PropImg*)slot.at = PropImg{e.w, e.h, px, pal};
        else if (slot.kind == 1) *(SetPieceImg*)slot.at = SetPieceImg{e.w, e.h, e.frames, e.anchor_x, e.anchor_y, e.world_per_px, px, pal};
        else {
            for (int a = 0; a < KART_ANGLES; ++a) k_kart_px[slot.index][a] = px + (usize)a * KART_SIZE * KART_SIZE;
            k_kart_pal[slot.index] = pal;
        }
    }
    free(s_art);
    s_art = buf;
    return true;
}

} // namespace sr
