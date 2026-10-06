# Original level entry: Veldin from 0x2465f8 to the first controlled frame

This is a **source map**, not an implementation. It records what the supported
PAL v2.00 executable and Veldin overlay do between the `level/enter` barrier
(entry `0x2465f8`) and the first gameplay frame in which the pad drives Ratchet.
Statuses: **CONFIRMED** (static instruction path read), **INFERRED**
(structure, debug strings or consistency; not executed) and **UNKNOWN**
(evidence missing, named below). Nothing was executed on hardware or an
emulator. Camera internals belong to the gameplay-camera work, ground movement
and jump/landing to their own locomotion tasks; for those only call sites are
listed. Detailed paths and tool provenance stay in the ignored
`local/forensics/level-enter-source/level-enter-source-evidence.md`.

## 1. Owners from 0x2465f8 to the gameplay loop

**Hand-off (CONFIRMED).** Resident `12db18` is a module loop: it calls the
current module entry, then `12da38`, which copies the loaded image's section
chain and returns the chain's common entry word. The Veldin image's seven
sections all carry entry `0x2465f8`, so `level/enter` is the overlay's module
entry. `0x2465f8` is the whole level module: a one-time prologue, a frame loop
and, when the level-exit flag is set, an exit owner (`2940e0`) and a return to
`12db18`.

**One-time prologue, in order:**

| Order | Owner | Role | Status |
|---|---|---|---|
| 1 | `1ff018` | Clear the overlay's second section (`0x161f00..0x1660b0`; BSS-like use INFERRED) | CONFIRMED |
| 2 | `294df0` | Calls resident services | role UNKNOWN |
| 3 | stores | Frames-in-mode `0x15f6ac = 0`; **game mode `0x15f6a8 = 6`** | CONFIRMED |
| 4 | `23d9c0` | View/render state reset; one-time camera calls `1f7bc8`/`1f7d00` | sites CONFIRMED, role INFERRED |
| 5 | `244ae0(1, 0)` | Level load and world construction (below) | CONFIRMED |
| 6 | stores, `1f39b8` | Subsystem enable mask `0x16c158+8 = 15`, video selector copy | CONFIRMED |
| 7 | `246ec0`, `235828`, `276f18` | Map art, unknown, "misc anim" | roles INFERRED/UNKNOWN |
| 8 | `2901a8` | **Mode selection: level 0 → mode 0**; most other levels → arrival mode 6 | CONFIRMED |
| 9 | `122598(0)` | Video-field sync; presented-frame counter `0x15f4f8 = 0` | CONFIRMED |
| 10 | `200ec8`, `266210`, then `2666a8` or `266670(0)` | Two unconditional calls, then a branch: on level 0, `2666a8` only when byte `0x13d498` is non-zero, otherwise `266670(0)` | order and branch CONFIRMED, roles UNKNOWN |
| 11 | stores, `12ec30` | Per-level first-visit flag and visit/time statistics | stores CONFIRMED, meaning INFERRED |

Rows 9–11 still belong to the one-time prologue: the prologue ends at
`0x2468a4` and the frame loop starts at `0x2468a8` (CONFIRMED), so the video
sync in row 9 is not the first loop iteration.

**Inside `244ae0(1, 0)` (CONFIRMED order):** level data unpacking and table
setup → **`2422d8` world setup** → memory-map diagnostics → `2860d8` (clear
the level-carry buffer, because the second argument is 0) → **`2657b8`** builds
the moby update list and runs **one load-time update pass** (per-moby animation
step, class callback and post-step), all while the mode is still 6.

**Inside `2422d8` (CONFIRMED order):** allocation and table setup → **Moby
admission loop `0x243210..0x2435f0`**, one record at a time: the scalar step
`0x243210..0x2433d8` ([admission](RAC_MOBY_ADMISSION_V1.md)), then for an
accepted record the [fresh constructor](RAC_MOBY_FRESH_CONSTRUCTOR_V1.md)
(`24f968`), `25a6d0`, the [post-step](RAC_MOBY_POST_V1.md) `251e30`,
`251308` and the reference helper `24b1b0`, before the next record (the existing
documents are not re-derived here) → post-admission tables → a conditional
ship object (input not traced: UNKNOWN for Veldin) → occlusion checks →
**hero init `205278`** → **hero state reset `205598`** → `1ed6d8` → one
camera update `1ed428` → `267b90` → `1fb3a8`.

