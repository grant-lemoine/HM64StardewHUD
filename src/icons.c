// Tool and item icons, decoded at runtime from the player's own ROM so no game art ships with the mod.
//
// HM64 keeps the pause menu's icons in two "type 1" sprite sheets (tools and holdable items). Each has:
//   - an asset index: offsets (from the texture base) to the palette, animation, and sprite-to-palette blocks
//   - a separate spritesheet index: offsets (from the texture base) to each sprite image
// A tool or item maps to an animation through the game's own tables. Frame 0 of that animation names a
// spritesheet index; the image there is an 8-byte header (format flag, width, height) plus CI4/CI8
// pixels, and its palette (RGBA5551) comes from the sprite-to-palette table.

#include "modding.h"
#include "recomputils.h"
#include "recompui.h"

#include "common.h"
#include "game/items.h"
#include "game/player.h"
#include "data/animation/entityAnimationScripts/entityAnimationScripts.h"

#include "icons.h"

extern void nuPiReadRom(u32 rom_addr, void* buf_ptr, u32 size);
extern u16 getAnimationOffsetFromScript(u16* vaddr, u16 offset);
extern u8 getToolLevelForAnimation(u8 tool);
extern volatile u16 toolAnimationIndices[];

// ROM layout of the two sheets (US ROM), from the decomp's sprite_addresses.csv.
typedef struct {
    u32 texture;
    u32 asset_index;
    u32 sheet_index;
    u32 sheet_index_end;
} SheetRom;

static const SheetRom sheet_rom[ICON_SHEET_COUNT] = {
    [ICON_SHEET_TOOLS] = { 0xD4DAB0, 0xD52590, 0xD525B0, 0xD52670 },
    [ICON_SHEET_ITEMS] = { 0xD52670, 0xD82FD0, 0xD82FF0, 0xD835A0 },
};

#define MAX_ANIMATIONS 512
#define MAX_ICON_DIM   64

typedef struct {
    u8 loaded;
    u8* palettes;        // palette block
    u8* animations;      // animation block
    u8* sprite_palette;  // sprite-to-palette block
    u8* sheet_offsets;   // spritesheet index
    u32 palettes_size, animations_size, sprite_palette_size, sheet_count;
} Sheet;

typedef struct {
    RecompuiTextureHandle texture;
    u8 state;            // 0 = not tried, 1 = ok, 2 = failed
    u8 width, height;
} CachedIcon;

static Sheet sheets[ICON_SHEET_COUNT];
static CachedIcon cache[ICON_SHEET_COUNT][MAX_ANIMATIONS];

// --- Byte helpers. Mod memory reads as big-endian; some sprite fields are little-endian. ---

static u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }
static u16 le16(const u8* p) { return (u16)(p[0] | p[1] << 8); }

// nuPiReadRom needs an 8-byte aligned destination and an even ROM address; allocate with slack and
// read from the aligned-down address so any ROM offset works. *allocation receives the pointer to
// pass to recomp_free (NULL to keep the data for the session).
static u8* read_rom(u32 rom_addr, u32 size, void** allocation) {
    u32 start = rom_addr & ~1u;
    u32 lead = rom_addr - start;
    u32 total = (size + lead + 1) & ~1u;
    u8* raw = recomp_alloc(total + 16);
    u8* buf = (u8*)(((u32)raw + 7) & ~7u);
    nuPiReadRom(start, buf, total);
    if (allocation != NULL) {
        *allocation = raw;
    }
    return buf + lead;
}

static int load_sheet(IconSheet id) {
    Sheet* s = &sheets[id];
    const SheetRom* r = &sheet_rom[id];
    if (s->loaded) {
        return s->loaded == 1;
    }

    void* index_alloc;
    u8* index = read_rom(r->asset_index, 32, &index_alloc);
    u32 off_palette = be32(index + 4);
    u32 off_anim = be32(index + 8);
    u32 off_s2p = be32(index + 12);
    u32 off_end = be32(index + 16);
    recomp_free(index_alloc);

    if (off_palette >= off_anim || off_anim >= off_s2p || off_s2p >= off_end || off_end > 0x100000) {
        s->loaded = 2;
        return 0;
    }

    s->palettes_size = off_anim - off_palette;
    s->animations_size = off_s2p - off_anim;
    s->sprite_palette_size = off_end - off_s2p;
    s->palettes = read_rom(r->texture + off_palette, s->palettes_size, NULL);
    s->animations = read_rom(r->texture + off_anim, s->animations_size, NULL);
    s->sprite_palette = read_rom(r->texture + off_s2p, s->sprite_palette_size, NULL);
    s->sheet_count = (r->sheet_index_end - r->sheet_index) / 4;
    s->sheet_offsets = read_rom(r->sheet_index, s->sheet_count * 4, NULL);
    s->loaded = 1;
    return 1;
}

