/* results.h - exit code contract and machine-readable results for darktable-cli
 *
 * Copyright (C) 2026 darktable developers.
 * darktable is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Part of the darktable-cli v2 work, step 1. See
 * dev-doc/automation-api-spec.md section 2.4.
 */

#pragma once

#include <glib.h>
#include <stdio.h>

/* Exit codes.
 *
 * Before this, every failure path in main() called exit(1). A caller could not
 * tell "your script was wrong" from "a photo failed to develop" from "the
 * library is broken", and could not make a branch decision on it. The three
 * outcomes below are the ones a caller actually branches on. */
typedef enum dt_cli_exit_t
{
  /* every input was processed successfully */
  DT_CLI_EXIT_OK = 0,
  /* the run was well-formed but at least one input failed */
  DT_CLI_EXIT_JOB_FAILED = 1,
  /* the command line or request could not be understood */
  DT_CLI_EXIT_USAGE = 2,
  /* the darktable library, configuration, or on-disk state is unusable */
  DT_CLI_EXIT_LIBRARY = 3,
  /* the work was cancelled by the caller */
  DT_CLI_EXIT_CANCELLED = 4,
  /* an internal error: a bug in darktable-cli */
  DT_CLI_EXIT_INTERNAL = 5
} dt_cli_exit_t;

/** Stable machine-readable name, for --results JSON and for scripts.
 *  Never change or remove a name; add a new one instead. */
const char *dt_cli_exit_name(dt_cli_exit_t code);

/** Human-readable one-liner, for stderr. */
const char *dt_cli_exit_description(dt_cli_exit_t code);

/** Per-input result record. */
typedef struct dt_cli_result_t
{
  char *input;  /* path as given, or NULL for a synthetic entry */
  char *output; /* path written, or NULL if nothing was written */
  dt_cli_exit_t status;
  char *error;  /* NULL unless status != DT_CLI_EXIT_OK */
  gdouble seconds;
} dt_cli_result_t;

/** Accumulates per-input results and renders them.
 *
 * Single-shot for now (step 1). The same interface is what a queue would fill
 * later, which is why it is an accumulator and not a printf. */
typedef struct dt_cli_results_t dt_cli_results_t;

dt_cli_results_t *dt_cli_results_new(void);
void dt_cli_results_free(dt_cli_results_t *results);

/** Record one input. Takes ownership of nothing; all strings are copied. */
void dt_cli_results_add(dt_cli_results_t *results, const char *input,
                        const char *output, dt_cli_exit_t status, const char *error,
                        gdouble seconds);

/** Number of records with a non-OK status. */
guint dt_cli_results_failed(const dt_cli_results_t *results);

/** The exit code this run should terminate with: OK if nothing failed, else
 *  the worst status seen, downgraded to JOB_FAILED if the failure was a
 *  per-input one. */
dt_cli_exit_t dt_cli_results_exit_code(const dt_cli_results_t *results);

/** Render as JSON. Caller frees with g_free().
 *
 *  The schema is part of the contract (automation-api-spec.md section 2.5):
 *  keys do not change, they gain fields. */
char *dt_cli_results_to_json(const dt_cli_results_t *results);

/** Convenience: emit the JSON to a stream. */
void dt_cli_results_print_json(const dt_cli_results_t *results, FILE *stream);
