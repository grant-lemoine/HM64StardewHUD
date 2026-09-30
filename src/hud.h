#ifndef HUD_H
#define HUD_H

#include "recompui.h"

// Shared by the HUD pieces (hud.c owns these).
extern RecompuiContext hud_context;
extern RecompuiResource hud_root;

extern RecompuiColor col_panel_bg;
extern RecompuiColor col_panel_border;
extern RecompuiColor col_text;

// Every DP size is multiplied by this and the "HUD Scale" setting; 1.0 on the setting means this size.
#define HUD_BASE_SCALE 1.25f

#define EDGE_MARGIN   16.0f
#define PANEL_BORDER   4.0f
#define PANEL_RADIUS   8.0f

// Current fade of the whole HUD (0..1). RmlUi's opacity is inherited, not multiplied: a child that
// sets its own opacity ignores hud_root's fade, so such children must multiply this in themselves.
extern float hud_opacity;

void style_panel(RecompuiResource panel);
RecompuiResource make_label(RecompuiResource parent, const char* text);

// toolbar.c. All calls happen with hud_context open.
void toolbar_init(void);
void toolbar_layout(float scale);
void toolbar_update(void);
void toolbar_set_visible(int visible);
void toolbar_apply_opacity(float hud_fade);

#endif
