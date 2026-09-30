// Bottom-center toolbar: a tools row (equipped tool, then the 8 rucksack tool slots) above an items row
// (the item in your hands, then the 8 belongings slots). The first slot of each row is the active one.
//
// D-pad: up/down picks which row has focus, left/right rotates the focused row through its active slot.

#include "modding.h"
#include "recompconfig.h"
#include "recompui.h"

#include "common.h"
#include "assetIndices/sfxs.h"
#include "game/gameAudio.h"
#include "game/gameStatus.h"
#include "game/items.h"
#include "game/player.h"
#include "mainLoop.h"
#include "system/controller.h"

#include "hud.h"
#include "icons.h"

extern void initializePlayerHeldItem(void);

#define ROW_COUNT      2
#define SLOTS_PER_ROW  9
#define SLOT_SIZE     44.0f
#define ICON_MAX      38.0f
#define SLOT_GAP       3.0f
#define ACTIVE_GAP    10.0f

static RecompuiColor col_slot_bg     = { 236, 214, 174, 212 };
static RecompuiColor col_active_edge = { 214, 168,  76, 255 };
static RecompuiColor col_focus_tab   = { 214, 168,  76, 255 };

#define ROW_TOOLS 0
#define ROW_ITEMS 1
#define UNFOCUSED_OPACITY 0.6f

// Which row the D-pad steers. Written by the input hook, drawn by toolbar_update.
static int focused_row = ROW_TOOLS;
static int shown_focus = -1;

static const char* row_captions[ROW_COUNT] = { "T", "I" };
static const IconSheet row_sheets[ROW_COUNT] = { ICON_SHEET_TOOLS, ICON_SHEET_ITEMS };

static RecompuiResource toolbar_root;
static RecompuiResource rows[ROW_COUNT];
static RecompuiResource row_tabs[ROW_COUNT];
static RecompuiResource row_captions_res[ROW_COUNT];
static RecompuiResource slots[ROW_COUNT][SLOTS_PER_ROW];
static RecompuiResource icons[ROW_COUNT][SLOTS_PER_ROW];

// Tools row extras: a stack count (seed and feed bags) and a water meter (watering can).
static RecompuiResource counts[SLOTS_PER_ROW];
static RecompuiResource meters[SLOTS_PER_ROW];
static RecompuiResource meter_fills[SLOTS_PER_ROW];
static int shown_count[SLOTS_PER_ROW];
static int shown_water[SLOTS_PER_ROW];

static RecompuiColor col_meter_trough = {  58,  40,  32, 200 };
static RecompuiColor col_meter_water  = {  86, 124, 180, 255 };

static RecompuiTextureHandle blank_texture;
static float toolbar_scale = 1.0f;

// Last animation shown per slot (-1 = empty, -2 = never set), to skip redundant updates.
static int shown_anim[ROW_COUNT][SLOTS_PER_ROW];

void toolbar_init(void) {
    static u8 transparent_pixel[4] = { 0, 0, 0, 0 };
    blank_texture = recompui_create_texture_rgba32(transparent_pixel, 1, 1);

    toolbar_root = recompui_create_element(hud_context, hud_root);
    recompui_set_position(toolbar_root, POSITION_ABSOLUTE);
    recompui_set_left(toolbar_root, 0.0f, UNIT_DP);
    recompui_set_right(toolbar_root, 0.0f, UNIT_DP);
    recompui_set_display(toolbar_root, DISPLAY_FLEX);
    recompui_set_flex_direction(toolbar_root, FLEX_DIRECTION_COLUMN);
    recompui_set_align_items(toolbar_root, ALIGN_ITEMS_CENTER);

    for (int r = 0; r < ROW_COUNT; r++) {
        rows[r] = recompui_create_element(hud_context, toolbar_root);
        recompui_set_display(rows[r], DISPLAY_FLEX);
        recompui_set_flex_direction(rows[r], FLEX_DIRECTION_ROW);
        recompui_set_align_items(rows[r], ALIGN_ITEMS_CENTER);

        row_tabs[r] = recompui_create_element(hud_context, rows[r]);
        style_panel(row_tabs[r]);
        row_captions_res[r] = make_label(row_tabs[r], row_captions[r]);

        for (int i = 0; i < SLOTS_PER_ROW; i++) {
            RecompuiResource slot = recompui_create_element(hud_context, rows[r]);
            recompui_set_display(slot, DISPLAY_FLEX);
            recompui_set_justify_content(slot, JUSTIFY_CONTENT_CENTER);
            recompui_set_align_items(slot, ALIGN_ITEMS_CENTER);
            recompui_set_background_color(slot, &col_slot_bg);
            recompui_set_border_color(slot, i == 0 ? &col_active_edge : &col_panel_border);
            slots[r][i] = slot;

            icons[r][i] = recompui_create_imageview(hud_context, slot, blank_texture);
            recompui_set_display(icons[r][i], DISPLAY_NONE);
            shown_anim[r][i] = -2;

            if (r == ROW_TOOLS) {
                recompui_set_position(slot, POSITION_RELATIVE);

                counts[i] = make_label(slot, "");
                recompui_set_position(counts[i], POSITION_ABSOLUTE);
                recompui_set_font_weight(counts[i], 700);
                recompui_set_display(counts[i], DISPLAY_NONE);
                shown_count[i] = -2;

                meters[i] = recompui_create_element(hud_context, slot);
                recompui_set_position(meters[i], POSITION_ABSOLUTE);
                recompui_set_background_color(meters[i], &col_meter_trough);
                meter_fills[i] = recompui_create_element(hud_context, meters[i]);
                recompui_set_position(meter_fills[i], POSITION_ABSOLUTE);
                recompui_set_left(meter_fills[i], 0.0f, UNIT_DP);
                recompui_set_top(meter_fills[i], 0.0f, UNIT_DP);
                recompui_set_bottom(meter_fills[i], 0.0f, UNIT_DP);
                recompui_set_background_color(meter_fills[i], &col_meter_water);
                recompui_set_display(meters[i], DISPLAY_NONE);
                shown_water[i] = -2;
            }
        }
    }
}

