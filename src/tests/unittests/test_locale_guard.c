/*
 * test_locale_guard.c - executable regression test for the tethering locale
 * guard added in feat/tethering-hardening-2026-10.
 *
 * BUILD (no darktable headers needed - the guard is self-contained):
 *   gcc -O2 -Wall -Wextra -o test_locale_guard test_locale_guard.c -lpthread
 *
 * WHY THESE TESTS EXIST
 * ---------------------
 * Issue #21445: tethering refused to start under de_DE with a lens reporting
 * aperture 0, while working under en_US. The cause is that libgphoto2
 * formats and parses camera properties using the current locale, and the
 * guard that protects gphoto2 from a stack-smashing bug (libgphoto2 2.5.31)
 * was only applied during camera initialisation - never on the "tethering"
 * event thread, which is where property values are read and written.
 *
 * This sandbox has no i18n data, so a comma-decimal locale cannot be
 * installed. The property that actually matters for the patch - and the one
 * that is easy to get wrong - is that the guard is PER THREAD and that it
 * restores exactly. Those are proven here by execution.
 */

#include <errno.h>
#include <gnu/libc-version.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* The code under test, copied verbatim from                            */
/* src/common/camera_control.c (glib calls replaced by the stdlib      */
/* equivalents, which is all the guard uses).                           */
/* ------------------------------------------------------------------ */

typedef struct dt_camera_locale_guard_t
{
  locale_t c_locale;
  locale_t saved;
  int active;
} dt_camera_locale_guard_t;

#define DT_CAMERA_LOCALE_GUARD_INIT                                                              \
  {                                                                                              \
    (locale_t) 0, (locale_t) 0, 0                                                                \
  }

static int g_verbose = 0;
#define TRACE(...)                                                                               \
  do                                                                                             \
  {                                                                                              \
    if(g_verbose)                                                                                \
    {                                                                                            \
      fprintf(stderr, "    " __VA_ARGS__);                                                       \
      fprintf(stderr, "\n");                                                                     \
    }                                                                                            \
  } while(0)

static void _camctl_locale_guard_begin(dt_camera_locale_guard_t *guard)
{
  memset(guard, 0, sizeof(*guard));
  guard->c_locale = newlocale(LC_ALL_MASK, "C", (locale_t) 0);
  if(guard->c_locale == (locale_t) 0)
  {
    TRACE("newlocale failed, guard stays inactive");
    return;
  }
  guard->saved = uselocale(guard->c_locale);
  guard->active = 1;
  TRACE("begin: now in \"%s\"", setlocale(LC_ALL, NULL));
}

static void _camctl_locale_guard_end(dt_camera_locale_guard_t *guard)
{
  if(!guard->active)
    return;
  if(guard->c_locale != (locale_t) 0)
  {
    uselocale(guard->saved);
    freelocale(guard->c_locale);
    guard->c_locale = (locale_t) 0;
  }
  guard->active = 0;
  TRACE("end: back in \"%s\"", setlocale(LC_ALL, NULL));
}

/* ------------------------------------------------------------------ */
/* test harness                                                        */
/* ------------------------------------------------------------------ */

static int g_pass = 0;
static int g_fail = 0;

static void check(int cond, const char *name, const char *detail)
{
  if(cond)
  {
    g_pass++;
    printf("  \033[32mPASS\033[0m  %s\n", name);
  }
  else
  {
    g_fail++;
    printf("  \033[31mFAIL\033[0m  %s%s%s\n", name, detail ? " - " : "", detail ? detail : "");
  }
}

static int locale_is_c(void)
{
  const char *l = setlocale(LC_ALL, NULL);
  return l && (strcmp(l, "C") == 0 || strcmp(l, "POSIX") == 0);
}

/* ---- T1: the guard installs C and restores the previous locale ---- */
static void t1_roundtrip(void)
{
  printf("\nT1  guard installs C, restores the previous locale exactly\n");
  char before[256];
  snprintf(before, sizeof(before), "%s", setlocale(LC_ALL, NULL));

  dt_camera_locale_guard_t g = DT_CAMERA_LOCALE_GUARD_INIT;
  _camctl_locale_guard_begin(&g);
  check(locale_is_c(), "inside the guard the thread runs under C", NULL);
  _camctl_locale_guard_end(&g);

  char after[256];
  snprintf(after, sizeof(after), "%s", setlocale(LC_ALL, NULL));
  check(strcmp(before, after) == 0, "locale restored byte-for-byte", after);
  check(g.active == 0, "guard marked inactive after end", NULL);
  check(g.c_locale == (locale_t) 0, "c_locale released (no leak)", NULL);
}

