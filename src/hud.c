// Stardew Valley-style HUD for Harvest Moon 64: Recompiled.
//
// Phase 1: a clock/date/weather panel and gold counter in the top-right corner, and vertical
// health and energy (stamina) bars in the bottom-right corner. Everything is drawn with RecompUI
// on top of the game, so it's crisp at any resolution and anchors to the real window edges in
// widescreen. The game's own state is read directly from its globals every frame.

#include "modding.h"
#include "recompconfig.h"
#include "recompui.h"

#include "common.h"
#include "game/cutscenes.h"
#include "game/game.h"
#include "game/player.h"
#include "game/time.h"
#include "game/weather.h"
#include "mainLoop.h"

// --- Palette (Stardew's parchment-and-wood look) ---

static RecompuiColor col_panel_bg     = { 255, 214, 147, 245 }; // parchment
static RecompuiColor col_panel_border = { 133,  67,  28, 255 }; // dark wood
static RecompuiColor col_text         = {  86,  22,  12, 255 }; // dark brown ink
static RecompuiColor col_bar_trough   = {  60,  30,  20, 220 };
static RecompuiColor col_energy_high  = {  95, 190,  60, 255 };
static RecompuiColor col_energy_mid   = { 230, 200,  40, 255 };
static RecompuiColor col_energy_low   = { 220,  60,  40, 255 };

// --- Layout constants, in DP before scaling ---

#define EDGE_MARGIN        16.0f
#define PANEL_BORDER        4.0f
#define PANEL_RADIUS        8.0f
#define CLOCK_PANEL_WIDTH 170.0f
#define BAR_WIDTH          26.0f
#define BAR_GAP            10.0f
// The energy bar grows with max stamina (100 at the start, up to MAX_STAMINA with power berries).
#define BAR_HEIGHT_MIN    140.0f
#define BAR_HEIGHT_MAX    260.0f
#define HEALTH_BAR_HEIGHT BAR_HEIGHT_MIN

// Health is the inverse of the game's hidden fatigue counter (health = 100 - fatigue). The game warns
// at fatigue 50 and 75 and gives you a sick day the morning after fatigue hits 100, so the bar is
// green above the first warning, yellow at it, and red (pulsing) at the second.
#define HEALTH_WARN_1        50
#define HEALTH_WARN_2        25
#define HEALTH_PULSE_BELOW   HEALTH_WARN_2
#define HEALTH_PULSE_FRAMES  40

// The clock display rounds down to this many minutes.
#define CLOCK_STEP_MINUTES 30

// Fraction of full opacity gained/lost per frame when the HUD shows or hides.
#define FADE_STEP 0.12f

// --- UI handles ---

static RecompuiContext hud_context;
static RecompuiResource hud_root;

static RecompuiResource clock_panel;
static RecompuiResource date_label;
static RecompuiResource weather_row;
static RecompuiResource weather_dot;
static RecompuiResource season_label;
static RecompuiResource time_label;

static RecompuiResource gold_panel;
static RecompuiResource gold_label;

static RecompuiResource bars_row;
static RecompuiResource health_column;
static RecompuiResource health_trough;
static RecompuiResource health_fill;
static RecompuiResource health_tab;
static RecompuiResource health_caption;
static RecompuiResource energy_column;
static RecompuiResource energy_trough;
static RecompuiResource energy_fill;
static RecompuiResource energy_tab;
static RecompuiResource energy_caption;

static int hud_initialized = 0;
static float hud_scale = -1.0f;
static float hud_opacity = 0.0f;
static int health_shown = -1;
static int pulse_frame = 0;

// Last values pushed to the UI, so text and layout are only touched when something changes.
static int last_day_of_week = -1;
static int last_day_of_month = -1;
static int last_season = -1;
static int last_weather = -1;
static int last_hour = -1;
static int last_minutes = -1;
static s64 last_gold = -1;
static int last_health = -1;
static int last_stamina = -1;
static int last_max_stamina = -1;

// --- Small string helpers (mods have no libc) ---

static void str_append(char* buf, int* len, int cap, const char* text) {
    while (*text != '\0' && *len < cap - 1) {
        buf[(*len)++] = *text++;
    }
    buf[*len] = '\0';
}

