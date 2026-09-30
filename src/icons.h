#ifndef ICONS_H
#define ICONS_H

#include "recompui.h"

typedef enum {
    ICON_SHEET_TOOLS,
    ICON_SHEET_ITEMS,
    ICON_SHEET_COUNT
} IconSheet;

// Animation index in the tools / holdable-items sheet for a tool or item id, or -1 for none.
// Tools account for their upgrade level (and the bottle's contents).
int icon_anim_for_tool(u8 tool);
int icon_anim_for_item(u8 item);

// Decodes (once, then cached) the icon for an animation. Returns 0 if there's nothing to show.
int icon_lookup(IconSheet sheet, int anim, RecompuiTextureHandle* texture, int* width, int* height);

#endif
