/*
 * test_cli_request.c - unit test for the versioned request layer and the
 * determinism contract (dev-doc/automation-api-spec.md 2.2-2.3).
 *
 * Self-contained: no JSON, no library, no camera. glib only.
 */

#include "request.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

static void ck(int c, const char *n, const char *d)
{
  if(c)
  {
    g_pass++;
    printf("  \033[32mPASS\033[0m  %s\n", n);
  }
  else
  {
    g_fail++;
    printf("  \033[31mFAIL\033[0m  %s%s%s\n", n, d ? " - " : "", d ? d : "");
  }
}

/* build a small, valid request */
static dt_cli_request_t *good_request(void)
{
  dt_cli_request_t *r = dt_cli_request_new(DT_CLI_REQUEST_VERSION_MAJOR, 0);
  dt_cli_request_add_input(r, "/film/b.cr3");
  dt_cli_request_add_input(r, "/film/a.cr3");
  dt_cli_request_set_output_dir(r, "/archive/out");
  dt_cli_request_set_output_format(r, "tiff");
  dt_cli_request_add_module(r, "exposure");
  dt_cli_module_add_double(g_ptr_array_index(r->stack, 0), "exposure", 0.35);
  dt_cli_request_add_module(r, "temperature");
  dt_cli_module_add_int(g_ptr_array_index(r->stack, 1), "temperature", 6500);
  return r;
}