void toolbar_layout(float s) {
    toolbar_scale = s;
    recompui_set_bottom(toolbar_root, EDGE_MARGIN * s, UNIT_DP);
    recompui_set_gap(toolbar_root, SLOT_GAP * s, UNIT_DP);

    for (int r = 0; r < ROW_COUNT; r++) {
        recompui_set_gap(rows[r], SLOT_GAP * s, UNIT_DP);

        recompui_set_width(row_tabs[r], 22.0f * s, UNIT_DP);
        recompui_set_margin_right(row_tabs[r], 4.0f * s, UNIT_DP);
        recompui_set_border_width(row_tabs[r], 3.0f * s, UNIT_DP);
        recompui_set_border_radius(row_tabs[r], 4.0f * s, UNIT_DP);
        recompui_set_font_size(row_captions_res[r], 16.0f * s, UNIT_DP);
        recompui_set_line_height(row_captions_res[r], 20.0f * s, UNIT_DP);

        for (int i = 0; i < SLOTS_PER_ROW; i++) {
            recompui_set_width(slots[r][i], SLOT_SIZE * s, UNIT_DP);
            recompui_set_height(slots[r][i], SLOT_SIZE * s, UNIT_DP);
            recompui_set_border_width(slots[r][i], (i == 0 ? 4.0f : 3.0f) * s, UNIT_DP);
            recompui_set_border_radius(slots[r][i], 6.0f * s, UNIT_DP);
            shown_anim[r][i] = -2;   // re-apply icon sizes at the new scale
        }
        recompui_set_margin_right(slots[r][0], (ACTIVE_GAP - SLOT_GAP) * s, UNIT_DP);
    }

    for (int i = 0; i < SLOTS_PER_ROW; i++) {
        recompui_set_right(counts[i], 3.0f * s, UNIT_DP);
        recompui_set_bottom(counts[i], 0.0f, UNIT_DP);
        recompui_set_font_size(counts[i], 14.0f * s, UNIT_DP);
        recompui_set_line_height(counts[i], 16.0f * s, UNIT_DP);

        recompui_set_left(meters[i], 5.0f * s, UNIT_DP);
        recompui_set_right(meters[i], 5.0f * s, UNIT_DP);
        recompui_set_bottom(meters[i], 3.0f * s, UNIT_DP);
        recompui_set_height(meters[i], 4.0f * s, UNIT_DP);
        recompui_set_border_radius(meters[i], 2.0f * s, UNIT_DP);
        recompui_set_border_radius(meter_fills[i], 2.0f * s, UNIT_DP);
    }
    shown_focus = -1;
}

// How many uses are left in a consumable tool (seed and feed bags), or -1 if it isn't one.
static int tool_quantity(u8 tool) {
    switch (tool) {
        case TURNIP_SEEDS:        return turnipSeedsQuantity;
        case POTATO_SEEDS:        return potatoSeedsQuantity;
        case CABBAGE_SEEDS:       return cabbageSeedsQuantity;
        case TOMATO_SEEDS:        return tomatoSeedsQuantity;
        case CORN_SEEDS:          return cornSeedsQuantity;
        case EGGPLANT_SEEDS:      return eggplantSeedsQuantity;
        case STRAWBERRY_SEEDS:    return strawberrySeedsQuantity;
        case MOON_DROP_SEEDS:     return moondropSeedsQuantity;
        case PINK_CAT_MINT_SEEDS: return pinkCatMintSeedsQuantity;
        case BLUE_MIST_SEEDS:     return blueMistSeedsQuantity;
        case GRASS_SEEDS:         return grassSeedsQuantity;
        case CHICKEN_FEED:        return chickenFeedQuantity;
        default:                  return -1;
    }
}