**Gameplay loop, every iteration (CONFIRMED order):** level-exit check →
accumulate the hardware frame timer → frame-begin owners (`1f70f0`, `2a1b58`,
`2a1ae8`, `200ff0`, `201300`, `200ef0`, resident `12ddc0`; roles UNKNOWN) →
**mode dispatch** (nine arms, table indexed by mode+1) → frames-in-mode counter
→ optional frame-skip exit → `24c308`, `24b638` → conditional status overlay →
frame-time ratio → **overrun catch-up** (mode 0 or 2: a second pad+update
before presentation when the timer exceeds one frame budget) → video sync,
presented-frame counter, `285f18` → `1f88c0` → respawn reload (when the player
requested it) → statistics. All of these run once per iteration; the prologue
owners above run once per level entry.

## 2. Non-interactive phase before control

* **No mode-level phase on Veldin (CONFIRMED).** `2901a8` selects mode 0 for
  level 0, so the first loop iteration already runs the gameplay arm: pad,
  hero update and camera update. Arrival (mode 6), scene playback (mode 2) and
  transition (mode 3) are not selected by the entry. Duration: **0 frames**.
* The pause/menu input is gated until 8 frames in mode 0 (CONFIRMED); this
  does not gate Ratchet's control.
* Object-driven scenes remain possible but are **UNKNOWN** for the New Game
  start: one placed Veldin class has a callback that can start a mode-2 scene
  after a proximity test and either an authored automatic flag or a button.
  The two other scene-starting classes are not placed on Veldin. Veldin has
  seven scene-animation containers (visible through `wad-scene-animation`);
  none is proven to belong to the start. Help-message triggers and any fade
  owner were not identified. Missing: that object's per-instance inputs, the
  help-message owner and a fade owner.

## 3. Ratchet at the first controlled frame

* **Position and rotation (CONFIRMED path).** `205278` clears the whole player
  structure, takes the first live Moby whose class is 0 (Veldin's only class-0
  placement, record 0, consistent with the existing spawn rule), probes it with
  `25a6d0` and replaces its height only when the probe returns a positive
  value, then copies the Moby position and Z rotation into the player and
  writes them back through `208e98` and the post-step. The probe's numeric
  meaning is **UNKNOWN** (INFERRED: vertical ground probe).
* **State number 0 (CONFIRMED).** `205598` writes the state word to 0 directly
  and calls set-state `222b80(0, 1)`. Because the mode is still 6 during load,
  the set-state gate rejects it, so the state-0 entry handler (idle-slot
  selection and animation set) does **not** run at load. The first frame runs
  the state-0 handler of the first dispatch table.
* **Animation (INFERRED).** No load-time owner sets Ratchet's sequence, so it
  keeps the fresh constructor's sequence 0, frame 0. Effects of the first
  frame's per-moby animation step are not traced.
* **Velocity (INFERRED 0).** Every player field not written by `205278`/`205598`
  is zero after the clear (CONFIRMED); field semantics belong to the
  locomotion tasks.
* **Moby lists (CONFIRMED).** The player Moby is flagged before `2657b8`, which
  excludes it from the load-time pass; class 0 has no class callback (INFERRED
  null). The hero update, not the Moby loop, drives Ratchet.
* **Hand item (INFERRED).** A per-frame equip owner attaches item 8 (class 71,
  INFERRED wrench) once its frame-counter gate opens, i.e. not in the first
  update.
* **Clank (UNKNOWN).** No owner on the entry path creates a backpack object;
  the attached-object slots and pack-item records are leads only.

## 4. Order of one mode-0 frame

1. Pad `268738` → `267d98` → `268020` (CONFIRMED). The address `0x2182b0`
   belongs to the boot executable's axis response; the Veldin overlay carries
   its own copy inside `268020` (`2680e0..268160`), and in the Veldin image
   `0x2182b0` is unrelated player-state code.