int main(void)
{
  setlocale(LC_ALL, "");
  printf("\033[1mtest_cli_request\033[0m - request layer + determinism contract\n");

  /* ---- A: validation ---- */
  printf("\nA  validation\n");
  dt_cli_request_t *r = good_request();
  ck(dt_cli_request_validate(r) == NULL, "a well-formed request validates", NULL);
  dt_cli_request_t *r_templated = NULL;

  dt_cli_request_t *empty = dt_cli_request_new(1, 0);
  ck(dt_cli_request_validate(empty) != NULL, "empty request is rejected", NULL);
  dt_cli_request_free(empty);

  dt_cli_request_t *nov = dt_cli_request_new(99, 0);
  dt_cli_request_add_input(nov, "a");
  dt_cli_request_set_output_dir(nov, "o");
  dt_cli_request_set_output_format(nov, "tiff");
  dt_cli_request_add_module(nov, "exposure");
  dt_cli_module_add_double(g_ptr_array_index(nov->stack, 0), "exposure", 1.0);
  ck(dt_cli_request_validate(nov) != NULL, "unknown major version is rejected", NULL);
  ck(g_strcmp0(dt_cli_request_validate(nov), "unsupported request version") == 0,
     "and the reason is specific", dt_cli_request_validate(nov));
  dt_cli_request_free(nov);

  /* ---- B: the determinism contract ---- */
  printf("\nB  determinism contract\n");
  r_templated = good_request();
  dt_cli_request_set_template(r_templated, "my-style");
  ck(dt_cli_request_validate(r_templated) == NULL,
     "a template WITH a recorded stack validates (the stack is what counts)", NULL);

  dt_cli_request_t *nostack = dt_cli_request_new(1, 0);
  dt_cli_request_add_input(nostack, "a");
  dt_cli_request_set_output_dir(nostack, "o");
  dt_cli_request_set_output_format(nostack, "tiff");
  dt_cli_request_set_template(nostack, "my-style");
  ck(dt_cli_request_validate(nostack) != NULL,
     "a template with NO recorded stack is refused - this is the whole point", NULL);
  ck(g_strcmp0(dt_cli_request_validate(nostack),
                "template given without a recorded module stack") == 0,
     "and says exactly why", dt_cli_request_validate(nostack));
  dt_cli_request_free(nostack);

  /* ---- C: canonical form is order independent where it should be ---- */
  printf("\nC  canonical form\n");
  dt_cli_request_t *r1 = good_request();
  char *c1 = dt_cli_request_canonical(r1);

  dt_cli_request_t *r2 = dt_cli_request_new(1, 0);
  dt_cli_request_add_input(r2, "/film/a.cr3");
  dt_cli_request_add_input(r2, "/film/b.cr3");
  dt_cli_request_set_output_dir(r2, "/archive/out");
  dt_cli_request_set_output_format(r2, "tiff");
  dt_cli_request_add_module(r2, "exposure");
  dt_cli_module_add_double(g_ptr_array_index(r2->stack, 0), "exposure", 0.35);
  dt_cli_request_add_module(r2, "temperature");
  dt_cli_module_add_int(g_ptr_array_index(r2->stack, 1), "temperature", 6500);
  char *c2 = dt_cli_request_canonical(r2);
  ck(strcmp(c1, c2) == 0, "input order does not change the canonical form", NULL);
  g_free(c2);
  dt_cli_request_free(r2);

  /* parameter order within a module must not matter */
  dt_cli_request_t *r3 = dt_cli_request_new(1, 0);
  dt_cli_request_add_input(r3, "/film/b.cr3");
  dt_cli_request_add_input(r3, "/film/a.cr3");
  dt_cli_request_set_output_dir(r3, "/archive/out");
  dt_cli_request_set_output_format(r3, "tiff");
  dt_cli_request_add_module(r3, "exposure");
  dt_cli_module_add_double(g_ptr_array_index(r3->stack, 0), "exposure", 0.35);
  dt_cli_module_add_string(g_ptr_array_index(r3->stack, 0), "mode", "linear");
  dt_cli_module_add_bool(g_ptr_array_index(r3->stack, 0), "protect", TRUE);
  char *c3 = dt_cli_request_canonical(r3);

  dt_cli_request_t *r4 = dt_cli_request_new(1, 0);
  dt_cli_request_add_input(r4, "/film/a.cr3");
  dt_cli_request_add_input(r4, "/film/b.cr3");
  dt_cli_request_set_output_dir(r4, "/archive/out");
  dt_cli_request_set_output_format(r4, "tiff");
  dt_cli_request_add_module(r4, "exposure");
  dt_cli_module_add_bool(g_ptr_array_index(r4->stack, 0), "protect", TRUE);
  dt_cli_module_add_string(g_ptr_array_index(r4->stack, 0), "mode", "linear");
  dt_cli_module_add_double(g_ptr_array_index(r4->stack, 0), "exposure", 0.35);
  char *c4 = dt_cli_request_canonical(r4);
  ck(strcmp(c3, c4) == 0, "parameter order within a module does not matter either", NULL);
  g_free(c3);
  g_free(c4);
  dt_cli_request_free(r3);
  dt_cli_request_free(r4);

  /* STACK order is meaning, so it must change the canonical form */
  dt_cli_request_t *swapped = good_request();
  {
    /* Swap by direct assignment. g_ptr_array_remove_index() runs the array's
     * free func, and stack elements own their values - removing and re-adding
     * the pointer would be a use-after-free. */
    dt_cli_setting_t *a = g_ptr_array_index(swapped->stack, 0);
    dt_cli_setting_t *b = g_ptr_array_index(swapped->stack, 1);
    swapped->stack->pdata[0] = b;
    swapped->stack->pdata[1] = a;
  }
  char *cs = dt_cli_request_canonical(swapped);
  ck(strcmp(c1, cs) != 0, "stack order DOES change it - order is meaning", NULL);
  g_free(cs);
  dt_cli_request_free(swapped);

  /* ---- D: re-setting a parameter replaces it ---- */
  printf("\nD  parameter replacement\n");
  dt_cli_setting_t *m = g_ptr_array_index(r1->stack, 0);
  dt_cli_module_add_double(m, "exposure", 1.75);
  const dt_cli_value_t *v = dt_cli_module_get(m, "exposure");
  ck(v && v->type == DT_CLI_VALUE_DOUBLE, "still one value, still a double", NULL);
  ck(v && fabs(v->v.d - 1.75) < 1e-12, "and it holds the new value", NULL);
  ck(m->values->len == 1, "no duplicate entry was appended", NULL);
  ck(dt_cli_module_get(m, "nope") == NULL, "absent parameter returns NULL", NULL);
  dt_cli_request_free(r1);

  /* ---- E: the fingerprint ---- */
  printf("\nE  fingerprint\n");
  dt_cli_request_free(r);
  r = good_request();
  char *f1 = dt_cli_request_fingerprint(r);
  ck(strlen(f1) == 64, "64 hex chars", f1);
  ck(g_ascii_isxdigit(f1[0]) && g_ascii_isxdigit(f1[63]), "all hex", f1);
  ck(strstr(f1, "\n") == NULL, "no newlines leaked in", f1);

  char *f1b = dt_cli_request_fingerprint(r);
  ck(strcmp(f1, f1b) == 0, "stable across calls", NULL);
  g_free(f1b);

  /* a changed value must change the fingerprint */
  dt_cli_module_add_double(g_ptr_array_index(r->stack, 0), "exposure", 0.35000001);
  char *f2 = dt_cli_request_fingerprint(r);
  ck(strcmp(f1, f2) != 0, "a 1e-8 change is detected", NULL);
  g_free(f2);
  g_free(f1);

  /* so must a different output directory */
  dt_cli_request_free(r);
  r = good_request();
  dt_cli_request_set_output_dir(r, "/archive/other");
  char *f3 = dt_cli_request_fingerprint(r);
  dt_cli_request_free(r);
  r = good_request();
  char *f4 = dt_cli_request_fingerprint(r);
  ck(strcmp(f3, f4) != 0, "a different output dir changes it", NULL);
  g_free(f3);
  g_free(f4);

  /* ---- F: doubles round-trip exactly ---- */
  printf("\nF  double fidelity\n");
  dt_cli_request_free(r);
  r = good_request();
  dt_cli_module_add_double(g_ptr_array_index(r->stack, 0), "exposure", 0.1 + 0.2);
  char *c = dt_cli_request_canonical(r);
  ck(strstr(c, "0.30000000000000004") != NULL,
     "%.17g keeps the exact double, not 0.3", c);
  g_free(c);

  /* r is released once, at the end; c1 still belongs to section C. */
  g_free(c1);

  dt_cli_request_free(r);
  dt_cli_request_free(r_templated);

  printf("\n----------------------------------------------------------\n");
  printf("  %d passed, %d failed\n", g_pass, g_fail);
  printf("----------------------------------------------------------\n");
  return g_fail == 0 ? 0 : 1;
}