// Water left as a percentage, or -1. A full can holds 30 / 50 / 80 uses by upgrade level (the
// refill amounts in player.c), and each watered tile uses one.
static int water_percent(u8 tool) {
    static const int capacity[3] = { 30, 50, 80 };
    if (tool != WATERING_CAN) {
        return -1;
    }
    int level = gPlayer.toolLevels[WATERING_CAN - 1];
    int cap = capacity[level < 3 ? level : 2];
    int uses = wateringCanUses < cap ? wateringCanUses : cap;
    return uses * 100 / cap;
}

static void show_tool_extras(int i, u8 tool) {
    int count = tool_quantity(tool);
    if (count != shown_count[i]) {
        shown_count[i] = count;
        if (count < 0) {
            recompui_set_display(counts[i], DISPLAY_NONE);
        } else {
            char buf[8];
            int n = 0;
            int v = count > 999 ? 999 : count;
            if (v >= 100) buf[n++] = (char)('0' + v / 100);
            if (v >= 10) buf[n++] = (char)('0' + (v / 10) % 10);
            buf[n++] = (char)('0' + v % 10);
            buf[n] = '\0';
            recompui_set_text(counts[i], buf);
            recompui_set_display(counts[i], DISPLAY_BLOCK);
        }
    }

    int water = water_percent(tool);
    if (water != shown_water[i]) {
        shown_water[i] = water;
        if (water < 0) {
            recompui_set_display(meters[i], DISPLAY_NONE);
        } else {
            recompui_set_width(meter_fills[i], (float)water, UNIT_PERCENT);
            recompui_set_display(meters[i], DISPLAY_BLOCK);
        }
    }
}

// The focused row has a gold caption tab; the other row is dimmed (see toolbar_apply_opacity).
static void show_focus(void) {
    if (shown_focus == focused_row) {
        return;
    }
    shown_focus = focused_row;
    for (int r = 0; r < ROW_COUNT; r++) {
        recompui_set_background_color(row_tabs[r], r == focused_row ? &col_focus_tab : &col_panel_bg);
    }
}

// Row opacity has to include the HUD fade itself: RmlUi opacity is inherited, so a row that sets its
// own value would otherwise stay visible when the rest of the HUD fades out.
void toolbar_apply_opacity(float hud_fade) {
    for (int r = 0; r < ROW_COUNT; r++) {
        recompui_set_opacity(rows[r], hud_fade * (r == focused_row ? 1.0f : UNFOCUSED_OPACITY));
    }
}

void toolbar_set_visible(int visible) {
    recompui_set_display(toolbar_root, visible ? DISPLAY_FLEX : DISPLAY_NONE);
}

static void show_icon(int r, int i, int anim) {
    if (anim == shown_anim[r][i]) {
        return;
    }
    shown_anim[r][i] = anim;

    RecompuiTextureHandle texture;
    int w, h;
    if (!icon_lookup(row_sheets[r], anim, &texture, &w, &h)) {
        recompui_set_display(icons[r][i], DISPLAY_NONE);
        return;
    }

    // Fit the longer side to ICON_MAX, keeping the pixel-art aspect ratio.
    float fit = ICON_MAX / (float)(w > h ? w : h);
    recompui_set_imageview_texture(icons[r][i], texture);
    recompui_set_width(icons[r][i], w * fit * toolbar_scale, UNIT_DP);
    recompui_set_height(icons[r][i], h * fit * toolbar_scale, UNIT_DP);
    recompui_set_display(icons[r][i], DISPLAY_BLOCK);
}

void toolbar_update(void) {
#ifdef DEBUG_TOOL_EXTRAS
    // Test-only: seed a turnip bag and a partly filled can in memory (never saved by the test).
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        gPlayer.toolSlots[5] = TURNIP_SEEDS;
        turnipSeedsQuantity = 7;
        wateringCanUses = 12;
    }
#endif
    show_focus();
    show_icon(0, 0, icon_anim_for_tool(gPlayer.currentTool));
    show_tool_extras(0, gPlayer.currentTool);
    show_icon(1, 0, icon_anim_for_item(gPlayer.heldItem));

    // Show the rucksack packed left, skipping empty slots. Equipping from the pause menu leaves a
    // hole where the tool or item used to sit; drawing slots in place made that look like a gap
    // right after the hand slot. Packed order is also the order the D-pad rotates through.
    int t = 1, b = 1;
    for (int i = 0; i < 8; i++) {
        if (gPlayer.toolSlots[i] != 0) {
            show_icon(0, t, icon_anim_for_tool(gPlayer.toolSlots[i]));
            show_tool_extras(t, gPlayer.toolSlots[i]);
            t++;
        }
        if (gPlayer.belongingsSlots[i] != 0) {
            show_icon(1, b, icon_anim_for_item(gPlayer.belongingsSlots[i]));
            b++;
        }
    }
    for (; t < SLOTS_PER_ROW; t++) {
        show_icon(0, t, -1);
        show_tool_extras(t, 0);
    }
    for (; b < SLOTS_PER_ROW; b++) {
        show_icon(1, b, -1);
    }
}

