# RAC player airborne contract V1

## Scope

This is the clean-room contract for Ratchet's standard jump in the supported
PAL v2.00 build (SCES-50916). It was traced in the loaded Veldin overlay
through the three player dispatch tables: update T1 `0x1e81a0`, entry T2
`0x1e83b0` (called by `set_state 0x222b80`) and transitions T3 `0x1e8630`.
Claims are labeled CONFIRMED (read from the instructions), INFERRED or
UNKNOWN. Numeric results are host IEEE binary32 evaluations of the source
operation order; the EE FPU rounding mode and the VU0 square root are not
hardware-qualified.

The model is a pure function beside `PlayerSimulationV1`. It does not change
that class, its profile, `PlayerSimulationSnapshotV1` or the snapshot hash.
The current runtime physics (`gravity 18`, `jump_speed 7`, …) remains an
OpenRC policy until a separate integration task connects this model.

## States

| Source state | T1 update | T2 entry | T3 transitions | Status |
| --- | --- | --- | --- | --- |
| 7, standard jump | `0x21f258` | `0x224d1c` | `0x22efe0` | CONFIRMED |
| 6, unsupported fall | `0x21eb80` | `0x224aa4` | `0x22e584` | vertical step and landing CONFIRMED |
| 9, 11, other 7..18 | shared with 7 | shared, per-state switch | shared | UNKNOWN parameters |

Transitions into state 7 (CONFIRMED):

- the jump button is pad bit `0x40` (INFERRED: Cross), read from a 30-entry
  press ring by `0x267ba8` over a buffered window;
- state 0 checks the button before its walk request, window frames(7) = 6;
- state 2 uses window frames(9) (UNKNOWN: 7 or 8 on PAL); slow substate goes
  to 7; the fast substate goes to 9 only when the stick is above 0.82, the
  turn is under 60° and planar speed exceeds `5.7 * dt * 0.85`;
- with bits `0xa` held a separate check can select state 11 instead.

An edge without a jump enters state 6: state 0 after more than frames(4)
unsupported frames above 0.4, state 2 after at least frames(5) above 1.35.
The unsupported-frame counter `+0x30e` is cleared only when `+0x2dc < 0.02`
together with a walkable slope, `+0x20b3 == 1` or category 22
(`0x213230..0x2132a4`). Before that transition can happen, the ground
updates run the edge probe `0x2178a0`: unconditionally as `(3.7, 0.0)` in
the shared state-0 update and as `(5.0, 0.2)` in state 2 while the
countdown `+0x1f4` is non-zero (the probe itself has early returns). Without
a walkable hit it calls
`0x2144a0(0.0)`, which zeroes `+0x194` and the `+0x100`, `+0x110`, `+0xe0` and
`+0x150` vectors in the same frame (`docs/RAC_PLAYER_LOCOMOTION_V1.md`). The
probe and its segment test `0x1efff0` are **not implemented** here; a ledge
may therefore stop the original player where OpenRC lets it walk off.

## Vertical step (state 7)

`dt = 0x3ca3d70b` and `dt2 = 0x39d1b718` are the PAL frame step and its
square. Entry first copies the measured displacement `+0x110` over the
velocity `+0xe0` (`0x224d68..0x224d78`), then writes gravity`
`g = dt2 * 29.7`, descent gravity `g * 1.17`, height `h = 1.47`, maximum`
`2.62`, takeoff window frames(5) = 4 and the ramp length frames(15). Each PAL frame, in source order:

1. after the takeoff window, a vertical velocity below `0.001` marks the apex
   and switches to descent gravity;
2. while Cross is held, or before any impulse exists, and while
   `frames_in_state <= frames(15)`: `h += (2.62 - 1.47) / frames(15)`,
   clamped to 2.62, and the pending impulse becomes
   `pending + ((sqrt((h + h) * g) - applied) - pending)`;
3. after the takeoff window a positive pending impulse is added to the
   vertical velocity and moved into the applied total;
4. inside the takeoff window the velocity is `0 - dt2 * 48`; afterwards it is
   `v - g`, raised to `measured - 0.1` and to `-(dt * 50)`.

`measured` is the previous collision step's vertical displacement. The
collision itself is outside the model.

frames(n) is `(int)(0.5 + n * 0.8333333)` evaluated with `ADDA.S`/`MADD.S`.
For n = 15 the sum is 12.9999997 before `CVT.W.S`: 13 with host rounding and
12 with truncation. `RacJumpVerticalProfileV1::height_ramp_frames` therefore
has no default and must be chosen explicitly.

Free-flight reference (measured motion fed back from the previous frame):

| Case | Peak frame | Peak height | Back to start height | Terminal speed reached |
| --- | ---: | --- | ---: | ---: |
| held, ramp 12 | 22 | 1.9842327 (`0x3ffdfb56`) | 39 | 95 |
| held, ramp 13 | 22 | 1.9587371 (`0x3ffab7e6`) | 39 | 95 |
| one-frame tap, ramp 12 | 17 | 1.1841749 (`0x3f97930b`) | 31 | 90 |

Terminal speed is `-(dt * 50) = 0xbf800001`, one ULP beyond -1 because the
PAL frame step is one ULP above 0.02.

## Landing from a jump

Ground contact after the apex starts a landing phase inside state 7 (counter
`+0x418`). From its second frame `0x229b78` leaves it (CONFIRMED):

- pad bits `0xa` held: state 4 with slot 13;
- previous stick magnitude above 0.3 and no input lock: state 2, entering the
  fast substate with slot 4 directly;
