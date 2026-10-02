/* request.c - versioned request representation for darktable-cli v2
 *
 * Copyright (C) 2026 darktable developers.
 * darktable is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "request.h"

#include <glib/gi18n.h> /* for _() */
#include <string.h>

static void _value_free(gpointer p)
{
  dt_cli_value_t *v = (dt_cli_value_t *)p;
  if(!v)
    return;
  if(v->type == DT_CLI_VALUE_STRING)
    g_free(v->v.s);
  g_free(v->name);
  g_free(v);
}

static void _module_free(gpointer p)
{
  dt_cli_setting_t *m = (dt_cli_setting_t *)p;
  if(!m)
    return;
  g_ptr_array_free(m->values, TRUE);
  g_free(m->module);
  g_free(m);
}

dt_cli_request_t *dt_cli_request_new(guint major, guint minor)
{
  dt_cli_request_t *r = g_new0(dt_cli_request_t, 1);
  r->version_major = major;
  r->version_minor = minor;
  r->stack = g_ptr_array_new_with_free_func(_module_free);
  return r;
}

void dt_cli_request_free(dt_cli_request_t *req)
{
  if(!req)
    return;
  g_list_free_full(req->inputs, g_free);
  g_ptr_array_free(req->stack, TRUE);
  g_free(req->output_dir);
  g_free(req->output_format);
  g_free(req->template_name);
  g_free(req);
}

void dt_cli_request_add_input(dt_cli_request_t *req, const char *path)
{
  req->inputs = g_list_append(req->inputs, g_strdup(path));
}

void dt_cli_request_add_module(dt_cli_request_t *req, const char *module)
{
  dt_cli_setting_t *m = g_new0(dt_cli_setting_t, 1);
  m->module = g_strdup(module);
  m->values = g_ptr_array_new_with_free_func(_value_free);
  g_ptr_array_add(req->stack, m);
}

void dt_cli_request_set_output_dir(dt_cli_request_t *req, const char *dir)
{
  g_free(req->output_dir);
  req->output_dir = g_strdup(dir);
}

void dt_cli_request_set_output_format(dt_cli_request_t *req, const char *fmt)
{
  g_free(req->output_format);
  req->output_format = g_strdup(fmt);
}

void dt_cli_request_set_template(dt_cli_request_t *req, const char *style)
{
  g_free(req->template_name);
  req->template_name = g_strdup(style);
  req->has_template = (style != NULL);
}

void dt_cli_request_set_provenance(dt_cli_request_t *req, gboolean sign)
{
  req->sign_provenance = sign;
}

static int _cmp_value(gconstpointer a, gconstpointer b)
{
  const dt_cli_value_t *va = *(const dt_cli_value_t *const *)a;
  const dt_cli_value_t *vb = *(const dt_cli_value_t *const *)b;
  return strcmp(va->name, vb->name);
}

/* Appends the value and returns it.
 *
 * The return value is not a convenience: the array is sorted on insert, so
 * "the last element" is only the one just added by accident. An earlier
 * version of this wrote through values[len-1] after the sort and corrupted
 * whichever entry the sort moved into that slot. */
static dt_cli_value_t *_add_value(dt_cli_setting_t *m, const char *name,
                                  dt_cli_value_type_t type)
{
  /* Re-setting an existing parameter replaces it: a request that mentions
   * exposure.exposure twice is a document error, and last-wins is the least
   * surprising of the alternatives. */
  for(guint i = 0; i < m->values->len; i++)
  {
    dt_cli_value_t *old = g_ptr_array_index(m->values, i);
    if(strcmp(old->name, name) == 0)
    {
      /* g_ptr_array_remove_index() runs the array's free func on the removed
       * element, which is _value_free() and which already frees the name and
       * any string payload. Freeing them here as well is a double free - the
       * crash this replaces was found by running the test, not by reading. */
      g_ptr_array_remove_index(m->values, i);
      break;
    }
  }

  dt_cli_value_t *v = g_new0(dt_cli_value_t, 1);
  v->name = g_strdup(name);
  v->type = type;
  g_ptr_array_add(m->values, v);
  g_ptr_array_sort(m->values, _cmp_value);
  return v;
}

void dt_cli_module_add_double(dt_cli_setting_t *m, const char *name, double v)
{
  _add_value(m, name, DT_CLI_VALUE_DOUBLE)->v.d = v;
}

void dt_cli_module_add_int(dt_cli_setting_t *m, const char *name, gint64 v)
{
  _add_value(m, name, DT_CLI_VALUE_INT)->v.i = v;
}

void dt_cli_module_add_string(dt_cli_setting_t *m, const char *name, const char *v)
{
  _add_value(m, name, DT_CLI_VALUE_STRING)->v.s = g_strdup(v);
}

void dt_cli_module_add_bool(dt_cli_setting_t *m, const char *name, gboolean v)
{
  _add_value(m, name, DT_CLI_VALUE_BOOL)->v.b = v ? TRUE : FALSE;
}

