/*
    This file is part of darktable,
    Copyright (C) 2011-2024 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/debug.h"
#include "common/image_cache.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/develop.h"
#include "dtgtk/button.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_viewswitcher_t {
  GList *labels;
} dt_lib_viewswitcher_t;

/* callback when a view label is pressed */
static gboolean _lib_viewswitcher_button_press_callback(GtkWidget *w,
                                                        GdkEventButton *ev,
                                                        const dt_view_t *view);
/* helper function to create a label */
static GtkWidget *_lib_viewswitcher_create_label(dt_view_t *view);
/* callback when view changed signal happens */
static void _lib_viewswitcher_view_changed_callback(gpointer instance,
                                                    dt_view_t *old_view,
                                                    dt_view_t *new_view,
                                                    dt_lib_module_t *self);
static void _lib_viewswitcher_view_cannot_change_callback(
    gpointer instance, dt_view_t *old_view, dt_view_t *new_view,
    dt_lib_module_t *self);
static void _switch_view(const dt_view_t *view);

const char *name(dt_lib_module_t *self) { return _("viewswitcher"); }

dt_view_type_flags_t views(dt_lib_module_t *self) { return DT_VIEW_ALL; }

uint32_t container(dt_lib_module_t *self) {
  return DT_UI_CONTAINER_PANEL_TOP_RIGHT;
}

int expandable(dt_lib_module_t *self) { return 0; }

int position(const dt_lib_module_t *self) { return 1001; }

#define SHORTCUT_TOOLTIP(v, w)                                                 \
  dt_action_define(&darktable.control->actions_global, "switch views",         \
                   v->module_name, w, NULL);

static void _dropdown_changed(GtkComboBox *widget, dt_lib_viewswitcher_t *d) {
  // Removed
}

void gui_init(dt_lib_module_t *self) {
  /* initialize ui widgets */
  dt_lib_viewswitcher_t *d = g_malloc0(sizeof(dt_lib_viewswitcher_t));
  self->data = (void *)d;

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  // d->dropdown = NULL; // Removed

  const gboolean gimping = dt_check_gimpmode("file");
  for (GList *view_iter = darktable.view_manager->views; view_iter;
       view_iter = g_list_next(view_iter)) {
    dt_view_t *view = view_iter->data;

    // skip hidden views
    if (view->flags() & VIEW_FLAGS_HIDDEN)
      continue;

    const gboolean lighttable = !g_strcmp0(view->module_name, "lighttable");
    const gboolean darkroom = !g_strcmp0(view->module_name, "darkroom");

    GtkWidget *w = _lib_viewswitcher_create_label(view);
    gtk_box_pack_start(GTK_BOX(self->widget), w, FALSE, FALSE, 0);
    d->labels = g_list_append(d->labels, gtk_bin_get_child(GTK_BIN(w)));

    if (lighttable)
      gtk_widget_set_sensitive(w, !gimping);
    else if (darkroom)
      gtk_widget_set_sensitive(
          w, TRUE); // Darkroom always active? Original code said !(lighttable
                    // && gimping) which is weird if lighttable is checked.
    // Original: gtk_widget_set_sensitive(w, !(lighttable && gimping));
    // If view is lighttable: disable if gimping.
    // If view is darkroom: ! (false && gimping) -> true.
    else
      gtk_widget_set_sensitive(w, !gimping); // Other views disabled if gimping

    SHORTCUT_TOOLTIP(view, w);

    /* create space if more views */
    if (view_iter->next != NULL) {
      GtkWidget *sep = gtk_label_new("|");
      gtk_widget_set_halign(sep, GTK_ALIGN_START);
      gtk_widget_set_name(sep, "view-label");
      gtk_box_pack_start(GTK_BOX(self->widget), sep, FALSE, FALSE, 0);
    }
  }

  /* connect callback to view change signal */
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_VIEWMANAGER_VIEW_CHANGED,
                           _lib_viewswitcher_view_changed_callback);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_VIEWMANAGER_VIEW_CANNOT_CHANGE,
                           _lib_viewswitcher_view_cannot_change_callback);
}

