#ifndef NUKLEAR_H
#define NUKLEAR_H

#include "nuklear.h"

void nkAdjustDragBounds(struct nk_context* nk,
    struct nk_rect* nodeBounds, struct nk_rect* dragBounds);

#endif

#if defined(NUKLEAR_IMPLEMENTATION) && !defined(NUKLEAR_UNIT)
#define NUKLEAR_UNIT

#define NK_IMPLEMENTATION
#define NK_GLFW_ES2_IMPLEMENTATION
#include "nuklear.h"

void nkAdjustDragBounds(struct nk_context* nk,
    struct nk_rect* nodeBounds, struct nk_rect* dragBounds)
{
  struct nk_panel* panel = nk_window_get_panel(nk);
  struct nk_style* style = &nk->style;
  struct nk_user_font const* font = style->font;
  // calculate dragging click bounds (window title)
  // HACK: there doesn't seem any api to get a panel's full bounds including title etc
  // NOTE: this assumes the nodes always have a title bar
  // TODO: find a way to get rid of all this jank
  struct nk_vec2 panelPadding = nk_panel_get_padding(style, panel->type);
  int headerHeight = font->height +
    2 * style->window.header.padding.y +
    2 * style->window.header.label_padding.y;
  struct nk_vec2 scrollbarSize = style->window.scrollbar_size;
  nodeBounds->y -= headerHeight;
  nodeBounds->x -= panelPadding.x;
  nodeBounds->h += headerHeight + panelPadding.y;
  nodeBounds->w += scrollbarSize.x + panelPadding.x * 2 + style->window.header.padding.x;

  *dragBounds = *nodeBounds;
  dragBounds->h = headerHeight + panelPadding.y;
}

#endif
