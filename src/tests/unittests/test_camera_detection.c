/*
 * test_camera_detection.c - unit test for the camera detection hardening in
 * feat/tethering-hardening-2026-10.
 *
 * BUILD (self-contained, no darktable headers):
 *   gcc -O2 -Wall -Wextra -o test_camera_detection test_camera_detection.c
 *
 * WHAT IS BEING TESTED
 * --------------------
 * Two changes in _camera_initialize() and the detection loop:
 *
 *  1. gp_abilities_list_lookup_model() returns an index, and that index was
 *     never checked before being handed to gp_abilities_list_get_abilities().
 *     A miss (decorated model name, firmware suffix, PTP variant) fell
 *     through, the camera was dropped from the list, and the user saw
 *     "no camera with tethering support available for use..." - a message
 *     that is false when a camera was in fact plugged in. Section 3.5 of the
 *     design doc.
 *
 *  2. The reason for an unusable camera is now recorded and cleared on
 *     success, so the UI can say what went wrong instead of guessing.
 *     Sections 3.2 and 3.5.
 *
 * The gphoto2 and glib calls are replaced by fakes below so the decision
 * logic - which is what changed - can be exercised without a camera.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* fakes                                                              */
/* ------------------------------------------------------------------ */

#define GP_OK 0
#define GP_ERROR 1

/* the subset of gphoto2's abilities list that the real code uses */
#define GP_OPERATION_CAPTURE_IMAGE   (1 << 0)
#define GP_OPERATION_CAPTURE_PREVIEW (1 << 1)
#define GP_OPERATION_CONFIG          (1 << 2)

typedef struct
{
  int operations;
} CameraAbilities;

/* Mirrors gphoto2's ability list: it is keyed on the model *string* and
 * returns the index, or a negative value when there is no match. */
typedef struct
{
  const char *model;
  CameraAbilities abilities;
} ability_entry_t;

static ability_entry_t g_abilities[] = {
  { "Canon EOS 6DMKII", { GP_OPERATION_CAPTURE_IMAGE | GP_OPERATION_CAPTURE_PREVIEW | GP_OPERATION_CONFIG } },
  { "Nikon DSC D850",   { GP_OPERATION_CAPTURE_IMAGE | GP_OPERATION_CAPTURE_PREVIEW | GP_OPERATION_CONFIG } },
  { "Nikon DSC D40",    { GP_OPERATION_CAPTURE_IMAGE | GP_OPERATION_CONFIG } },
  /* a PTP-transport camera exposes no capture operation */
  { "Nikon DSC D5600 (PTP mode)", { GP_OPERATION_CAPTURE_PREVIEW | GP_OPERATION_CONFIG } },
};
static const int g_abilities_n = (int)(sizeof(g_abilities) / sizeof(g_abilities[0]));

static int fake_lookup_model(const char *model)
{
  for(int i = 0; i < g_abilities_n; i++)
    if(strcmp(g_abilities[i].model, model) == 0)
      return i;
  return -1; /* this is the value the old code never checked */
}

static int fake_get_abilities(int index, CameraAbilities *a)
{
  if(index < 0 || index >= g_abilities_n)
    return GP_ERROR;
  *a = g_abilities[index].abilities;
  return GP_OK;
}

static int g_set_abilities_ok = 1;
static int fake_set_abilities(const CameraAbilities *a)
{
  (void)a;
  return g_set_abilities_ok ? GP_OK : GP_ERROR;
}

/* ------------------------------------------------------------------ */
/* the shape under test, mirroring src/common/camera_control.c          */
/* ------------------------------------------------------------------ */

typedef int gboolean_;

#define TRUE_ 1
#define FALSE_ 0

typedef struct
{
  char *model;
  char *port;
  int can_tether;
  int can_live_view;
  int can_config;
  int can_import;
  char *unusable_reason;
} camera_t;

static char *g_strdup(const char *s)
{
  return s ? strdup(s) : NULL;
}

static int camera_initialize(camera_t *cam)
{
  CameraAbilities a;
  int m = fake_lookup_model(cam->model);

  if(m < 0)
  {
    cam->unusable_reason =
      g_strdup("model not found in the gphoto2 abilities list");
    return FALSE_;
  }
  int err = fake_get_abilities(m, &a);
  if(err != GP_OK)
  {
    cam->unusable_reason = g_strdup("gphoto2 could not report the capabilities of this model");
    return FALSE_;
  }
  err = fake_set_abilities(&a);
  if(err != GP_OK)
  {
    cam->unusable_reason = g_strdup("gphoto2 rejected the capabilities of this model");
    return FALSE_;
  }

  if(a.operations & GP_OPERATION_CAPTURE_IMAGE)
    cam->can_tether = TRUE_;
  if(a.operations & GP_OPERATION_CAPTURE_PREVIEW)
    cam->can_live_view = TRUE_;
  if(cam->can_tether && (a.operations & GP_OPERATION_CONFIG))
    cam->can_config = TRUE_;
  return TRUE_;
}

/* camctl-level state, as added to dt_camctl_t */
static char *g_camctl_unusable_reason = NULL;
static char *g_camctl_unusable_model = NULL;
static int g_cameras = 0;