const dt_cli_value_t *dt_cli_module_get(const dt_cli_setting_t *m, const char *name)
{
  for(guint i = 0; i < m->values->len; i++)
  {
    const dt_cli_value_t *v = g_ptr_array_index(m->values, i);
    if(strcmp(v->name, name) == 0)
      return v;
  }
  return NULL;
}

const char *dt_cli_request_validate(const dt_cli_request_t *req)
{
  if(!req)
    return _("no request");
  if(req->version_major != DT_CLI_REQUEST_VERSION_MAJOR)
    return _("unsupported request version");
  if(!req->inputs)
    return _("no inputs given");
  if(!req->output_dir)
    return _("no output directory given");
  if(!req->output_format)
    return _("no output format given");
  /* The determinism contract, checked before the generic "no stack" rule so
   * that the specific reason is the one a caller sees. A request that names a
   * style but carries no recorded stack is a promise that depends on a name
   * resolving to the same thing forever, which is exactly what a style does
   * not guarantee. Refuse it rather than silently produce a different archive
   * later. */
  if(req->has_template && req->stack->len == 0)
    return _("template given without a recorded module stack");
  if(req->stack->len == 0)
    return _("no module stack recorded");

  for(guint i = 0; i < req->stack->len; i++)
  {
    const dt_cli_setting_t *m = g_ptr_array_index(req->stack, i);
    if(!m->module || !*m->module)
      return _("module in the stack has no name");
    if(m->values->len == 0)
      return _("module in the stack has no recorded parameters");
    for(guint k = 0; k < m->values->len; k++)
    {
      const dt_cli_value_t *v = g_ptr_array_index(m->values, k);
      if(!v->name || !*v->name)
        return _("parameter without a name");
    }
  }
  return NULL;
}

static int _cmp_str(gconstpointer a, gconstpointer b)
{
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

char *dt_cli_request_canonical(const dt_cli_request_t *req)
{
  GString *out = g_string_new("darktable-cli-request/1\n");

  g_string_append_printf(out, "version\t%u.%u\n", req->version_major, req->version_minor);
  g_string_append_printf(out, "format\t%s\n", req->output_format ? req->output_format : "");
  g_string_append_printf(out, "output\t%s\n", req->output_dir ? req->output_dir : "");
  g_string_append_printf(out, "sign\t%d\n", req->sign_provenance ? 1 : 0);

  /* inputs sorted: the same set of files is the same request whatever order
   * the author listed them in */
  GPtrArray *ins = g_ptr_array_new();
  for(GList *l = req->inputs; l; l = l->next)
    g_ptr_array_add(ins, l->data);
  g_ptr_array_sort(ins, _cmp_str);
  for(guint i = 0; i < ins->len; i++)
    g_string_append_printf(out, "input\t%s\n", (const char *)g_ptr_array_index(ins, i));
  g_ptr_array_free(ins, TRUE);

  /* stack in order - order is meaning, not noise */
  for(guint i = 0; i < req->stack->len; i++)
  {
    const dt_cli_setting_t *m = g_ptr_array_index(req->stack, i);
    /* values are kept sorted by name on insert */
    for(guint k = 0; k < m->values->len; k++)
    {
      const dt_cli_value_t *v = g_ptr_array_index(m->values, k);
      switch(v->type)
      {
        case DT_CLI_VALUE_DOUBLE:
          /* %.17g round-trips every IEEE double exactly; a shorter form would
           * make the fingerprint depend on formatting, not on the value */
          g_string_append_printf(out, "set\t%s\t%s\t%.17g\n", m->module, v->name, v->v.d);
          break;
        case DT_CLI_VALUE_INT:
          g_string_append_printf(out, "set\t%s\t%s\t%" G_GINT64_FORMAT "\n", m->module, v->name,
                                 v->v.i);
          break;
        case DT_CLI_VALUE_BOOL:
          g_string_append_printf(out, "set\t%s\t%s\t%s\n", m->module, v->name,
                                 v->v.b ? "true" : "false");
          break;
        case DT_CLI_VALUE_STRING:
          g_string_append_printf(out, "set\t%s\t%s\t%s\n", m->module, v->name,
                                 v->v.s ? v->v.s : "");
          break;
      }
    }
  }
  return g_string_free(out, FALSE);
}

char *dt_cli_request_fingerprint(const dt_cli_request_t *req)
{
  char *canonical = dt_cli_request_canonical(req);
  /* G_CHECKSUM_SHA256 is what keeps this checkable in ten years, and it is
   * glib, which is already linked. Note that g_compute_checksum_for_string()
   * returns an ALREADY hex-encoded string - only the *_for_bytes() variant
   * returns raw bytes - so hex-encoding it again yields a perfectly
   * plausible 64-character string that is the hex of a hex. An assertion of
   * "64 hex chars" does not catch that. */
  char *fp = g_compute_checksum_for_string(G_CHECKSUM_SHA256, canonical, -1);
  g_free(canonical);
  return fp;
}