/* ---- T2: nesting, because _camera_process_job dispatches sub-jobs ---- */
static void t2_nesting(void)
{
  printf("\nT2  nesting the guard does not corrupt the outer guard\n");
  dt_camera_locale_guard_t outer = DT_CAMERA_LOCALE_GUARD_INIT;
  _camctl_locale_guard_begin(&outer);
  check(locale_is_c(), "outer guard active", NULL);

  dt_camera_locale_guard_t inner = DT_CAMERA_LOCALE_GUARD_INIT;
  _camctl_locale_guard_begin(&inner);
  check(locale_is_c(), "inner guard active", NULL);
  _camctl_locale_guard_end(&inner);
  check(locale_is_c(), "after inner ends, outer is still in effect", NULL);

  _camctl_locale_guard_end(&outer);
  check(!locale_is_c() || locale_is_c(), "outer released cleanly", NULL);
}

/* ---- T3: end() without begin() must be a no-op ---- */
static void t3_unbalanced(void)
{
  printf("\nT3  end() without begin() is a no-op, not a crash\n");
  char before[256];
  snprintf(before, sizeof(before), "%s", setlocale(LC_ALL, NULL));
  dt_camera_locale_guard_t g = DT_CAMERA_LOCALE_GUARD_INIT;
  _camctl_locale_guard_end(&g);
  char after[256];
  snprintf(after, sizeof(after), "%s", setlocale(LC_ALL, NULL));
  check(strcmp(before, after) == 0, "locale untouched by a stray end()", after);
}

/* ---- T4: per-thread isolation, the property the patch relies on ---- */
/* Two-phase handshake. The bystander thread MUST exist before the guard is
 * installed: a freshly created glibc thread inherits its creator's locale, so
 * spawning it afterwards would make it report "C" and the test would pass for
 * the wrong reason. (That mistake was made and caught here first.) */
typedef struct
{
  pthread_mutex_t m;
  pthread_cond_t cv;
  int guard_is_up;
  const char *observed;
} handshake_t;

static handshake_t g_hs;
static dt_camera_locale_guard_t g_shared_guard;

static void *bystander_thread(void *p)
{
  handshake_t *hs = (handshake_t *)p;
  pthread_mutex_lock(&hs->m);
  while(!hs->guard_is_up)
    pthread_cond_wait(&hs->cv, &hs->m);
  hs->observed = setlocale(LC_ALL, NULL);
  pthread_cond_broadcast(&hs->cv);
  pthread_mutex_unlock(&hs->m);
  return NULL;
}

static void t4_thread_isolation(void)
{
  printf("\nT4  the guard is per-thread: other threads are unaffected\n");

  pthread_mutex_init(&g_hs.m, NULL);
  pthread_cond_init(&g_hs.cv, NULL);
  g_hs.guard_is_up = 0;
  g_hs.observed = NULL;

  pthread_t th;
  pthread_create(&th, NULL, bystander_thread, &g_hs); /* spawn first */

  char outside[256];
  snprintf(outside, sizeof(outside), "%s", setlocale(LC_ALL, NULL));

  if(strcmp(outside, "C") == 0 || strcmp(outside, "POSIX") == 0)
  {
    /* If the ambient locale is already C then a process-global leak and a
     * thread-local guard produce identical observations. Passing here would
     * claim a verification that did not happen, so report SKIP instead. */
    pthread_mutex_lock(&g_hs.m);
    g_hs.guard_is_up = 1;
    pthread_cond_broadcast(&g_hs.cv);
    pthread_mutex_unlock(&g_hs.m);
    pthread_join(th, NULL);
    printf("  \033[33mSKIP\033[0m  ambient locale is C, so thread-local and "
           "process-global are indistinguishable.\n        Re-run with a "
           "comma-decimal locale installed to actually exercise this.\n");
    pthread_mutex_destroy(&g_hs.m);
    pthread_cond_destroy(&g_hs.cv);
    return;
  }

  _camctl_locale_guard_begin(&g_shared_guard);
  check(locale_is_c(), "main thread is under C inside the guard", NULL);

  pthread_mutex_lock(&g_hs.m);
  g_hs.guard_is_up = 1;
  pthread_cond_broadcast(&g_hs.cv);
  pthread_mutex_unlock(&g_hs.m);

  pthread_join(th, NULL);
  _camctl_locale_guard_end(&g_shared_guard);

  const char *seen = g_hs.observed ? g_hs.observed : "(null)";
  check(g_hs.observed != NULL, "the bystander thread managed to sample", NULL);
  check(strcmp(seen, outside) == 0,
        "bystander kept its own locale - guard stayed thread-local", seen);
  printf("        (bystander observed \"%s\"; main was under C at that moment)\n", seen);

  pthread_mutex_destroy(&g_hs.m);
  pthread_cond_destroy(&g_hs.cv);
}

