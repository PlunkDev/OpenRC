# Milestone 1: original new-game flow

Scope confirmed by the user on 2026-09-08: OpenRC startup, original required
intro, original frontend/menu, normal new game, initial Veldin from beginning
to end, and its original events/cutscenes/progression into Novalis. Veldin is
the first playable planet, not a later destination after another first planet.
This extends the existing project; no second runtime, launcher, asset pipeline,
or throwaway first-planet implementation is authorized.

## Acceptance and present status

Current checkpoint, 2026-10-06: the normal sequence now continues through a
real `level/enter` consumer into controllable Veldin in the same window,
renderer and canonical session. The consumer moves the installed frontend
`GameSessionV1` into `RuntimeGameplaySessionOptionsV1::frontend_session`
(seed, state identity, bytes and revision retained; no initial values
replayed). It admits the scene through `D3d11Renderer::set_gameplay_scene`.
Gameplay admission and the interactive loop are the same code as the
developer `--level` path. The `new-game-sequence` smoke runs 600 fixed ticks
with scripted stick deflection through the controller-sample boundary. Result:
player displacement 24.2 units, revision 1195 unchanged, a non-frozen frame,
exit 0. An ordinary launch driven only by window input messages entered the
level without an exception and moved Ratchet with W. Logs:
`local/forensics/level-enter/`.

This does **not** qualify the original entry. The run reports
`original_entity_admission=0 original_entry_qualified=0 camera=developer
physics=openrc-policy`. The source map of the original entry is
[LEVEL_ENTER_SOURCE_V1.md](LEVEL_ENTER_SOURCE_V1.md). Compared with it:

- `0x2465f8` is the whole level module (one-time prologue, mode-dispatched
  frame loop, exit owner). The native consumer instead runs the neutral
  admission and OpenRC's 60 Hz fixed-step loop. There is no mode word,
  `2901a8`, frames-in-mode or PAL per-present update with catch-up.
- Entities come from the existing neutral loader, all at once. The source
  admits and constructs one record at a time inside `2422d8`, then runs a
  load-time Moby pass (`2657b8`) that is also absent here.
- The spawn is the bootstrap default. It is the same single class-0 Moby, but
  without the `25a6d0` height probe of `205278`, whose meaning is UNKNOWN.
  Ratchet's state also starts without the source's mode-gated set-state.
- Immediate control is consistent with the source at mode level: `2901a8`
  selects mode 0 for level 0, i.e. zero non-interactive frames (CONFIRMED in
  the source). A possible object-started scene on Veldin remains UNKNOWN and
  unhandled.
- Frame order differs. The source updates Mobys before the hero, then the
  camera (`1ed428`), and runs the projection update `1f7d00` last in render.
  OpenRC steps the developer camera in the movement mapper, then the player,
  then entity gameplay.
- The camera is the developer third-person prototype, not 1f7bc8/1f7d00. The
  recovered projection, the type-0 follow spring and the right-stick steps are
  documented in [RAC_GAMEPLAY_CAMERA_V1.md](RAC_GAMEPLAY_CAMERA_V1.md). Its
  clean `RacGameplayCameraV1` model is not yet connected here; eye
  composition and camera collision remain UNKNOWN.
- Character physics beyond the source pad response and standard ground pace
  is OpenRC policy.
- There is no HUD, AI, event, cutscene, particle or level audio integration.

The earlier 2026-10-03 checkpoint follows as history.

Previous checkpoint,2026-10-03: the normal sequence smoke verifies START →
original intro → menu → no-card dialog → confirmed New Game → actual frontend
retirement/preparation → all three original movies, three loading cards and
seven fades → completed Veldin load and transition cleanup. The same sequence
consumers serve ordinary launches; smoke supplies menu input. Preparation
checks the fresh PAL resource/state profile, joins the audio worker,
releases both banks and preserves the final framebuffer for its fade.
The v14 normal sequence additionally passes `level/admit-prepared-sections`:
3,457 source-compiled state writes preserve the existing session, loaded
content and framebuffer, with revision1194→1195. The next missing consumer is
`level/enter`; the world has not been entered or made playable through this
flow. The test explicitly reports `level_state_installed=1 level_admitted=0
world_entered=0 level_playable=0`. See `HANDOFF.md`,
`docs/STATE_INSTALLATION_V1.md` and
`local/forensics/frontend-transition/v14-normal-sequence-oct3-v1.log`.