- `+0x168` above `3 * dt` after frames(3) landing frames: skid state 3,
  slot 5;
- after frames(12) = 10 landing frames or when the landing animation reports
  completion: state 0 with the idle sequence.

Cross pressed within frames(8) = 7 buffered frames during the landing jumps
again through the state-2 choice (7 or 9). A jump still airborne after
frames(70) and higher than `+0x490` above ground becomes state 6.

## Unsupported fall (state 6)

Entry copies the measured displacement into the velocity. Each PAL frame the
vertical velocity loses `dt2 * 24` (`0x3c1d4952`) and is limited to
`-(dt * 50)`; there is no apex switch and no measured-motion floor. From rest
the terminal speed is reached on frame 104. The fall enters its slot-11
phase after frames(18) = 15 frames or above 1.75 height.

On ground contact the vertical velocity is raised to `dt * -9`
(`0xbe3851ec`) and the next state is chosen in this order: state 61 without
hit points (INFERRED meaning); a hard landing to state 0 with slot 12 after
frames(90) = 75 frames, starting at source frame 4; a slot-11-phase landing
to state 0 with slot 12 from source frame 9 and an input lock of frames(7) =
6. The source would extend that lock to frames(18) after frames(50) frames,
but the accepted `set_state(0, 0)` zeroes `+0x198` at `0x227db0` before the
comparison reads it, so the extension is only reachable when set_state is
vetoed. Otherwise state 2 when the previous stick magnitude exceeds 0.5 and no lock
is active, state 3 with slot 6 above `3 * dt` planar speed, else state 0.

## PAL frame counts

`frames(n) = (int)(0.5 + n * 0.8333333)` is evaluated with `ADDA.S`,
`MADD.S` and `CVT.W.S`. frames(3), frames(9) and frames(15) fall within one
ULP below an integer, so truncation gives 2, 7, 12 and host rounding 3, 8,
13. Every other count used here is independent of the rounding mode. The
ambiguous ones stay caller-supplied or undocumented until a console run of
`0x1feed0` qualifies the result.

## Horizontal air control (partial)

After takeoff, state 7 turns toward the target yaw with
`k = ts * 0.04`, `d = ts * 0.2` and a maximum of `dt * 15.009831`, and
accelerates the planar velocity toward the stick target by at most
`dt2 * 20` per frame (`dt2 * 2` or `dt2 * 6` with a small stick). The vertical
component is never changed by this step. The speed target and the state-6
and state-9/11 parameters remain open.

## Animation slots

- CONFIRMED: state-7 entry selects source slot 7 with a frames(5) blend.
- CONFIRMED: state-7 landing (ground contact after the apex) re-selects the
  current slot through an identity mapping, with a frames(7) blend and the
  argument `(int)12.0 + 2`. That argument is the source frame stored at
  Moby `+0x51` (CONFIRMED: the `0x229618` slot remap passes its mapped frame
  through it), so the landing is the tail of slot 7 from frame 14, not a
  separate slot. The re-selection is skipped when the value returned by
  `0x24fe00` (a 1/16-scaled halfword, INFERRED frame position) is not below
  `30 - 1.5`; that gate is why the tail is not implemented.
- CONFIRMED: state-6 entry selects slot 10 by default, slot 11 above 1.75
  height, and slot 4 or 6 depending on an UNKNOWN mode byte; the fall later
  switches to slot 11 and lands through slot 12, slot 6 (skid) or the entry
  of state 0/2.

`RuntimePlayerAnimationProfileV1` gains two optional keys after the V1
fields: `jump_clip_key` (intended slot 7) and `fall_clip_key` (intended slot
10). Both empty keep the V1 behavior of holding the last grounded pose. The
neutral snapshot has no source state, so the first airborne tick picks the
jump clip for upward velocity and the fall clip otherwise; this choice is an
OpenRC adapter policy. The clip plays until the player is grounded again,
which re-enters the existing grounded selection (slot 3 or idle). Source
blends and the slot-7 landing tail are not reproduced.

Two further optional keys refine the fall role: `long_fall_clip_key`
(intended slot 11) replaces the fall clip once the airborne time reaches
frames(18) = 15 PAL frames, and `fall_landing_clip_key` (intended slot 12)
plays on the first grounded tick after such a fall. Its start frame and input
lock come from `select_rac_fall_landing_v1`: source frame 9 with a
frames(7) = 6 lock, or source frame 4 without a written lock after
frames(90) = 75 PAL frames. Shorter falls and jumps return to the grounded
selection immediately. The landing clip holds until it completes a cycle,
or until horizontal motion appears after the lock has elapsed. Converting
runtime ticks to PAL frames through the clip cadence, ignoring the 1.75
height test and ending the landing on motion are adapter policy.

## Code

`include/openrc/player_simulation.hpp` exposes the CONFIRMED arithmetic as
pure functions: `enter_rac_jump_vertical_v1` and `step_rac_jump_vertical_v1`
(state 7), `step_rac_fall_vertical_v1`, `land_rac_fall_vertical_v1`,
`rac_fall_enters_long_phase_v1` and `select_rac_fall_landing_v1` (state 6).
The collision, the measured displacement and the source fields with unknown
meaning are explicit inputs.

## Open work

- Hardware result of frames(3), frames(9), frames(15) and of VU0 `VSQRT`.
- The speed target of the air control, state 45, the parameters of states 9,
  11 and 17, and the writers of `+0x1b4` and `+0x1e2`.
- The edge probe `0x2178a0`/`0x1efff0` and the writer of `+0x1f4`.
- Connecting the model to `PlayerSimulationV1` (opt-in) and the runtime.