// Frame 0's first bitmap for an animation, or -1.
static int first_sprite(Sheet* s, u16 anim) {
    u32 count = be32(s->animations) / 4;
    if (anim + 1 >= count) {
        return -1;
    }
    u32 a0 = be32(s->animations + anim * 4);
    u32 a1 = be32(s->animations + (anim + 1) * 4);
    if (a0 == a1 || a0 + 16 > s->animations_size) {
        return -1;
    }
    const u8* frame = s->animations + a0 + 8;   // skip 4-byte header and frame count
    if (le16(frame) == 0) {
        return -1;
    }
    return le16(frame + 4);                       // BitmapMetadata.spritesheetIndex
}

static int decode(IconSheet id, u16 anim, CachedIcon* out) {
    Sheet* s = &sheets[id];
    int sprite = first_sprite(s, anim);
    if (sprite < 0 || (u32)sprite + 1 >= s->sheet_count) {
        return 0;
    }

    u32 off = be32(s->sheet_offsets + sprite * 4);
    u32 next = be32(s->sheet_offsets + (sprite + 1) * 4);
    if (next <= off + 8) {
        return 0;
    }

    void* image_alloc;
    u8* image = read_rom(sheet_rom[id].texture + off, next - off, &image_alloc);
    int ci4 = image[3] == 0x10;
    int w = (s16)le16(image + 4);
    int h = (s16)le16(image + 6);
    u32 pixel_bytes = ci4 ? (u32)(w * h + 1) / 2 : (u32)(w * h);
    if (w <= 0 || h <= 0 || w > MAX_ICON_DIM || h > MAX_ICON_DIM || 8 + pixel_bytes > next - off) {
        recomp_free(image_alloc);
        return 0;
    }

    u32 palette_index = s->sprite_palette[4 + sprite];
    u32 palette_off = be32(s->palettes + palette_index * 4) + 4;   // skip palette header
    const u8* palette = s->palettes + palette_off;

    u8* rgba = recomp_alloc(w * h * 4);
    const u8* pixels = image + 8;
    for (int i = 0; i < w * h; i++) {
        int ci = ci4 ? (pixels[i / 2] >> ((i & 1) ? 0 : 4)) & 0xF : pixels[i];
        u16 c = be16(palette + ci * 2);
        u8 r5 = (c >> 11) & 31, g5 = (c >> 6) & 31, b5 = (c >> 1) & 31;
        rgba[i * 4 + 0] = (u8)((r5 << 3) | (r5 >> 2));
        rgba[i * 4 + 1] = (u8)((g5 << 3) | (g5 >> 2));
        rgba[i * 4 + 2] = (u8)((b5 << 3) | (b5 >> 2));
        rgba[i * 4 + 3] = (c & 1) ? 255 : 0;
    }

    out->texture = recompui_create_texture_rgba32(rgba, w, h);
    out->width = (u8)w;
    out->height = (u8)h;
    recomp_free(rgba);
    recomp_free(image_alloc);
    return 1;
}

static const CachedIcon* get_icon(IconSheet id, u16 anim) {
    if (anim >= MAX_ANIMATIONS || !load_sheet(id)) {
        return NULL;
    }
    CachedIcon* c = &cache[id][anim];
    if (c->state == 0) {
        c->state = decode(id, anim, c) ? 1 : 2;
    }
    return c->state == 1 ? c : NULL;
}

// --- Public API ---

int icon_anim_for_tool(u8 tool) {
    if (tool == 0 || tool >= MAX_TOOLS) {
        return -1;
    }
    return getAnimationOffsetFromScript(toolsAnimationScripts, toolAnimationIndices[tool] + getToolLevelForAnimation(tool));
}

int icon_anim_for_item(u8 item) {
    if (item == 0) {
        return -1;
    }
    return getAnimationOffsetFromScript(heldItemsAnimationScripts, getItemAnimationIndex(item));
}

int icon_lookup(IconSheet sheet, int anim, RecompuiTextureHandle* texture, int* width, int* height) {
    if (anim < 0) {
        return 0;
    }
    const CachedIcon* c = get_icon(sheet, (u16)anim);
    if (c == NULL) {
        return 0;
    }
    *texture = c->texture;
    *width = c->width;
    *height = c->height;
    return 1;
}