// --- Switching ---

// Rotates a row "through the hand": the hand and every occupied slot form a ring, and each press
// shifts the ring by one so the next thing lands in the hand and the old one goes back in line. The
// order is stable, so pressing the same way N times comes back around. With include_empty_stop, one
// empty slot joins the ring too, so the items row can cycle to empty hands (putting the item away).
// Returns 1 if the hand changed.
static int rotate_row(u8* hand, u8* slot_values, int slot_count, int direction, int include_empty_stop) {
    u8* ring[1 + 8];
    u8 values[1 + 8];
    int n = 0;

    // The empty stop is the first empty slot, kept at its own position in the ring so the cyclic
    // order never changes. When the hand is already empty, the hand itself is the empty stop.
    int empty_stop = -1;
    if (include_empty_stop && *hand != 0) {
        for (int i = 0; i < slot_count; i++) {
            if (slot_values[i] == 0) {
                empty_stop = i;
                break;
            }
        }
    }

    ring[n++] = hand;
    for (int i = 0; i < slot_count; i++) {
        if (slot_values[i] != 0 || i == empty_stop) {
            ring[n++] = &slot_values[i];
        }
    }
    if (n < 2) {
        return 0;
    }

    for (int j = 0; j < n; j++) {
        values[j] = *ring[j];
    }
    for (int j = 0; j < n; j++) {
        *ring[j] = values[(j + direction + n) % n];
    }
    return 1;
}

static void switch_tool(int direction) {
    if (rotate_row(&gPlayer.currentTool, gPlayer.toolSlots, 8, direction, FALSE)) {
        playSfx(MOVE_CURSOR);
    }
}

static void switch_item(int direction) {
    u8 old_item = gPlayer.heldItem;

    // Only things that fit in the rucksack can leave your hands (not animals, the baby, etc.).
    if (old_item != 0 && !(getItemFlags(old_item) & ITEM_RUCKSACK_STORABLE)) {
        playSfx(INVALID_BUZZ);
        return;
    }
    if (!rotate_row(&gPlayer.heldItem, gPlayer.belongingsSlots, 8, direction, TRUE)) {
        return;
    }

    // Same steps the game takes when the pause menu closes: drop the old held object, then let
    // initializePlayerHeldItem spawn the new one (or clear the slot for empty hands).
    if (old_item != 0) {
        clearHeldItemSlot(gPlayer.itemInfoIndex);
        gItemBeingHeld = 0xFF;
    }
    initializePlayerHeldItem();
    playSfx(PICKING_UP_SFX);
}

static int switching_allowed(void) {
    return recomp_get_config_u32("hud_enabled") == 0
        && recomp_get_config_u32("show_toolbar") == 0
        && mainLoopCallbackCurrentIndex == MAIN_GAME
        && !(gPlayer.flags & PLAYER_RIDING_HORSE)
        && !checkDailyEventBit(BLOCK_BUTTON_USAGE);
}

// handlePlayerInput only runs while the player is idle (actionHandler == 0) and in control (not taken
// over by a cutscene), so switching can't interrupt a swing, throw, meal, or scripted scene. There's
// no CUTSCENE_ACTIVE check: background scripts like Greg's by the mountain pond set it while you're
// free to act. The game doesn't use the D-pad in free roam.
RECOMP_HOOK("handlePlayerInput")
void StardewHud_OnPlayerInput(void) {
    if (!switching_allowed()) {
        return;
    }

    if (checkButtonPressed(CONTROLLER_1, BUTTON_D_UP) && focused_row != ROW_TOOLS) {
        focused_row = ROW_TOOLS;
        playSfx(MOVE_CURSOR);
    } else if (checkButtonPressed(CONTROLLER_1, BUTTON_D_DOWN) && focused_row != ROW_ITEMS) {
        focused_row = ROW_ITEMS;
        playSfx(MOVE_CURSOR);
    }

    int direction = 0;
    if (checkButtonPressed(CONTROLLER_1, BUTTON_D_RIGHT)) {
        direction = 1;
    } else if (checkButtonPressed(CONTROLLER_1, BUTTON_D_LEFT)) {
        direction = -1;
    }
    if (direction != 0) {
        if (focused_row == ROW_TOOLS) {
            switch_tool(direction);
        } else {
            switch_item(direction);
        }
    }
}
