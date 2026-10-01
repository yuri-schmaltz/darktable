# Modern Display Pipeline - Scope Reassessment

Status: **reassessment, not a design.** This document exists because the
proposal it replaces was wrong.

The fork roadmap argued that a solo fork should own a modern HDR / wide-gamut
display pipeline, on the grounds that Adobe and Capture One have no incentive
to break their installed base to ship one. Reading the code says that argument
is **mostly obsolete**: darktable already has the colour infrastructure, and the
remaining gap is much smaller than claimed. Writing a design document here
would have been writing a plan to build what already exists.

This is the honest version.

---

## 1. What I claimed, and why it was wrong

The earlier roadmap said, in a table, that "display pipeline: Rec.2020, PQ/HLG,
WideGamutRGB, soft-proofing HDR real" was a *high viability, uncontested* green
field for a solo fork, with the commercial-incentive argument as the moat.

Checked against `src/common/colorspaces.h` and `colorspaces.c`:

```c
DT_COLORSPACE_LIN_REC2020 = 4,
...
DT_COLORSPACE_PQ_REC2020 = 22,
DT_COLORSPACE_HLG_REC2020 = 23,
DT_COLORSPACE_PQ_P3 = 24,
DT_COLORSPACE_HLG_P3 = 25,
```

And the profiles are not export-only. From `_create_profile()` in
`src/common/colorspaces.c:1534`:

```c
res->profiles = g_list_append
  (res->profiles,
   _create_profile(DT_COLORSPACE_PQ_REC2020,
                   _colorspaces_create_pq_rec2020_rgb_profile(),
                   _("PQ Rec2020 RGB"), ++in_pos, ++out_pos,
                   ++display_pos, ++category_pos,
                   ++work_pos, ++display2_pos));
```

`++display_pos` and `++display2_pos`. PQ and HLG Rec2020 and P3 are **already
selectable as display profiles**, and as work profiles.

Also already present:

- A CICP colour space definition with Rec.2020 primaries and PQ / HLG transfer
  characteristics (`colorspaces.h:137-153`) - i.e. HDR10-style signalling is
  modelled, not guessed.
- `DT_CICP_TRANSFER_CHARACTERISTICS_REC2020_10B` / `_12B` - the two Rec.2020
  bit depths, distinguished.
- Soft-proofing: `res->softproof_type` from `ui_last/color/softproof_type`
  (`colorspaces.c:1605`), and a "softproof profile" entry at line 1473.
- `src/common/gamut_mapping.h` - gamut mapping helpers, with white-point
  chroma clipping against both black and white (`_clip_chroma_white`,
  `_clip_chroma_black`, `Ych_max_chroma`).
- `src/iop/agx.c` - AgX tone mapping, which is the modern display-referred
  transform, present in the module list alongside `filmicrgb.c`.
- `src/common/darktable_ucs_22_helpers.h` - for the tonemapping work.

The "green field for a solo fork" was a green field I had not walked into.

## 2. What is actually missing

Stated as questions, not conclusions, because the honest answer is that this
needs measurement before it earns a design document.

### 2.1 Screen color management - the likely real gap

Every one of the above is about *selecting and converting between colour
profiles*. None of it is about what the windowing system does with the pixels.
Questions:

- Does darktable query the display's actual characteristics (EDID / ICC),
  or does it assume sRGB and ask the compositor to deal with it?
- On **Wayland**, where the compositor owns the colour pipeline, is there a
  path that lets a client declare a wide-gamut or HDR surface?
- Is there a code path at all for a PQ/HLG display profile to mean
  "the screen can show this", or does it only ever mean "convert to it for
  export"?

The GTK4 migration in progress (issues #22402, #22461) is directly relevant: any
answer here depends on the GTK version, and the project is mid-transition.

**This is where I would spend a measurement hour before spending a week.**

### 2.2 Tone mapping to a real HDR display

`agx.c` exists as a darkroom module. What is missing, if anything, is a
*display transform*: taking scene-referred data and mapping it to the actual
peak brightness and gamut of the attached display, rather than to a nominal
Rec.709/sRGB profile at 100 nits. That is a different problem from having a
PQ profile in a dropdown.

### 2.3 Soft-proofing against an HDR target that is not the screen

Soft-proofing exists. Whether it can target a PQ destination and show a
reliable ΔE against it, with a tone-mapped display render, is unknown from the
code alone.

## 3. Recommendation

**Do not start this. Measure first.**

The one-hour measurement:

1. `darktable-cli` on a PQ Rec2020 profile export, inspect the output
   (`src/cli/main.c` already exposes the profile choice via `get_icc_type()`).
2. On an HDR-capable display, check whether darktable offers the display
   profile, and what it does with it.
3. Read what the compositor reports for the darktable window's colour space.

Only if those three show a real, user-visible gap does this deserve a design
document. If they show the pipeline is already complete, then the
differentiation thesis in the fork roadmap should move to whatever the
measurement identifies - and if the answer is "the pipeline is fine", then
honestly, this is not the fork's edge, and the roadmap should say so.

## 4. The general lesson, recorded

The roadmap's "commercial vendors will not build it" heuristic is only valid
when the *whole* stack is missing. It fails when the foundation is done and
only the last mile is absent - which is a much smaller project with a much
smaller moat.

Before committing a solo fork to any "the big guys can't/won't do this" bet,
the check is: **read the code first, argue second.** That check is cheap. I
skipped it here and it would have cost a wasted sprint.
