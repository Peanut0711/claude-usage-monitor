# Touch-Scroll Tuning (menu / settings cursor)

How the right-edge drag scrollbar moves the cursor in the nav menu and settings
screen, every tuning mechanism applied so far, the current state, and ideas to
try next. All of this lives in one function: `scrubUpdate()` in `src/main.cpp`
(called once per ~16 ms frame while the menu or settings overlay is open).

## Interaction model (context)

- **Move the cursor**: drag up/down inside the right band (x ≈ 326–448). It's a
  **relative swipe** — the cursor moves by how far you drag, not where you tap.
  A tap with no travel does nothing. The thin bar at the far-right edge is
  view-only (shows the cursor); you don't touch it. The far-right edge is left
  out of the band on purpose (the Home-button sensor there throws touch errors).
- **Select**: the **Home button** (never a tap). IO12 = cursor down, IO16 =
  cursor up, both wrap around. (IO16 is the lock button only on the main screen.)
- Exact word taps also select, but selection is meant to be Home/buttons.

## Mechanisms applied (in `scrubUpdate`)

Each was added to fix a specific feel problem. Order ≈ the order we hit them.

| Mechanism | Constant / form | What it does | Problem it fixed |
| --- | --- | --- | --- |
| **Grab zone** | `inScrollBand(x,y,rows)` | Only start a drag if the first touch is in the right band | Stray taps elsewhere shouldn't scroll |
| **Relative accumulate** | `accum += dy` each frame | Sum finger travel; advance one row per `STEP` px | Replaced absolute "tap a point = jump there", which the user didn't want |
| **Per-row threshold** | `STEP = 7` px | px of swipe needed per row (lower = more sensitive) | Overall sensitivity knob |
| **First-row threshold** | `FIRST = 3` px | First row of a drag triggers at a smaller distance, then `STEP` | "Dead at the start" feel before the first row |
| **Glitch cap** | `GLITCH = 70` px | Ignore a single-frame jump bigger than this | Capacitive panel reports a spurious far coordinate → sudden multi-row jump |
| **Contact-dropout grace** | `GRACE = 2` frames | Keep the swipe alive across brief touch losses | Flaky contact reset the anchor → swipe felt stuck/slow |
| **Lift-off discard (pipeline)** | 1-frame delay (`p1`) | Commit a frame's movement only after the next frame confirms touch continued; the held frame is dropped on release | Cursor jumped one row **in the travel direction** when lifting (lift-off coordinate smear) |
| **One row per frame** | `if`, not `while` | At most one row advances per frame; leftover `accum` clamped to ±`STEP` | A hard/fast push committed several rows at once (burst jump) |

### Current constants (as flashed)
```
STEP   = 7     // per-row distance (sensitivity)
FIRST  = 3     // first-row distance (snappy start)
GLITCH = 70    // single-frame jump rejected above this
GRACE  = 2     // tolerated dropped-touch frames
pipeline = 1   // frames of lift-off discard (p1 only)
loop   ≈ 16ms  // delay(16) in the menu/settings modal -> ~60 fps sampling
```

## Current-state analysis

**Tension we kept hitting:** every lift-off fix (delay before committing) also
delays the *start*. The lift-off smear only happens at **release**, but we can't
know release is coming, so the same delay applies to both ends.

- Pipeline **2** → release jump ~gone, but start felt laggy ("must move more
  before it starts").
- Pipeline **1** (current) → snappier start; a residual lift-off can still nudge,
  but the **one-row-per-frame cap** bounds it to **at most 1 row**.
- The **one-row-per-frame cap** is the single biggest win for "확 튐" (burst): a
  fast flick now spreads across frames (~60 rows/s max) instead of jumping 3–4
  rows in one frame.

**Known root irritant — the panel itself:** the CST226SE reports
- a shifting coordinate during finger lift-off (direction-consistent smear), and
- occasional spurious spikes,
which is *why* we need glitch caps, grace, and the discard pipeline at all. None
of this is "acceleration" — the mapping is strictly linear (distance → rows).

**Open feel issues (as of last test):** snappy start is good; whether the
≤1-row release nudge is acceptable is the remaining judgment call.

**Direction reversal (down→up etc.):** works, but `accum` is a *signed* running
sum that carries across the reversal, so there's a small slack. Example (STEP=7):
after a down-step `accum` sits in [0,7); reversing to up must first cancel that
residual (up to 7 px) and then reach −7 — so the first reversed row needs up to
~STEP *more* travel than continuing in the same direction. Also `moved` stays
true, so the reversed first row uses `STEP`, not the snappier `FIRST`; and the
1-frame pipeline delays the reversal by a frame. Bounded/safe (residual clamped
to ±STEP, no burst), just slightly notchy at the turn. See TODO for the fix
(snap `accum` to 0 + re-arm `FIRST` on a sign change).

## Ideas to try next

Roughly best-value first.

1. **Settle-lock (park-to-commit).** When the finger stays ~stationary for N
   frames, freeze the cursor; ignore further drift until a clear re-drag. Kills
   the lift-off nudge for the common "reach target → pause → lift" case without
   adding start lag. Risk: tuning the "stationary" threshold vs a genuinely slow
   drag (too low → slow drags falsely lock). Pair a small still-threshold (≤1–2
   px) with a short frame count.
2. **Input smoothing (low-pass).** `filtY += (ty - filtY) * α`. Damps jitter and
   softens spikes so the glitch cap rarely triggers. Adds a little lag (α tunes
   it). Could replace or complement the discard pipeline.
3. **Asymmetric delay.** Commit the *first* row immediately (no pipeline) but keep
   the discard for ongoing movement + release — instant start, lift-off still
   handled mid/late-drag. More code; resolves the core tension directly.
4. **Velocity / momentum (kinetic) scroll.** Flick to fling, decelerate. Nice on
   long lists; overkill for 4–6 rows and reintroduces overshoot — probably skip.
5. **Tune what we have.** STEP/FIRST are live knobs (lower = more sensitive).
   Pipeline length is the start-lag ↔ release-jump dial (0/1/2).
6. **Touch driver upgrade.** The CST226SE has no INT line wired (polled over I2C;
   see POWER.md). A faster/cleaner sample path, or characterizing the lift-off
   signature (does the controller flag "lift" on the last report?), would let us
   drop the *real* lift-off frame precisely instead of heuristically.
7. **Avoid the bad zone entirely.** The band already dodges the right-edge Home
   sensor. If specific x/y still misbehave, narrow `M_CTRL_X0/X1` further.

## Where to look

- `src/main.cpp` → `scrubUpdate()` — all the logic + constants above.
- `src/main.cpp` → menu / settings modal blocks — call `scrubUpdate()` and the
  IO12/IO16 cursor moves.
- `src/display/DisplayHAL.cpp` → `inScrollBand()`, `drawScrollbar()`, layout
  constants (`M_CTRL_X0/X1`, `M_ROW_H`, `M_HDR_H`).