2. `299250`: debug/pause gates, then subsystem groups gated by the enable mask,
   labelled by retail-empty profiler markers (attribution INFERRED):
   **moby update** (`24f7e0`, Moby loop `2658c0`: animation step `2514b8`,
   class callback, post-step `251e30`; then `28f2a0`, `2a5b20`) →
   **hero update** `2076e8` (`211d28`: pad stick copy, **first dispatch table
   at `0x218174`** inside `2180f8`; then `20bfd8`…`20d9c8`; **third table at
   `0x22a798`** inside `22a340`; then player-to-Moby sync `208e98`, equip owner
   `210fe0` and further hero owners; the **second table at `0x222d88`** is the
   set-state entry) → **part update** (`268aa8`, `1fe9c8`) → **camera update**
   `1ed428` at `299a10` → **sound update** `28dee0` → `23f228`, `250be0`,
   `23b610`, `2042d8` → `299148`.
3. Render `1f91b0` → `1f8938`: render setup, sky, effects, moby effects,
   particles, post effects, blur, **HUD**, screen overlays, packet submission,
   patch lighting, and finally `1f74f0` → **`1f7d00` at `1f75d8`**. In mode 0,
   `1f7bc8` is not called per frame; it runs in prologue owner `23d9c0`.
4. Loop tail as in section 1 (overrun catch-up repeats steps 1–2 only).

## 5. OpenRC ownership and next tasks

| Original owner | OpenRC component | Status |
|---|---|---|
| Entry hand-off, section admission | `rac_new_game_flow` barriers `level/admit-prepared-sections` and `level/enter`, `state_installation` | PARTIAL (`level/enter` has no original consumer) |
| Mode word, `2901a8`, frames-in-mode, pause gate | none | MISSING |
| `244ae0` data load | `prepared_game_v2*`, `runtime_level_foundation`, `runtime_level_content` | PARTIAL (order) |
| Admission loop in `2422d8` | `rac_moby_admission`, `placement_admission`, `rac_moby_fresh_constructor`, `rac_moby_authored_tail`, `rac_moby_reference`, `ordered_spatial_index`, `rac_moby_post` | PARTIAL (not interleaved; `game_world` creates all entities at once) |
| Ship object | none | MISSING / UNKNOWN |
| `205278`, `205598` | `level_bootstrap`, `player_simulation`, `runtime_player_actor` | PARTIAL (no probe, reset or gated set-state) |
| `2657b8`, `2658c0`, class callbacks | `runtime_actor_behavior`, `runtime_actor_schedule`, `runtime_actor_animation` | PARTIAL |
| Pad | `rac_pad_input`, `game_input` | PARTIAL (axis response done) |
| Hero update, three tables | `rac_player_locomotion`, `player_simulation`, `runtime_player_animation` | PARTIAL |
| Camera `1ed428`, `1f7bc8`/`1f7d00` | `third_person_camera` (developer) | PARTIAL, not original |
| Particles | none | MISSING |
| Sound update | audio components | UNKNOWN mapping |
| Render, HUD | `src/runtime/d3d11_renderer.*`, `render_scene`, `runtime_render_scene` | PARTIAL; HUD MISSING |
| Frame timing | `fixed_step` at 60 Hz | PARTIAL (original: one update per PAL present, plus catch-up) |

The current runtime advances the player before entity gameplay; the original
updates Mobys before the hero in mode 0.

Ordered implementation tasks:

1. **Original frame scheduler and mode word.** Mode dispatch, `2901a8`
   selection (level 0 → 0), frames-in-mode, PAL single update per present with
   overrun catch-up, pause gate. No dependency; unblocks 3–5.
2. **Interleaved level-entry transaction.** Run `2422d8`'s per-record admission,
   constructor, post-step and tail in source order, with absent identities and
   atomic publication. Builds on the committed v14 components; depends on
   connecting `RacMobyPostV1` to the spatial owner.
3. **Hero initialisation lowering.** `205278`/`205598` semantics including the
   class-0 lookup, the `25a6d0` probe (needs its reverse), the structure reset
   and the mode-gated set-state. Depends on 1 and 2.
4. **Mode-0 subsystem order.** Moby loop before the hero, hero update order
   (stick copy, first and third tables, player-to-Moby sync, equip), then
   particles, camera call site and sound; render with the projection update
   last. Depends on 1–3 and on the locomotion/camera tasks.
5. **Load-time Moby pass and Veldin class callbacks.** `2657b8` and the placed
   classes' callbacks, including the scene-starting object and the mode-2
   scene owner, to settle the first-frame triggers. Depends on 1 and 2.

Reverse prerequisites (UNKNOWN items): `25a6d0`, that object's per-instance
inputs, the Clank attachment owner, the roles of the unnamed prologue and
frame owners, the ship branch input and the first-frame animation step.