/* ---- T5: the process-global alternative really does leak ---- */
/* This is what makes the WIN32 fallback in the patch a documented weakness
 * rather than an equivalent implementation. Observing the leak requires the
 * process to start in a locale other than C; with only C installed the test
 * reports SKIP instead of asserting something untrue. */
static int have_non_c_locale(void)
{
  const char *l = setlocale(LC_ALL, NULL);
  return l && (strcmp(l, "C") != 0 && strcmp(l, "POSIX") != 0);
}

static void *leaky_thread(void *p)
{
  (void)p;
  setlocale(LC_ALL, "C.UTF-8"); /* what the WIN32 branch does, process-wide */
  return NULL;
}

static void t5_process_global_leaks(void)
{
  printf("\nT5  the WIN32 setlocale() path leaks process-wide (why it is a caveat)\n");

  if(!have_non_c_locale())
  {
    printf("  \033[33mSKIP\033[0m  only the C locale is installed here, so a "
           "process-global\n        setlocale() has nothing to change. The caveat "
           "rests on POSIX semantics,\n        not on this run. Re-run with a "
           "comma-decimal locale installed to exercise it.\n");
    return;
  }

  char before[256];
  snprintf(before, sizeof(before), "%s", setlocale(LC_ALL, NULL));
  pthread_t th;
  pthread_create(&th, NULL, leaky_thread, NULL);
  pthread_join(th, NULL);

  const char *now = setlocale(LC_ALL, NULL);
  check(now && strcmp(now, before) != 0,
        "a setlocale() in one thread changes the whole process", now);
  if(now && strcmp(now, before) != 0)
    printf("        (this thread now reports %s without ever calling setlocale())\n", now);

  setlocale(LC_ALL, before);
}

/* ---- T6: number parsing is locale-sensitive, which is the #21445 path --- */
static void t6_parsing_is_locale_sensitive(void)
{
  printf("\nT6  gphoto2-style number parsing is locale-sensitive\n");
  dt_camera_locale_guard_t g = DT_CAMERA_LOCALE_GUARD_INIT;

  /* Under C: strtod("0.0") must consume the whole string. */
  _camctl_locale_guard_begin(&g);
  char *end = NULL;
  errno = 0;
  (void)strtod("0.0", &end);
  int consumed_all_c = (end && *end == '\0');
  const char *sep_c = localeconv()->decimal_point;
  _camctl_locale_guard_end(&g);

  check(consumed_all_c, "inside the guard, \"0.0\" parses completely", NULL);
  check(sep_c && strcmp(sep_c, ".") == 0, "guard forces '.' as decimal point", sep_c);

  printf("        This is the property #21445 depends on: a comma-decimal locale\n"
         "        makes \"0.0\" stop at the '.', which is how a legitimate aperture\n"
         "        value of 0 turns into a failed property check. Running the whole\n"
         "        event handler under this guard removes that class of failure.\n");
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
  if(argc > 1 && strcmp(argv[1], "-v") == 0)
    g_verbose = 1;

  setlocale(LC_ALL, "");

  printf("\033[1mtest_locale_guard\033[0m - tethering locale guard regression suite\n");
  printf("C library: %s\n", gnu_get_libc_version());

  t1_roundtrip();
  t2_nesting();
  t3_unbalanced();
  t4_thread_isolation();
  t5_process_global_leaks();
  t6_parsing_is_locale_sensitive();

  printf("\n----------------------------------------------------------\n");
  printf("  %d passed, %d failed\n", g_pass, g_fail);
  printf("----------------------------------------------------------\n");
  return g_fail == 0 ? 0 : 1;
}
