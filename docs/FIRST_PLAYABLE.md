# Milestone 1: original new-game flow

Scope confirmed by the user on 2026-09-08: OpenRC startup, original required
intro, original frontend/menu, normal new game, initial Veldin from beginning
to end, and its original events/cutscenes/progression into Novalis. Veldin is
the first playable planet, not a later destination after another first planet.
This extends the existing project; no second runtime, launcher, asset pipeline,
or throwaway first-planet implementation is authorized.

## Acceptance and present status

**The end-to-end milestone has not passed.** The current launcher enters a
requested prepared level directly. That path is a development smoke path, not
the original new-game flow. A full gameplay/fidelity QA pass cannot be credited
until the original flow actually runs. Historical roadmap checkmarks describe
their stated component scope, not original-game fidelity for the whole feature.

| Area | Status | What is actually established / still required |
| --- | --- | --- |
| One launcher, prepared-package boundary, portable import guards | DONE at infrastructure scope | Existing compilation, validation and package-only launch; this does not implement the PS2 frontend. |
| Startup, intro and original menu | MISSING as a playable flow | Original keyed text, font metrics and frontend textures are recovered content prerequisites, not a replacement menu. Original layout, input, transitions, presentation and audio still need reconstruction. |
| Level loading, geometry, textures and static collision | PARTIAL | Shared source-backed loaders/rendering exist; scene-family coverage and original loading lifecycle remain incomplete. |
| Original ordered actor initialization | PARTIAL | Admission/session state and ordered spatial lists exist. Full constructor/post-step numerics, live-token/entity binding and ordered integration remain required. |
| Ratchet movement and gameplay animations | PARTIAL | Source animation banks and a grounded subset exist; full state transitions, acceleration/turning, airborne behavior and weapon handling are not verified. Prototype behavior is not fidelity evidence. |
| Camera | PARTIAL | A third-person prototype exists; the original camera is not reconstructed. |
| Enemies, weapons, damage and health | PARTIAL foundations | Neutral behavior/damage infrastructure exists; original complete per-type AI, attacks, weapon states/ammo/effects, health and reactions are not integrated. |
| Objects, crates, bolts and interactions | PARTIAL | Existing generic interaction loop includes explicitly non-original policies; it cannot be counted as faithful completion. |
| Death, respawn and checkpoints | PARTIAL | Generic reset/checkpoint machinery exists; original save/restore/event semantics remain open. |
| HUD | MISSING original implementation | Must use actual assets and authoritative gameplay state. |
| Audio | PARTIAL formats | ADPCM/VAG/SBlk decoding exists; original routing, voice policy, mixing and required playback do not. |
| Cutscenes | PARTIAL formats | Structural camera/actor/subtitle decoding exists; synchronized original playback and return of control do not. |
| Triggers, events, scripts and progression to Novalis | PARTIAL state foundations | Persistent state/admission are not the complete event/progression system. |
| Developer Planet Select | MISSING dedicated tool | Existing explicit level launch is not progression; any UI must reuse the same loader and package-derived list. Lower priority than the original flow. |
| Full normal-flow test and separate fidelity pass | MISSING | No debug level load, forced flag, teleport, omitted cutscene or Planet Select can satisfy this gate. |

No external blocker currently prevents further in-scope work. Missing systems
are work remaining, not proof that the task is impossible. In particular,
unqualified VU multiply/add/ACC rounding is a concrete dependency of exact actor
bounds; a host-float approximation must not be silently substituted or labeled
confirmed. There is no physical-PS2 numerical capture from this batch.

## Evidence discipline and immediate continuation

- **CONFIRMED:** source-derived control flow/formats and implemented component
  contracts retain their existing evidence. PS2 integer-conversion tests are
  specification-derived, not physical hardware captures. Original data and
  instruction traces stay in ignored `local/forensics`. Current additions:
  [shared keyed text and original font metrics](RAC_TEXT_BANK_V1.md),
  [original frontend textures](RAC_FRONTEND_TEXTURE_V1.md), and
  [bounded source numerical recovery](SOURCE_NUMERIC_RECOVERY_V1.md).
- **INFERRED:** any interpretation beyond those contracts must be labeled and
  independently testable; a likely gameplay meaning does not become a constant.
- **UNKNOWN:** original frontend presentation/audio dispatch and remaining
  numerical/gameplay semantics are not filled with placeholders.

Keep the current admission, session-state and spatial-index implementation.
Continue the exact constructor/post-step with qualified arithmetic and original
ordered side effects, then bind it to the actual loaded entity world. In
parallel, extend the recovered font metrics/atlases and bounded glyph-emitter
evidence with original layout, glyph/control-stream and inherited render-state
contracts. Decoding
strings alone does not justify a made-up UI resource or renderer. Reuse the
scene/audio parsers when tracing required
intro/cutscene dispatch; do not re-inventory all unchanged decoded assets.

At each coherent change: build the portable package, run targeted and full
regressions, record source-versus-native comparisons separately from hardware
or visual fidelity, and retain a precise handoff. Completion requires all 33
user acceptance checks in the clarified startup-to-Novalis scope, followed by
a separate original-fidelity pass.

## Latest verified batch (2026-09-08)

The complete portable Release build passed **127/127 tests** and all required
PE import audits, then published the existing launcher, compiler and runtime.
A fresh source preparation published all 19 level packages with compiler
profile `native-eight-resource-v5-vu-loi`; repeating Prepare verified and reused
the same data. Source comparisons for text, font metrics, frontend textures,
integer spatial projection and frame structure are recorded in their linked
component notes. The Veldin and Novalis package-only checks and hidden
graphical smoke passed again on the final published executables.

Those smoke checks exercise development loading and forced interaction probes.
They do **not** satisfy the normal startup/menu/new-game playthrough, original
progression or a visual/gameplay fidelity pass. The acceptance table above
therefore remains unchanged at whole-system scope.