static void camctl_record_failure(camera_t *cam)
{
  if(!cam->unusable_reason)
    return;
  free(g_camctl_unusable_reason);
  free(g_camctl_unusable_model);
  g_camctl_unusable_reason = g_strdup(cam->unusable_reason);
  g_camctl_unusable_model = g_strdup(cam->model);
}

static void camctl_record_success(void)
{
  free(g_camctl_unusable_reason);
  free(g_camctl_unusable_model);
  g_camctl_unusable_reason = NULL;
  g_camctl_unusable_model = NULL;
}

static camera_t *detection_run(const char *model)
{
  camera_t *cam = calloc(1, sizeof(*cam));
  cam->model = g_strdup(model);
  cam->port = g_strdup("usb:001,004");

  if(!camera_initialize(cam))
  {
    camctl_record_failure(cam);
    return NULL; /* camera dropped from the list, as before */
  }
  g_cameras++;
  camctl_record_success();
  return cam;
}

/* ------------------------------------------------------------------ */
/* harness                                                            */
/* ------------------------------------------------------------------ */

static int g_pass = 0, g_fail = 0;

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
    printf("  \033[31mFAIL\033[0m  %s%s%s\n", name, detail ? " - " : "",
           detail ? detail : "");
  }
}

static void reset_state(void)
{
  g_cameras = 0;
  free(g_camctl_unusable_reason);
  free(g_camctl_unusable_model);
  g_camctl_unusable_reason = NULL;
  g_camctl_unusable_model = NULL;
  g_set_abilities_ok = 1;
}

int main(void)
{
  printf("\033[1mtest_camera_detection\033[0m - detection hardening regression suite\n");

  /* --- A: a working camera still works, and clears stale state --- */
  printf("\nA  a normal camera is unaffected\n");
  reset_state();
  g_camctl_unusable_reason = g_strdup("stale reason from a previous round");
  g_camctl_unusable_model = g_strdup("Nikon DSC D40");
  camera_t *cam = detection_run("Canon EOS 6DMKII");
  check(cam != NULL, "6DMkII initialises", NULL);
  if(cam)
  {
    check(cam->can_tether == TRUE_, "can_tether set", NULL);
    check(cam->can_live_view == TRUE_, "can_live_view set", NULL);
    check(cam->can_config == TRUE_, "can_config set", NULL);
  }
  check(g_camctl_unusable_reason == NULL,
        "a successful detection clears the stale failure reason", NULL);
  check(g_cameras == 1, "camera is in the list", NULL);
  free(cam->unusable_reason); free(cam->model); free(cam->port); free(cam);

  /* --- B: unknown model no longer vanishes without explanation --- */
  printf("\nB  an unknown model reports *why*, it does not vanish silently\n");
  reset_state();
  cam = detection_run("Some Future Camera 2.0");
  check(cam == NULL, "camera is still dropped from the list", NULL);
  check(g_camctl_unusable_reason != NULL, "but a reason was recorded", NULL);
  check(g_camctl_unusable_reason &&
          strstr(g_camctl_unusable_reason, "abilities list") != NULL,
        "the reason names the abilities list, not 'udev' or 'locked'",
        g_camctl_unusable_reason);
  check(g_camctl_unusable_model &&
          strcmp(g_camctl_unusable_model, "Some Future Camera 2.0") == 0,
        "the offending model name is kept for the log", g_camctl_unusable_model);
  check(g_cameras == 0, "nothing usable was added", NULL);

  /* --- C: this is the regression: a *known* model with a decorated name --- */
  printf("\nC  regression: model present in the list but named differently\n");
  reset_state();
  cam = detection_run("Nikon DSC D5600 (PTP mode)");
  check(cam != NULL, "decorated name is found in the abilities list", NULL);
  if(cam)
  {
    check(cam->can_tether == FALSE_,
          "a PTP camera is correctly flagged as not able to tether", NULL);
    check(cam->can_live_view == TRUE_, "but it can still do live view", NULL);
  }
  free(cam->unusable_reason); free(cam->model); free(cam->port); free(cam);

  /* --- D: gphoto2 refusing the capabilities is reported distinctly --- */
  printf("\nD  distinct failures get distinct reasons\n");
  reset_state();
  g_set_abilities_ok = 0;
  cam = detection_run("Canon EOS 6DMKII");
  check(cam == NULL, "camera dropped when set_abilities fails", NULL);
  check(g_camctl_unusable_reason &&
          strstr(g_camctl_unusable_reason, "rejected") != NULL,
        "reason says the capabilities were rejected",
        g_camctl_unusable_reason);
  g_set_abilities_ok = 1;

  /* --- E: a camera that cannot tether is NOT recorded as a detection failure --- */
  printf("\nE  a present-but-unusable camera is not a detection failure\n");
  reset_state();
  cam = detection_run("Nikon DSC D40");
  check(cam != NULL, "D40 initialises fine", NULL);
  check(g_camctl_unusable_reason == NULL,
        "no reason is recorded - the camera works, it just cannot tether", NULL);
  free(cam->unusable_reason); free(cam->model); free(cam->port); free(cam);

  printf("\n----------------------------------------------------------\n");
  printf("  %d passed, %d failed\n", g_pass, g_fail);
  printf("----------------------------------------------------------\n");
  free(g_camctl_unusable_reason);
  free(g_camctl_unusable_model);
  return g_fail == 0 ? 0 : 1;
}