static void str_append_u32(char* buf, int* len, int cap, u32 value, int group_thousands) {
    char digits[16];
    int count = 0;

    do {
        if (group_thousands && count % 4 == 3) {
            digits[count++] = ',';
        }
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (count > 0 && *len < cap - 1) {
        buf[(*len)++] = digits[--count];
    }
    buf[*len] = '\0';
}

static const char* day_abbrev(u8 day) {
    static const char* names[] = { "Sun.", "Mon.", "Tue.", "Wed.", "Thu.", "Fri.", "Sat." };
    return day <= SATURDAY ? names[day] : "???";
}

static const char* season_name(u8 season) {
    switch (season) {
        case SPRING: return "Spring";
        case SUMMER: return "Summer";
        case AUTUMN: return "Fall";
        case WINTER: return "Winter";
        default:     return "";
    }
}

// Stand-in for a weather icon until Phase 2 adds real artwork: a colored dot.
static RecompuiColor weather_color(u8 weather) {
    RecompuiColor sunny   = { 250, 190,  40, 255 };
    RecompuiColor rain    = {  70, 130, 220, 255 };
    RecompuiColor snow    = { 235, 240, 250, 255 };
    RecompuiColor typhoon = { 100, 100, 120, 255 };
    RecompuiColor unknown = { 160, 160, 160, 255 };

    switch (weather) {
        case SUNNY:   return sunny;
        case RAIN:    return rain;
        case SNOW:    return snow;
        case TYPHOON: return typhoon;
        default:      return unknown;
    }
}

static u8 lerp_u8(u8 a, u8 b, float t) {
    return (u8)(a + (b - a) * t);
}

static RecompuiColor lerp_color(const RecompuiColor* a, const RecompuiColor* b, float t) {
    RecompuiColor out;
    out.r = lerp_u8(a->r, b->r, t);
    out.g = lerp_u8(a->g, b->g, t);
    out.b = lerp_u8(a->b, b->b, t);
    out.a = lerp_u8(a->a, b->a, t);
    return out;
}

// Green when full, yellow at half, red when nearly empty.
static RecompuiColor energy_color(float fraction) {
    if (fraction >= 0.5f) {
        return lerp_color(&col_energy_mid, &col_energy_high, (fraction - 0.5f) * 2.0f);
    }
    return lerp_color(&col_energy_low, &col_energy_mid, fraction * 2.0f);
}

static float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// --- Config ---

// Enum options are indexed in the order listed in mod.toml, so "On" is 0.
static int config_hud_enabled(void) {
    return recomp_get_config_u32("hud_enabled") == 0;
}

static int config_show_health(void) {
    return recomp_get_config_u32("show_health") == 0;
}

static float config_scale(void) {
    return (float)recomp_get_config_double("hud_scale");
}

// --- Construction ---

static void style_panel(RecompuiResource panel) {
    recompui_set_background_color(panel, &col_panel_bg);
    recompui_set_border_color(panel, &col_panel_border);
    recompui_set_display(panel, DISPLAY_FLEX);
    recompui_set_flex_direction(panel, FLEX_DIRECTION_COLUMN);
    recompui_set_align_items(panel, ALIGN_ITEMS_CENTER);
}

static RecompuiResource make_label(RecompuiResource parent, const char* text) {
    RecompuiResource label = recompui_create_label(hud_context, parent, text, LABELSTYLE_NORMAL);
    recompui_set_color(label, &col_text);
    recompui_set_font_weight(label, 700);
    recompui_set_text_align(label, TEXT_ALIGN_CENTER);
    return label;
}

static RecompuiResource make_bar_column(RecompuiResource parent, const char* caption,
                                        RecompuiResource* trough_out, RecompuiResource* fill_out,
                                        RecompuiResource* tab_out, RecompuiResource* caption_out) {
    RecompuiResource column = recompui_create_element(hud_context, parent);
    recompui_set_display(column, DISPLAY_FLEX);
    recompui_set_flex_direction(column, FLEX_DIRECTION_COLUMN);
    recompui_set_align_items(column, ALIGN_ITEMS_CENTER);

    // Caption tab sits on top of the bar, like Stardew's "E".
    RecompuiResource tab = recompui_create_element(hud_context, column);
    style_panel(tab);
    *tab_out = tab;
    *caption_out = make_label(tab, caption);

    RecompuiResource trough = recompui_create_element(hud_context, column);
    recompui_set_position(trough, POSITION_RELATIVE);
    recompui_set_background_color(trough, &col_bar_trough);
    recompui_set_border_color(trough, &col_panel_border);

    RecompuiResource fill = recompui_create_element(hud_context, trough);
    recompui_set_position(fill, POSITION_ABSOLUTE);
    recompui_set_left(fill, 0.0f, UNIT_DP);
    recompui_set_right(fill, 0.0f, UNIT_DP);
    recompui_set_bottom(fill, 0.0f, UNIT_DP);

    *trough_out = trough;
    *fill_out = fill;
    return column;
}

static void layout_bar(RecompuiResource trough, RecompuiResource tab, RecompuiResource caption, float s) {
    recompui_set_width(trough, BAR_WIDTH * s, UNIT_DP);
    recompui_set_border_width(trough, PANEL_BORDER * s, UNIT_DP);
    recompui_set_border_radius(trough, 4.0f * s, UNIT_DP);
    recompui_set_width(tab, (BAR_WIDTH + 4.0f) * s, UNIT_DP);
    recompui_set_margin_bottom(tab, 2.0f * s, UNIT_DP);
    recompui_set_border_width(tab, 3.0f * s, UNIT_DP);
    recompui_set_border_radius(tab, 4.0f * s, UNIT_DP);
    recompui_set_font_size(caption, 18.0f * s, UNIT_DP);
    recompui_set_line_height(caption, 22.0f * s, UNIT_DP);
}

// Applies every size-dependent property. Called on init and whenever the HUD scale setting changes.
static void apply_layout(float s) {
    // Clock panel, top-right.
    recompui_set_top(clock_panel, EDGE_MARGIN * s, UNIT_DP);
    recompui_set_right(clock_panel, EDGE_MARGIN * s, UNIT_DP);
    recompui_set_width(clock_panel, CLOCK_PANEL_WIDTH * s, UNIT_DP);
    recompui_set_padding(clock_panel, 8.0f * s, UNIT_DP);
    recompui_set_gap(clock_panel, 2.0f * s, UNIT_DP);
    recompui_set_border_width(clock_panel, PANEL_BORDER * s, UNIT_DP);
    recompui_set_border_radius(clock_panel, PANEL_RADIUS * s, UNIT_DP);

    recompui_set_font_size(date_label, 24.0f * s, UNIT_DP);
    recompui_set_line_height(date_label, 30.0f * s, UNIT_DP);

    recompui_set_gap(weather_row, 8.0f * s, UNIT_DP);
    recompui_set_width(weather_dot, 16.0f * s, UNIT_DP);
    recompui_set_height(weather_dot, 16.0f * s, UNIT_DP);
    recompui_set_border_radius(weather_dot, 8.0f * s, UNIT_DP);
    recompui_set_border_width(weather_dot, 2.0f * s, UNIT_DP);
    recompui_set_font_size(season_label, 20.0f * s, UNIT_DP);
    recompui_set_line_height(season_label, 26.0f * s, UNIT_DP);

    recompui_set_font_size(time_label, 26.0f * s, UNIT_DP);
    recompui_set_line_height(time_label, 32.0f * s, UNIT_DP);

    // Gold panel, directly under the clock.
    recompui_set_top(gold_panel, (EDGE_MARGIN + 116.0f) * s, UNIT_DP);
    recompui_set_right(gold_panel, EDGE_MARGIN * s, UNIT_DP);
    recompui_set_width(gold_panel, CLOCK_PANEL_WIDTH * s, UNIT_DP);
    recompui_set_padding(gold_panel, 4.0f * s, UNIT_DP);
    recompui_set_border_width(gold_panel, PANEL_BORDER * s, UNIT_DP);
    recompui_set_border_radius(gold_panel, PANEL_RADIUS * s, UNIT_DP);
    recompui_set_font_size(gold_label, 22.0f * s, UNIT_DP);
    recompui_set_line_height(gold_label, 28.0f * s, UNIT_DP);

    // Health and energy bars, bottom-right.
    recompui_set_bottom(bars_row, EDGE_MARGIN * s, UNIT_DP);
    recompui_set_right(bars_row, EDGE_MARGIN * s, UNIT_DP);
    recompui_set_gap(bars_row, BAR_GAP * s, UNIT_DP);
    layout_bar(health_trough, health_tab, health_caption, s);
    layout_bar(energy_trough, energy_tab, energy_caption, s);
    recompui_set_height(health_trough, HEALTH_BAR_HEIGHT * s, UNIT_DP);

    // Force the energy bar height to be recomputed at the new scale.
    last_max_stamina = -1;
}

static void hud_init(void) {
    hud_context = recompui_create_context();
    recompui_open_context(hud_context);
    // Pure display: never take keyboard, controller, or mouse input away from the game.
    recompui_set_context_captures_input(hud_context, 0);
    recompui_set_context_captures_mouse(hud_context, 0);

    hud_root = recompui_context_root(hud_context);
    recompui_set_position(hud_root, POSITION_ABSOLUTE);
    recompui_set_top(hud_root, 0.0f, UNIT_DP);
    recompui_set_right(hud_root, 0.0f, UNIT_DP);
    recompui_set_bottom(hud_root, 0.0f, UNIT_DP);
    recompui_set_left(hud_root, 0.0f, UNIT_DP);
    recompui_set_width_auto(hud_root);
    recompui_set_height_auto(hud_root);
    recompui_set_padding(hud_root, 0.0f, UNIT_DP);
    recompui_set_opacity(hud_root, 0.0f);

    // Clock/date/weather panel.
    clock_panel = recompui_create_element(hud_context, hud_root);
    recompui_set_position(clock_panel, POSITION_ABSOLUTE);
    style_panel(clock_panel);

    date_label = make_label(clock_panel, "");

    weather_row = recompui_create_element(hud_context, clock_panel);
    recompui_set_display(weather_row, DISPLAY_FLEX);
    recompui_set_flex_direction(weather_row, FLEX_DIRECTION_ROW);
    recompui_set_align_items(weather_row, ALIGN_ITEMS_CENTER);
    weather_dot = recompui_create_element(hud_context, weather_row);
    recompui_set_border_color(weather_dot, &col_panel_border);
    season_label = make_label(weather_row, "");

    time_label = make_label(clock_panel, "");

    // Gold panel.
    gold_panel = recompui_create_element(hud_context, hud_root);
    recompui_set_position(gold_panel, POSITION_ABSOLUTE);
    style_panel(gold_panel);
    gold_label = make_label(gold_panel, "");

    // Health and energy bars side by side, energy rightmost (like Stardew's H and E).
    bars_row = recompui_create_element(hud_context, hud_root);
    recompui_set_position(bars_row, POSITION_ABSOLUTE);
    recompui_set_display(bars_row, DISPLAY_FLEX);
    recompui_set_flex_direction(bars_row, FLEX_DIRECTION_ROW);
    recompui_set_align_items(bars_row, ALIGN_ITEMS_FLEX_END);

    health_column = make_bar_column(bars_row, "H", &health_trough, &health_fill, &health_tab, &health_caption);

    energy_column = make_bar_column(bars_row, "E", &energy_trough, &energy_fill, &energy_tab, &energy_caption);

    recompui_close_context(hud_context);
    recompui_show_context(hud_context);

    hud_initialized = 1;
}

// --- Per-frame update ---

// The HUD shows during free-roam gameplay and conversations, and hides in menus (which have their
// own clock), cutscenes, the title screen, and before a save is loaded.
static int hud_should_show(void) {
    if (gMaximumStamina == 0) {
        return 0;
    }
    if (gCutsceneFlags & CUTSCENE_ACTIVE) {
        return 0;
    }

    switch (mainLoopCallbackCurrentIndex) {
        case MAIN_GAME:
        case DIALOGUE:
        case MESSAGE_BOX:
        case DIALOGUE_SELECTION:
        case SHOP_DIALOGUE:
            return 1;
        default:
            return 0;
    }
}

static void update_clock_panel(void) {
    char buf[32];
    int len;

    if (gDayOfWeek != last_day_of_week || gDayOfMonth != last_day_of_month) {
        last_day_of_week = gDayOfWeek;
        last_day_of_month = gDayOfMonth;
        len = 0; buf[0] = '\0';
        str_append(buf, &len, sizeof(buf), day_abbrev(gDayOfWeek));
        str_append(buf, &len, sizeof(buf), " ");
        str_append_u32(buf, &len, sizeof(buf), gDayOfMonth, 0);
        recompui_set_text(date_label, buf);
    }

    if (gSeason != last_season) {
        last_season = gSeason;
        recompui_set_text(season_label, season_name(gSeason));
    }

    if (gWeather != last_weather) {
        last_weather = gWeather;
        RecompuiColor c = weather_color(gWeather);
        recompui_set_background_color(weather_dot, &c);
    }

    // Like Stardew's 10-minute ticks, the clock only moves in CLOCK_STEP_MINUTES increments.
    int shown_minutes = gMinutes - gMinutes % CLOCK_STEP_MINUTES;
    if (gHour != last_hour || shown_minutes != last_minutes) {
        u32 hour12 = gHour % 12 == 0 ? 12 : gHour % 12;
        last_hour = gHour;
        last_minutes = shown_minutes;
        len = 0; buf[0] = '\0';
        str_append_u32(buf, &len, sizeof(buf), hour12, 0);
        str_append(buf, &len, sizeof(buf), shown_minutes < 10 ? ":0" : ":");
        str_append_u32(buf, &len, sizeof(buf), shown_minutes, 0);
        str_append(buf, &len, sizeof(buf), gHour < 12 ? " am" : " pm");
        recompui_set_text(time_label, buf);
    }

    if ((s64)gGold != last_gold) {
        last_gold = gGold;
        len = 0; buf[0] = '\0';
        str_append_u32(buf, &len, sizeof(buf), gGold, 1);
        str_append(buf, &len, sizeof(buf), " G");
        recompui_set_text(gold_label, buf);
    }
}

static void update_bars(float s) {
    int max_stamina = gMaximumStamina;
    int stamina = gPlayer.currentStamina;

    if (max_stamina != last_max_stamina) {
        last_max_stamina = max_stamina;
        // Starting max stamina is 100, so the bar grows from BAR_HEIGHT_MIN toward BAR_HEIGHT_MAX.
        float growth = clamp01((float)(max_stamina - 100) / (float)(MAX_STAMINA - 100));
        recompui_set_height(energy_trough, (BAR_HEIGHT_MIN + (BAR_HEIGHT_MAX - BAR_HEIGHT_MIN) * growth) * s, UNIT_DP);
        last_stamina = -1;
    }

    if (stamina != last_stamina) {
        last_stamina = stamina;
        float fraction = clamp01((float)stamina / (float)max_stamina);
        RecompuiColor c = energy_color(fraction);
        recompui_set_height(energy_fill, fraction * 100.0f, UNIT_PERCENT);
        recompui_set_background_color(energy_fill, &c);
    }

    int health = MAX_FATIGUE_POINTS - gPlayer.fatigueCounter;
#ifdef DEBUG_HEALTH_CYCLE
    // Test-only: step the displayed health through each color band.
    static int debug_frame = 0;
    static const int debug_values[3] = { 80, 40, 15 };
    debug_frame = (debug_frame + 1) % 900;
    health = debug_values[debug_frame / 300];
#endif
    if (health != last_health) {
        last_health = health;
        recompui_set_height(health_fill, clamp01((float)health / (float)MAX_FATIGUE_POINTS) * 100.0f, UNIT_PERCENT);
        recompui_set_background_color(health_fill, health <= HEALTH_WARN_2 ? &col_energy_low
                                                 : health <= HEALTH_WARN_1 ? &col_energy_mid
                                                 : &col_energy_high);
    }

    // Pulse the health fill (triangle wave on its opacity) once a sick day is close.
    if (health <= HEALTH_PULSE_BELOW) {
        pulse_frame = (pulse_frame + 1) % HEALTH_PULSE_FRAMES;
        float phase = (float)pulse_frame / (float)HEALTH_PULSE_FRAMES;
        float wave = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        recompui_set_opacity(health_fill, 0.35f + 0.65f * wave);
    } else if (pulse_frame != 0) {
        pulse_frame = 0;
        recompui_set_opacity(health_fill, 1.0f);
    }
}

RECOMP_HOOK("gfxRetraceCallback")
void StardewHud_OnFrame(int pendingGfx) {
    if (!hud_initialized) {
        hud_init();
    }

    int show = config_hud_enabled() && hud_should_show();
    float target = show ? 1.0f : 0.0f;

    // Nothing to do while fully hidden; skip all UI work.
    if (!show && hud_opacity == 0.0f) {
        return;
    }

    recompui_open_context(hud_context);

    float scale = config_scale();
    if (scale != hud_scale) {
        hud_scale = scale;
        apply_layout(scale);
    }

    int want_health = config_show_health();
    if (want_health != health_shown) {
        health_shown = want_health;
        recompui_set_display(health_column, want_health ? DISPLAY_FLEX : DISPLAY_NONE);
    }

    if (show) {
        update_clock_panel();
        update_bars(hud_scale);
    }

    if (hud_opacity < target) {
        hud_opacity = clamp01(hud_opacity + FADE_STEP);
    } else if (hud_opacity > target) {
        hud_opacity = clamp01(hud_opacity - FADE_STEP);
    }
    recompui_set_opacity(hud_root, hud_opacity);

    recompui_close_context(hud_context);
}
