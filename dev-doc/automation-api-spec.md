# darktable-cli v2 - Automation API Specification

Status: proposal. Target: `master`.
Scope: `src/cli/main.c` (926 lines), new `src/cli/jobqueue.{h,c}`.

This is the fork roadmap's only true moat, so it gets the most concrete
specification. Every competitor that sells a per-seat licence has no reason to
expose a real automation API. ON1, Capture One and Lightroom all do not.
darktable already has the ingredients - `darktable-cli`, Lua, and `darktable-mcp`
- and no product discipline to hold them back.

---

## 1. What exists today

- `src/cli/main.c`, 926 lines. A single-shot, option-driven batch converter.
  The option parser is coherent and worth keeping: `get_icc_type()`,
  `get_icc_intent()`, `_inputs_have_xmp_sidecar()`, and a clear
  `usage()`. It is not a mess.
- `src/mcp/` - a JSON-RPC server with `dt_bridge.c`, `mcp_tools.c`,
  `mcp_jsonrpc.c`, added in 5.8. This is the more interesting half: an agent
  can already drive the raw pipeline by parameter name, on a throwaway
  duplicate, with statistics returned. `src/mcp/README.md` documents it.
- `src/external/lua/lua-scripts/` - Lua inside the GUI process.

**The gap is not capability, it is shape.** `darktable-cli` exits. There is no
way to submit a job, watch it, retry it, or cancel it. A 50 000-image studio
workflow has to shell out 50 000 times.

## 2. What a v2 must add

### 2.1 A job queue that outlives the process

The minimum that changes what is possible:

```
darktable-cli --submit <request.json> --queue <name>
darktable-cli --status  --queue <name>
darktable-cli --watch   --queue <name>
darktable-cli --cancel  <job-id> --queue <name>
darktable-cli --results --queue <name> [--json]
```

State in a small SQLite database next to the queue file. Why SQLite and not a
daemon: darktable already depends on SQLite 3.26 (`src/CMakeLists.txt:462`), a
file-backed queue survives a crash, and a queue that needs a long-running daemon
is a queue that stops working when the daemon dies.

**Honest cost:** a queue is a concurrency problem, and this subsystem has a
recent history of concurrency bugs - the raw-denoise X-Trans cluster
(#22455-#22457) and the mask lock-order work (#22469). The job queue must be
one worker per queue, or a strict lock discipline from the first commit. It
should not be the place to discover a new threading model.

### 2.2 Determinism, stated as a contract

The single most valuable thing an automation API can promise:

> The same library state + the same request file produces the same output
> bytes, on any machine, forever.

That is what makes a 50 000-image archive trustworthy in ten years. It is also
what a generative or non-deterministic step would destroy, which is another
argument for keeping AI out of the critical path (RFC #22294).

A concrete consequence: the request file must record the exact module stack
with parameter values, not reference a style by name. A style can change under
you. A recorded stack cannot.

### 2.3 A stable, versioned request format

`--submit` takes JSON, and it is **versioned from the first release**:

```json
{
  "version": 1,
  "inputs": ["film/*.CR3"],
  "output": "/archive/exports",
  "template": "tpl:deliverable-v3",
  "settings": {
    "module": "exposure",
    "params": { "exposure": 0.35, "mode": "linear" }
  },
  "output_format": { "format": "tiff", "bits": 16, "compression": "lz4" },
  "provenance": { "sign": false }
}
```

`version: 1` is checked on read. An unknown major version is a hard error, not a
best effort. A v2 that starts life without this will never be able to add
anything.

### 2.4 Exit codes that mean something for automation

Today `main()` returns 0/1. A v2 needs a contract:

| Code | Meaning |
|---|---|
| 0 | all jobs in the request succeeded |
| 1 | at least one job failed; see `--results --json` |
| 2 | bad usage / unparseable request |
| 3 | queue unavailable or locked |
| 4 | library or configuration error |

A caller must be able to tell "your script was wrong" from "a photo failed to
develop" from a branch decision.

### 2.5 Output that is machine-readable

`--results --json` emitting a stable schema - imgid, input, output, status,
error, duration. Scripts parse it. The human-readable form can change; the JSON
schema cannot.

## 3. Relationship to `darktable-mcp`

Do not fork the concepts. `darktable-mcp` already answers "let an agent drive
this pipeline by parameter name", and it does it on a throwaway duplicate,
which is the right instinct.

The v2 job queue should be **the same introspection underneath**, with three
differences: it is batch and durable rather than interactive, it takes a request
file rather than natural language, and it promises determinism rather than
explanation. If the two diverge, the fix is to factor the parameter-name
resolution into one place - which is the point of darktable's introspection
system (`dev-doc/introspection.md`) in the first place.

`provenance.sign` in the request above is the hook the C2PA document
(`c2pa-signing.md`, step 5) plugs into. One flag, one place.

## 4. Sequencing

| Step | Work | Why first |
|---|---|---|
| 1 | Stable exit codes + `--results --json` | Small, immediately useful, no new concepts. |
| 2 | Versioned request file, single-shot | Makes the contract explicit before any durability. |
| 3 | `--submit` / `--status` | File-backed SQLite queue, single worker. |
| 4 | `--watch` / `--cancel` | Only after the queue has survived real failures. |
| 5 | `--provenance.sign` hook | Depends on the C2PA work. |
| 6 | Shared introspection layer with `darktable-mcp` | Refactor, do it when both are proven. |

Steps 1-2 are days and are useful on their own. Step 3 is the first one that
changes what a studio can do.

## 5. Known approximations and open questions

- **The queue is a concurrency surface in a codebase with recent concurrency
  bugs.** Single worker per queue, no exceptions, and the raw-denoise issues
  should be closed before this ships.
- **Where does the output state live?** darktable writes XMP; 5.8 adds
  `.dtdata` for per-pixel data. The request format has to say which one the
  caller's archive is keyed on, or a batch tool will produce archives nobody
  can reopen. This is the `INTAKE.md` lock-in question, and an automation API
  makes it much more expensive to get wrong.
- **No daemon means no push.** A caller must poll. That is a feature, not a
  limitation, but it should be said out loud rather than discovered.
- **What happens on library upgrade?** A queue with a month of pending jobs
  spans a release. Either pin the version per job or declare the queue invalid
  on upgrade. The first is more work and more honest.

## 6. Tests

- Unit: request parsing - every version, every malformed field, and the
  guarantee that a bad request never starts a job.
- Unit: the three-way distinction in section 2.4, asserted.
- Unit: request round trip - JSON in, equivalent explicit stack out. A style
  name in must become concrete parameters out, or determinism is a lie.
- Integration: submit 200 jobs, kill the process mid-run, restart, assert the
  queue resumes and no job is half-written. This is the test that matters, and
  it is the one that would be skipped by everyone else.

Every commit must build on its own, per `AGENTS.md`:

```bash
git rebase --exec 'cmake --build build' <base>
```

## 7. Upstream notes

Steps 1-2 would plausibly merge. The queue and the determinism contract are
where maintainer appetite will vary, and that is acceptable: the parts most
likely to be accepted are also the parts that make the rest possible.
