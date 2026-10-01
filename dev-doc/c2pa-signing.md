# C2PA Content Credentials - Design Document

Status: proposal. Target: `master`.
Scope: `src/imageio/imageio.c`, new `src/common/c2pa.{h,c}`.

**Priority: P3 / opportunistic.** Nothing in this document is a bug. The case
for it is positioning, and it is a real case: as the industry converges on
generative editing, "was this image edited, by what, and is the history
intact" becomes a question a photo tool should be able to answer. darktable is
the only major editor where the answer can be given locally, offline, and
without a subscription.

Deliberately **not** urgent. It is in the plan because it is cheap, greenfield,
and because it is the kind of thing a solo fork can own completely - no
upstream dependency, no API churn, no competitive pressure from the vendors
(signing edit history competes with the narrative that edited means suspect).

---

## 1. What already exists

- **C2PA: zero.** No mention in the issue tracker, nothing in the build, no
  dependency.
- **XMP export path is single and well-defined.** `src/imageio/imageio.c`
  line 1522-1528, at the end of `write_image()`:

  ```c
  /* now write xmp into that container, if possible */
  if(copy_metadata
     && (format->flags(format_params) & FORMAT_FLAGS_SUPPORT_XMP))
  {
    dt_exif_xmp_attach_export(imgid, filename, metadata, &dev, &pipe);
    // no need to cancel the export if this fail
  }
  ```

  Declared in `src/common/exif.h:111`:
  `gboolean dt_exif_xmp_attach_export(const dt_imgid_t imgid, const char *filename, void *metadata, dt_develop_t *dev, struct dt_dev_pixelpipe_t *pipe);`

This is the insertion point. Everything else in this document follows from the
fact that it already exists, already runs on every metadata-carrying export, and
already tolerates failure.

## 2. Why not just embed it in XMP

Because that is what everyone else does, and it is the weaker answer.

A C2PA **manifest** is a signed JSON-LD structure with a certificate chain. It
belongs in its own file (`.c2pa`, or embedded as a C2PA box where the container
allows). XMP can *reference* a manifest, but a manifest is not XMP.

## 3. Proposed design

### 3.1 The component

New `src/common/c2pa.h` / `c2pa.c`, wrapping the C2PA C library. The library is
large and carries a Rust core; the integration must not leak it into darktable's
build in a way that makes a missing dependency a hard error.

That means the **same optional-dependency pattern** the project already uses
for OpenCV (`src/CMakeLists.txt:687+`, `find_package(... QUIET)`, degrade to
no-op, `HAVE_*` guard). darktable is installable on an old machine; a photo
editor that refuses to start without a signing library is a regression for
everyone who does not need it.

```cmake
if(USE_C2PA)
  find_package(c2pa QUIET)
  if(c2pa_FOUND)
    set(HAVE_C2PA 1)
  endif()
endif()
```

With the library absent, every function below is a no-op and the export behaves
exactly as it does today. No UI change, no warning spam - one line in the log
at `DT_DEBUG_CAMCTL`-equivalent verbosity.

### 3.2 The API

```c
/** Sign an exported file. No-op when built without c2pa.
 *  \return TRUE if a manifest was written, FALSE otherwise. Never fatal. */
gboolean dt_c2pa_sign(const dt_imgid_t imgid, const char *filename,
                      void *metadata, dt_develop_t *dev);

/** Certificate configuration, set by the user in preferences. */
typedef struct dt_c2pa_identity_t
{
  char *certificate_path;   /* PEM, user-supplied */
  char *certificate_key_path;
  char *contact_info;       /* optional, embedded in the manifest */
  char *signer_name;
} dt_c2pa_identity_t;
```

`dt_c2pa_sign()` is **never fatal to the export**. A signing failure must not
cost the user their photo. The existing comment at imageio.c:1527 - *"no need to
cancel the export if this fail"* - is the precedent and the rule.

### 3.3 What goes in the manifest

The manifest is a claim about the image. Only include what darktable can
actually attest:

| Assertion | Source |
|---|---|
| Editing software | "darktable", version from `dt_version_get_string()` |
| Asset type | the format flags, already computed at the call site |
| Edits performed | the module instances in `dev->iop_pipe`, which is the real history |
| Date/time | export time, ISO 8601 |
| Optional creator | the identity fields above, only if the user opted in |

**Do not** hash the developer's own image and present it as proof of anything.
A manifest that says "darktable made these edits" is useful. One that claims
the image is authentic says something the software cannot know.

### 3.4 Non-goals, written down so they stay written down

- **Not a DRM or a gate.** Signing is opt-in, and an unsigned export is a
  perfectly normal export.
- **Not a receipt chain.** No wallet, no account, no key server, no revocation
  list. A local key file is the whole model.
- **Not "AI detection".** darktable's neural restore exists; that does not make
  the file generated. Claiming otherwise would be the single fastest way to
  make this feature untrustworthy.
- **Not embedded by default.** A `.c2pa` sidecar next to the export, opt-in.
  Container embedding (JPEG C2PA box, PNG `caBX` chunk) is phase 2 and only for
  containers darktable already writes.

## 4. Sequencing

| Step | Work | Notes |
|---|---|---|
| 1 | Optional dependency + `QUIET`/`HAVE_C2PA` pattern, no-op stubs | Compiles and behaves identically with no library. |
| 2 | Preference storage for the identity | Pure plumbing. |
| 3 | `dt_c2pa_sign()` from the module history in `dev->iop_pipe` | The actual work. |
| 4 | Hook at imageio.c:1526, honouring `copy_metadata` | One call site. |
| 5 | CLI flag for headless pipelines | `src/cli/main.c` already has a coherent option parser to extend. |
| 6 | Container embedding, if it is ever wanted | Optional. |

Steps 1-2 are days. Step 3-4 is the substance. Step 5 matters more than it
looks: a provenance feature that only works in a GUI is much less useful to the
archive and lab workflows that would care most.

## 5. Known approximations and open questions

- **The C2PA library is a large native dependency.** It pulls a Rust toolchain
  into at least one supported platform's build. That is the single biggest
  reason this is P3 and not P1, and it should be argued about before the first
  line is written, not after.
- **Manifest size.** A manifest is kilobytes. On a 16-megapixel JPEG that
  matters; on a 100-megapixel one it does not. Worth measuring both.
- **Key handling.** A PEM file on disk is the minimum. A keyring integration
  (`Libsecret` is already an optional dependency at `src/CMakeLists.txt:590`)
  would be nicer and is out of scope here.
- **What a verifier does with it.** If nobody can check these manifests, the
  feature is decoration. Worth deciding who the audience is before shipping.

## 6. Tests

- Unit: manifest JSON structure for a synthetic history, with a fixed
  timestamp. Assert the module list survives the round trip.
- Unit: the no-op path. Built without c2pa, `dt_c2pa_sign()` returns FALSE and
  touches nothing.
- Unit: signing failure must not propagate. Feed a bad key, assert the export
  is untouched and the return is FALSE.
- The last one is the important one, and it is the one that would regress.

Every commit must build on its own, per `AGENTS.md`:

```bash
git rebase --exec 'cmake --build build' <base>
```

## 7. Upstream notes

Uncontroversial in principle, and plausibly mergeable as steps 1-2 (dependency
plumbing plus preferences) even if the rest is contested. The `QUIET`/
no-op pattern is the part most likely to be accepted as-is, because it is the
pattern the project already uses twice.