The v10 component snapshot below is historical; its statements that menu input
is not integrated have been superseded by the checkpoint above. Its remaining
gameplay limitations are not cleared by the frontend tests.

**The end-to-end milestone has not passed.** Launcher Play now enters the
prepared original intro in the same runtime. Its complete PAL video and audio
playback pass. V10 continues through the original fade/copyright into the title
overlay, partial sky/terrain and1398 updates of five animated background actors.
All19 levels pass exact native validation. The captured normal runtime image
matches the independent GPU image. TIE/shrub, procedural sky sprites, fog and
camera-dependent terrain morph remain open; Press Start/menu input remains unintegrated. Explicit
`--level` remains a development smoke path. A full gameplay/fidelity QA pass cannot be credited
until the original flow actually runs. Historical roadmap checkmarks describe
their stated component scope, not original-game fidelity for the whole feature.

| Area | Status | What is actually established / still required |
| --- | --- | --- |
| One launcher, prepared-package boundary, portable import guards | DONE at infrastructure scope | Existing compilation, validation and package-only launch; this does not implement the PS2 frontend. |
| Original intro movie | PASS at media scope | Compiler-selected original PAL MPEG-2, prepared PCM, full runtime video/audio drain and actual D3D11 framebuffer comparison. Boot/prelude and post-intro frontend are not complete. |
| Original title/menu and New Game | PARTIAL | Title overlay, partial sky/terrain and animated actors run after copyright. Source menu models/lists, absent-card dialog, 12-update entry gate and live reset have source comparisons. The normal New Game sequence reaches controllable Veldin through `level/enter` (2026-10-06). The entry itself is not the original 2465f8 owner chain. |
| Level loading, geometry, textures and static collision | PARTIAL | Shared source-backed loaders/rendering exist; scene-family coverage and original loading lifecycle remain incomplete. |
| Original ordered actor initialization | PARTIAL | Admission/session state, ordered spatial lists, fresh constructor, authored color tail, indexed reference store, dynamic allocator, sequence/frame setter and scalar post continuation exist. Actual session-world materialization is shared. Remaining compound numerics, live-token/entity binding and ordered integration are still required. |
| Ratchet movement and gameplay animations | PARTIAL | Source animation banks and a grounded subset exist; full state transitions, acceleration/turning, airborne behavior and weapon handling are not verified. Prototype behavior is not fidelity evidence. |
| Camera | PARTIAL | A third-person prototype exists; the original camera is not reconstructed. |
| Enemies, weapons, damage and health | PARTIAL foundations | Neutral behavior/damage infrastructure exists; original complete per-type AI, attacks, weapon states/ammo/effects, health and reactions are not integrated. |
| Objects, crates, bolts and interactions | PARTIAL | Existing generic interaction loop includes explicitly non-original policies; it cannot be counted as faithful completion. |
| Death, respawn and checkpoints | PARTIAL | Generic reset/checkpoint machinery exists; original save/restore/event semantics remain open. |
| HUD | MISSING original implementation | Must use actual assets and authoritative gameplay state. |
| Audio | PARTIAL | Original intro PCM playback and sample clock work; gameplay routing, voice policy and mixing remain incomplete. |
| Cutscenes | PARTIAL components | Scene actors compile to the existing neutral animation player; camera/root samples, odd-update behavior and frontend background clock are recovered. Full scene rendering, synchronized audio, sequencing and return of control remain incomplete. |
| Triggers, events, scripts and progression to Novalis | PARTIAL state foundations | Persistent state/admission are not the complete event/progression system. |
| Developer Planet Select | MISSING dedicated tool | Existing explicit level launch is not progression; any UI must reuse the same loader and package-derived list. Lower priority than the original flow. |
| Full normal-flow test and separate fidelity pass | MISSING | No debug level load, forced flag, teleport, omitted cutscene or Planet Select can satisfy this gate. |

