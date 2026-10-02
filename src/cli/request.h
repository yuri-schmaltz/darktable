/* request.h - versioned request representation for darktable-cli v2
 *
 * Copyright (C) 2026 darktable developers.
 * darktable is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Step 2 of dev-doc/automation-api-spec.md.
 *
 * The point of this layer is the determinism contract (spec 2.2): a request
 * must record the exact module stack with parameter values, never a style
 * name. A style can change underneath an archive; a recorded stack cannot.
 * dt_cli_request_validate() enforces that, and
 * dt_cli_request_fingerprint() gives the archive a value to check later.
 *
 * Deliberately free of JSON: parsing and serialisation are thin adapters
 * (json-glib is already a dependency via src/mcp/), and keeping the contract
 * in plain C means it can be unit tested without a document.
 */

#pragma once

#include <glib.h>

/** Request schema version. Bump the major for anything that changes the
 *  meaning of an existing field; a reader must reject an unknown major
 *  outright rather than guess. */
#define DT_CLI_REQUEST_VERSION_MAJOR 1
#define DT_CLI_REQUEST_VERSION_MINOR 0

/** Value kinds a module parameter can take in a request. */
typedef enum dt_cli_value_type_t
{
  DT_CLI_VALUE_DOUBLE = 0,
  DT_CLI_VALUE_INT = 1,
  DT_CLI_VALUE_STRING = 2,
  DT_CLI_VALUE_BOOL = 3
} dt_cli_value_type_t;

/** One module parameter. Named, never positional: darktable's introspection
 *  exposes fields by name precisely so that a record survives a module
 *  gaining a field between versions. */
typedef struct dt_cli_value_t
{
  char *name;
  dt_cli_value_type_t type;
  union
  {
    double d;
    gint64 i;
    gboolean b;
    char *s;
  } v;
} dt_cli_value_t;

/** One module in the stack. Order in the list is stack order. */
typedef struct dt_cli_setting_t
{
  char *module;
  /* sorted by name on insert, so the fingerprint does not depend on the order
   * the author happened to type the parameters in */
  GPtrArray *values; /* dt_cli_value_t* */
} dt_cli_setting_t;

/** A complete request. */
typedef struct dt_cli_request_t
{
  guint version_major;
  guint version_minor;
  GList *inputs;          /* char* */
  char *output_dir;
  char *output_format;    /* e.g. "tiff" */
  gboolean sign_provenance;
  /** When a style was used to seed the stack, this is the *expanded* stack
   *  that was recorded. Validation refuses a request that names a style
   *  without one - see spec 2.2. */
  GPtrArray *stack;       /* dt_cli_setting_t* */
  gboolean has_template;
  char *template_name;
} dt_cli_request_t;

dt_cli_request_t *dt_cli_request_new(guint major, guint minor);
void dt_cli_request_free(dt_cli_request_t *req);

/* Setters. All copy their arguments. */
void dt_cli_request_add_input(dt_cli_request_t *req, const char *path);
void dt_cli_request_add_module(dt_cli_request_t *req, const char *module);
void dt_cli_request_set_output_dir(dt_cli_request_t *req, const char *dir);
void dt_cli_request_set_output_format(dt_cli_request_t *req, const char *fmt);
void dt_cli_request_set_template(dt_cli_request_t *req, const char *style);
void dt_cli_request_set_provenance(dt_cli_request_t *req, gboolean sign);

/** Record a parameter on the most recently added module. */
void dt_cli_module_add_double(dt_cli_setting_t *m, const char *name, double v);
void dt_cli_module_add_int(dt_cli_setting_t *m, const char *name, gint64 v);
void dt_cli_module_add_string(dt_cli_setting_t *m, const char *name, const char *v);
void dt_cli_module_add_bool(dt_cli_setting_t *m, const char *name, gboolean v);

/** Look a parameter up. Returns NULL when absent or when the type differs. */
const dt_cli_value_t *dt_cli_module_get(const dt_cli_setting_t *m, const char *name);

/** Validate the request.
 *  \return NULL when valid, else a static, translatable reason. */
const char *dt_cli_request_validate(const dt_cli_request_t *req);

/** Canonical text form: inputs sorted, stack in order, parameters sorted by
 *  name, doubles formatted with %.17g so a round trip is exact.
 *
 *  This is the value an archive can be checked against in ten years, and it is
 *  what dt_cli_request_fingerprint() hashes. */
char *dt_cli_request_canonical(const dt_cli_request_t *req);

/** Fingerprint of the canonical form, lowercase hex, 64 chars.
 *  Caller frees. */
char *dt_cli_request_fingerprint(const dt_cli_request_t *req);