void gui_cleanup(dt_lib_module_t *self) {
  g_free(self->data);
  self->data = NULL;
}

static void _lib_viewswitcher_enter_leave_notify_callback(GtkWidget *w,
                                                          GdkEventCrossing *e,
                                                          GtkLabel *l) {
  /* if not active view lets highlight */
  if (e->type == GDK_ENTER_NOTIFY &&
      strcmp(g_object_get_data(G_OBJECT(w), "view-label"),
             dt_view_manager_name(darktable.view_manager)))
    gtk_widget_set_state_flags(GTK_WIDGET(l), GTK_STATE_FLAG_PRELIGHT, FALSE);
  else
    gtk_widget_unset_state_flags(GTK_WIDGET(l), GTK_STATE_FLAG_PRELIGHT);
}

static void _lib_viewswitcher_view_cannot_change_callback(
    gpointer instance, dt_view_t *old_view, dt_view_t *new_view,
    dt_lib_module_t *self) {
  // Nothing to do here for buttons as they don't hold state like the dropdown
  // did
}

static void _lib_viewswitcher_view_changed_callback(gpointer instance,
                                                    dt_view_t *old_view,
                                                    dt_view_t *new_view,
                                                    dt_lib_module_t *self) {
  dt_lib_viewswitcher_t *d = self->data;

  const char *name = dt_view_manager_name(darktable.view_manager);

  for (GList *iter = d->labels; iter; iter = g_list_next(iter)) {
    GtkWidget *label = GTK_WIDGET(iter->data);
    if (!g_strcmp0(g_object_get_data(G_OBJECT(label), "view-label"), name)) {
      gtk_widget_set_state_flags(label, GTK_STATE_FLAG_SELECTED, TRUE);
    } else
      gtk_widget_set_state_flags(label, GTK_STATE_FLAG_NORMAL, TRUE);
  }
}

static GtkWidget *_lib_viewswitcher_create_label(dt_view_t *view) {
  GtkWidget *eb = gtk_event_box_new();
  GtkWidget *b = gtk_label_new(view->name(view));
  gtk_container_add(GTK_CONTAINER(eb), b);
  /*setup label*/
  gtk_widget_set_halign(b, GTK_ALIGN_START);
  g_object_set_data(G_OBJECT(b), "view-label", (gchar *)view->name(view));
  g_object_set_data(G_OBJECT(eb), "view-label", (gchar *)view->name(view));
  gtk_widget_set_name(b, "view-label");
  gtk_widget_set_state_flags(b, GTK_STATE_FLAG_NORMAL, TRUE);

  /* connect button press handler */
  g_signal_connect(G_OBJECT(eb), "button-press-event",
                   G_CALLBACK(_lib_viewswitcher_button_press_callback), view);

  /* set enter/leave notify events and connect signals */
  gtk_widget_add_events(GTK_WIDGET(eb),
                        GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

  g_signal_connect(G_OBJECT(eb), "enter-notify-event",
                   G_CALLBACK(_lib_viewswitcher_enter_leave_notify_callback),
                   b);
  g_signal_connect(G_OBJECT(eb), "leave-notify-event",
                   G_CALLBACK(_lib_viewswitcher_enter_leave_notify_callback),
                   b);

  return eb;
}

static void _switch_view(const dt_view_t *view) {
  dt_ctl_switch_mode_to_by_view(view);
}

static gboolean _lib_viewswitcher_button_press_callback(GtkWidget *w,
                                                        GdkEventButton *ev,
                                                        const dt_view_t *view) {
  if (ev->button == GDK_BUTTON_PRIMARY) {
    _switch_view(view);
    return TRUE;
  }
  return FALSE;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
