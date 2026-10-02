/* results.c - exit code contract and machine-readable results for darktable-cli
 *
 * Copyright (C) 2026 darktable developers.
 * darktable is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "results.h"

#include <glib/gi18n.h> /* for _() */
#include <string.h>

struct dt_cli_results_t
{
  GPtrArray *records; /* dt_cli_result_t* */
  gdouble total_seconds;
};

/* JSON string escaping.
 *
 * Paths on Linux can contain anything except NUL and '/', and on Windows they
 * contain backslashes. A raw quote or a raw backslash in an unescaped string
 * produces output that no parser accepts, so this is not defensive coding - it
 * is the minimum required for the format to be valid.
 *
 * A NULL pointer renders as JSON null, not as "". An absent error and an error
 * whose message happens to be empty are different facts, and a caller that
 * branches on "did this fail" must be able to tell them apart. */
static void _append_json_string(GString *out, const char *s)
{
  if(!s)
  {
    g_string_append(out, "null");
    return;
  }
  g_string_append_c(out, '"');
  for(const unsigned char *p = (const unsigned char *)s; *p; p++)
  {
    switch(*p)
    {
      case '"':
        g_string_append(out, "\\\"");
        break;
      case '\\':
        g_string_append(out, "\\\\");
        break;
      case '\n':
        g_string_append(out, "\\n");
        break;
      case '\r':
        g_string_append(out, "\\r");
        break;
      case '\t':
        g_string_append(out, "\\t");
        break;
      case '\b':
        g_string_append(out, "\\b");
        break;
      case '\f':
        g_string_append(out, "\\f");
        break;
      default:
        if(*p < 0x20)
          g_string_append_printf(out, "\\u%04x", *p);
        else
          g_string_append_c(out, (char)*p);
        break;
    }
  }
  g_string_append_c(out, '"');
}

static void _result_free(gpointer data)
{
  dt_cli_result_t *r = (dt_cli_result_t *)data;
  if(!r)
    return;
  g_free(r->input);
  g_free(r->output);
  g_free(r->error);
  g_free(r);
}

const char *dt_cli_exit_name(dt_cli_exit_t code)
{
  switch(code)
  {
    case DT_CLI_EXIT_OK:
      return "ok";
    case DT_CLI_EXIT_JOB_FAILED:
      return "job_failed";
    case DT_CLI_EXIT_USAGE:
      return "usage";
    case DT_CLI_EXIT_LIBRARY:
      return "library";
    case DT_CLI_EXIT_CANCELLED:
      return "cancelled";
    case DT_CLI_EXIT_INTERNAL:
      return "internal";
  }
  return "unknown";
}

const char *dt_cli_exit_description(dt_cli_exit_t code)
{
  switch(code)
  {
    case DT_CLI_EXIT_OK:
      return _("all inputs processed successfully");
    case DT_CLI_EXIT_JOB_FAILED:
      return _("at least one input failed to process");
    case DT_CLI_EXIT_USAGE:
      return _("the command line could not be understood");
    case DT_CLI_EXIT_LIBRARY:
      return _("the darktable library or configuration is unusable");
    case DT_CLI_EXIT_CANCELLED:
      return _("the run was cancelled");
    case DT_CLI_EXIT_INTERNAL:
      return _("internal error in darktable-cli");
  }
  return _("unknown error");
}

dt_cli_results_t *dt_cli_results_new(void)
{
  dt_cli_results_t *r = g_new0(dt_cli_results_t, 1);
  r->records = g_ptr_array_new_with_free_func(_result_free);
  return r;
}

void dt_cli_results_free(dt_cli_results_t *results)
{
  if(!results)
    return;
  g_ptr_array_free(results->records, TRUE);
  g_free(results);
}

void dt_cli_results_add(dt_cli_results_t *results, const char *input,
                        const char *output, dt_cli_exit_t status, const char *error,
                        gdouble seconds)
{
  if(!results)
    return;
  dt_cli_result_t *r = g_new0(dt_cli_result_t, 1);
  r->input = g_strdup(input);
  r->output = g_strdup(output);
  r->status = status;
  r->error = g_strdup(error);
  r->seconds = seconds;
  results->total_seconds += seconds;
  g_ptr_array_add(results->records, r);
}

guint dt_cli_results_failed(const dt_cli_results_t *results)
{
  if(!results)
    return 0;
  guint n = 0;
  for(guint i = 0; i < results->records->len; i++)
  {
    const dt_cli_result_t *r = g_ptr_array_index(results->records, i);
    if(r->status != DT_CLI_EXIT_OK)
      n++;
  }
  return n;
}

dt_cli_exit_t dt_cli_results_exit_code(const dt_cli_results_t *results)
{
  if(!results || dt_cli_results_failed(results) == 0)
    return DT_CLI_EXIT_OK;

  /* If every failure is a per-input one, the run itself was fine: that is
   * JOB_FAILED, not USAGE or LIBRARY. Escalate only when something
   * run-wide went wrong. */
  dt_cli_exit_t worst = DT_CLI_EXIT_JOB_FAILED;
  for(guint i = 0; i < results->records->len; i++)
  {
    const dt_cli_result_t *r = g_ptr_array_index(results->records, i);
    if(r->status == DT_CLI_EXIT_OK)
      continue;
    if(r->status == DT_CLI_EXIT_JOB_FAILED)
      continue; /* the least severe, keep looking */
    worst = r->status;
    break;
  }
  return worst;
}

char *dt_cli_results_to_json(const dt_cli_results_t *results)
{
  GString *out = g_string_new(NULL);

  g_string_append(out, "{");
  g_string_append(out, "\n  \"schema\": \"darktable-cli-results/1\",");
  g_string_append_printf(out, "\n  \"schema_version\": %d,", 1);
  g_string_append_printf(out, "\n  \"inputs\": %u,", results ? results->records->len : 0);
  g_string_append_printf(out, "\n  \"failed\": %u,",
                         results ? dt_cli_results_failed(results) : 0);
  g_string_append_printf(out, "\n  \"exit_code\": %d,",
                         results ? (int)dt_cli_results_exit_code(results) : 0);
  g_string_append(out, "\n  \"exit_name\": ");
  _append_json_string(out, dt_cli_exit_name(results ? dt_cli_results_exit_code(results)
                                                    : DT_CLI_EXIT_OK));
  g_string_append_printf(out, ",\n  \"seconds\": %.3f,",
                         results ? results->total_seconds : 0.0);
  g_string_append(out, "\n  \"results\": [");

  if(results)
    for(guint i = 0; i < results->records->len; i++)
    {
      const dt_cli_result_t *r = g_ptr_array_index(results->records, i);
      g_string_append(out, i ? ",\n    {" : "\n    {");
      g_string_append(out, "\n      \"input\": ");
      _append_json_string(out, r->input);
      g_string_append(out, ",\n      \"output\": ");
      _append_json_string(out, r->output);
      g_string_append_printf(out, ",\n      \"status\": \"%s\"", dt_cli_exit_name(r->status));
      g_string_append(out, ",\n      \"error\": ");
      _append_json_string(out, r->error);
      g_string_append_printf(out, ",\n      \"seconds\": %.3f", r->seconds);
      g_string_append(out, "\n    }");
    }

  g_string_append(out, results->records->len ? "\n  ]\n}\n" : "]\n}\n");
  return g_string_free(out, FALSE);
}

void dt_cli_results_print_json(const dt_cli_results_t *results, FILE *stream)
{
  char *json = dt_cli_results_to_json(results);
  fputs(json, stream ? stream : stdout);
  g_free(json);
}