No external blocker currently prevents further in-scope work. Missing systems
are work remaining, not proof that the task is impossible. In particular,
remaining instruction coverage and live entity bindings are concrete dependencies
of full actor execution. ADD/SUB, ordered MUL/MADD/MSUB with explicit ACC overflow,
DIV and bounded SQRT now have integer reference models,
not blanket hardware qualification; a host-float approximation must not be
silently substituted or labeled confirmed. There is no new physical-PS2
numerical capture from this batch.

## Evidence discipline and immediate continuation

- **CONFIRMED:** source-derived control flow/formats and implemented component
  contracts retain their existing evidence. Numerical reference tests and
  finite public-result comparisons are not new physical hardware captures. Original data and
  instruction traces stay in ignored `local/forensics`. Current additions:
  [shared keyed text and original font metrics](RAC_TEXT_BANK_V1.md),
  [original frontend textures](RAC_FRONTEND_TEXTURE_V1.md), and
  [bounded source numerical recovery](SOURCE_NUMERIC_RECOVERY_V1.md), plus
  [original text layout/glyph plans](RAC_TEXT_LAYOUT_V1.md),
  [quad and frontend submission scope](RAC_FRONTEND_DRAW_V1.md),
  [the original frontend text-node callback](RAC_FRONTEND_TEXT_NODE_V1.md),
  [the fresh Moby constructor](RAC_MOBY_FRESH_CONSTRUCTOR_V1.md) and
  [the authored color tail](RAC_MOBY_AUTHORED_TAIL_V1.md) and
  [the indexed reference-store adapter](RAC_MOBY_REFERENCE_V1.md) and
  [staged dynamic allocation](RAC_MOBY_ALLOCATE_V1.md) and
  [the original sequence/frame setter](RAC_MOBY_SEQUENCE_SET_V1.md),
  [the scalar post-step continuation](RAC_MOBY_POST_V1.md) and
  [ordered multiplication](SOURCE_MULTIPLIER_REFERENCE_V1.md).
- **INFERRED:** any interpretation beyond those contracts must be labeled and
  independently testable; a likely gameplay meaning does not become a constant.
- **UNKNOWN:** original frontend presentation/audio dispatch and remaining
  numerical/gameplay semantics are not filled with placeholders.

Keep the current admission, session-state and spatial-index implementation.
Continue the exact constructor/post-step with qualified arithmetic and original
ordered side effects, then bind it to the actual loaded entity world, now
shared by the owning session and entity materializer. In
parallel, continue from the recovered layout/glyph/quad and submission owners
into qualified numeric execution and complete enclosing/inherited render-state
contracts. Decoding
strings alone does not justify a made-up UI resource or renderer. Reuse the
scene/audio parsers when tracing required
intro/cutscene dispatch; do not re-inventory all unchanged decoded assets.

At each coherent change: build the portable package, run targeted and full
regressions, record source-versus-native comparisons separately from hardware
or visual fidelity, and retain a precise handoff. Completion requires all 33
user acceptance checks in the clarified startup-to-Novalis scope, followed by
a separate original-fidelity pass.

## Earlier verified batch (2026-09-08)

The latest complete portable Release build passed **142/142 tests** and all
required PE import audits, then published the existing launcher, compiler and
runtime. Actual runtime materialization now adopts the owning session's loaded
world, preserving its level-instance and selected-spawn identity. The separate
stored shadow session is removed; the main world view exposes the actual
entities. Transactional load/failure tests cover persistent state, entity
removal, replacement, allocation failure and reentrant mutation rejection.

