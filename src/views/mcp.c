/*
    This file is part of darktable,
    Copyright (C) 2013-2025 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "common/darktable.h"
#include "control/control.h"
#include "gui/gtk.h"
#include "views/view.h"
#include "views/view_api.h"

#ifdef USE_LUA
#include "lua/call.h"
#include "lua/lua.h"
#include "lua/modules.h"
#include "lua/types.h"
#include "lua/widget/widget.h"
#endif

DT_MODULE(1)

typedef struct dt_mcp_view_t {
  GtkWidget *box; // The container for our MCP UI
} dt_mcp_view_t;

const char *name(const dt_view_t *self) {
  return _(
      "mcp"); // Lowercase to match module name usually, allows dt.gui.views.mcp
}

uint32_t view(const dt_view_t *self) { return DT_VIEW_MCP; }

#ifdef USE_LUA
static int set_widget_cb(lua_State *L) {
  dt_view_t *self = (dt_view_t *)lua_touserdata(L, lua_upvalueindex(1));
  dt_mcp_view_t *d = (dt_mcp_view_t *)self->data;

  // Expecting: view:set_widget(widget)
  // Stack: 1=view wrapper (userdata), 2=widget (userdata)

  if (lua_gettop(L) != 2)
    return luaL_error(L, "usage: view:set_widget(widget)");

  // We assume arg 2 is a widget. Ideally we could check type.
  // dt_lua_widget_t is the structure.

  dt_lua_widget_t *w_lua = (dt_lua_widget_t *)lua_touserdata(L, 2);

  // Basic sanity check
  if (!w_lua || !w_lua->widget)
    return luaL_error(L, "invalid widget passed");

  GtkWidget *new_widget = w_lua->widget;

  if (d->box && new_widget) {
    // Clear existing children
    GList *children = gtk_container_get_children(GTK_CONTAINER(d->box));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
      gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    // Pack new widget
    gtk_box_pack_start(GTK_BOX(d->box), new_widget, TRUE, TRUE, 0);
    gtk_widget_show_all(new_widget);

    // Bind it so Lua doesn't GC it while it's in the view.
    // Note: Typically cleanup would unbind, but for now we assume it persists
    // until replaced.
    dt_lua_widget_bind(L, w_lua);
  }

  return 0;
}
#endif

void init(dt_view_t *self) {
  dt_mcp_view_t *d = calloc(1, sizeof(dt_mcp_view_t));
  self->data = d;

  d->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(d->box, "mcp_center_box");
  // Expand to fill available space
  gtk_widget_set_halign(d->box, GTK_ALIGN_FILL);
  gtk_widget_set_valign(d->box, GTK_ALIGN_FILL);

#ifdef USE_LUA
  lua_State *L = darktable.lua_state.state;
  // Provide 'set_widget' method on the view object in Lua
  const int my_type =
      dt_lua_module_entry_get_type(L, "view", self->module_name);

  lua_pushlightuserdata(L, self);
  lua_pushcclosure(L, set_widget_cb, 1);
  dt_lua_gtk_wrap(L); // Function will run in GTK thread
  lua_pushcclosure(L, dt_lua_type_member_common, 1);
  dt_lua_type_register_const_type(L, my_type, "set_widget");
#endif
}

void cleanup(dt_view_t *self) {
  dt_mcp_view_t *d = self->data;
  // Clean up widget if needed? GTK destroys children usually.
  free(d);
}

gboolean try_enter(dt_view_t *self) { return FALSE; }

void enter(dt_view_t *self) {
  dt_mcp_view_t *d = self->data;

  // Hide panels
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_LEFT, FALSE, FALSE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_RIGHT, FALSE, FALSE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_TOP, TRUE, TRUE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_BOTTOM, FALSE, FALSE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_CENTER_TOP, FALSE, FALSE);
  dt_ui_panel_show(darktable.gui->ui, DT_UI_PANEL_CENTER_BOTTOM, FALSE, FALSE);

  GtkWidget *center = dt_ui_center(darktable.gui->ui);
  gtk_box_pack_start(GTK_BOX(center), d->box, TRUE, TRUE, 0);
  gtk_widget_show_all(d->box);
}

void leave(dt_view_t *self) {
  dt_mcp_view_t *d = self->data;
  GtkWidget *center = dt_ui_center(darktable.gui->ui);

  if (d->box && gtk_widget_get_parent(d->box) == center) {
    g_object_ref(d->box);
    gtk_container_remove(GTK_CONTAINER(center), d->box);
  }
}

void expose(dt_view_t *self, cairo_t *cr, int32_t width, int32_t height,
            int32_t pointerx, int32_t pointery) {}

void mouse_moved(dt_view_t *self, double x, double y, double pressure,
                 int which) {}
int button_released(dt_view_t *self, double x, double y, int which,
                    uint32_t state) {
  return 0;
}
int button_pressed(dt_view_t *self, double x, double y, double pressure,
                   int which, int type, uint32_t state) {
  return 0;
}