The original post-step's conditional source/native comparison matches 1,008
cases and 4,023 checkpoints, including full actor projections, ordered writes
and typed request values. Rotation/blend/derived numerical observations are
explicitly external, and 287 reached spatial tails remain pending. The scale
method separately executes seven ordered reference products. This is not a
completed original actor initializer. The frontend half-color calculation now
executes in its exact byte domain: all 65,536 channel pairs and 96 enclosing
callback cases match original instruction routing. Timer and timed-color
results were external in that batch; floating glyph arithmetic remains symbolic. Earlier constructor,
allocator, sequence setter, quad and transport comparisons retain their stated
component evidence; none substitutes for original-flow integration.

EE/VU standalone MUL/MULA now use a source-ordered integer reference model,
including the noncommutative low-column carry correction, value packing and
operation-local flag events. Independent generated-oracle tests and finite
primary-author result comparisons corroborate that model. An ignored audit
of 4,724 multiplication steps derived from actual Veldin inputs finds nine
intermediate differences from ideal-product/truncation, without claiming a
complete rotation or physical-console capture. Compound MADD/MSUB, hidden ACC
state, division, full projection and hardware qualification remain open.

Because the existing VU preparation executor now uses those MUL semantics,
the compiler identity advances to `native-eight-resource-v7-vu-mul`.
The published compiler correctly rejects the old v6 profile as stale. Fresh
all-19-level preparation completed and was verified on 2026-09-12, including
package/graphical smoke and Prepare reuse. There remain eight resources per
level and one preparation path. The newer v8 shared package is described below.

The frontend owner still records unexecuted callees; IMAGE retention does not
implement GS residency or rasterization. These checks do not add a normal-flow
or original visual/gameplay fidelity pass.

Those smoke checks exercise development loading and forced interaction probes.
They do **not** satisfy the normal startup/menu/new-game playthrough, original
progression or a visual/gameplay fidelity pass. The acceptance table above
therefore remains unchanged at whole-system scope.

## Current implementation (2026-09-12)

The compiler profile is now `0.1.0-native-eight-resource-v8-startup-media`.
An optional shared reference in PreparedGameV2 carries the original selected
intro as neutral MediaClipV1. Real v7-to-v8 migration and exact validation passed;
all nineteen level files retained their hashes. A second Prepare reported
already prepared. Veldin/Novalis package smoke retained replay values
`11091236615096623899` and `1540951510915972271` respectively. Novalis is only
a regression of shared mechanisms while the current task prioritizes Veldin.

The full PAL intro executes in the runtime: 363 decoded video frames,
640,976 stereo sample frames and completed audio drain. Actual GPU readback
compares 961 samples against decoded pixels with a maximum channel error of
1/255. This caught and fixed a double gamma conversion that a completion-only
smoke could not detect. The source logical movie raster is preserved;
physical-console overscan/display stretch have not been measured.

The frontend timer and bounded dyadic timed-color paths now execute. Actual
Press Start, action 4, dialog-ready and no-save/reset/request arms execute as
compiler-side source effects; the full reset comparison matches all 267 payload
copies and 280 writes. Scene actor animation uses the existing neutral player.
The original fifteen-chunk frontend background passes two complete source
clock loops, with 2,796 samples and 310,356 sampled joints for actor zero.
These component comparisons do not supply the missing menu renderer or full
cutscene sequence. General DIV/MADD/MSUB/hidden ACC remain unqualified.

The actual main-list callback and its integer glyph emitter now execute:
192 source/native callback cases produce 4,102 matching quads; 384 separate
glyph cases match 7,318 complete source packets. This is the source's integer
path, not a substitute for the separate floating text-node owner. The final
portable build passes 149/149 tests and PE audits. Published v8 graphical
smoke passes for levels 0/1. Source-verified reuse also rejects modified movie
provenance even when its container hashes have been recomputed.

The next integration boundary is the original post-intro title/frontend:
copyright/prelude, actual animated object admission, projection and presentation,
then the recovered main-list/input flow. Do not substitute three flat labels
for the original scene. Current evidence, commands and exact artifact hashes
are maintained in [HANDOFF.md](../HANDOFF.md), [media format/runtime](MEDIA_CLIP_V1.md)
and [scene animation adapter](RAC_SCENE_ANIMATION_COMPILE_V1.md).
