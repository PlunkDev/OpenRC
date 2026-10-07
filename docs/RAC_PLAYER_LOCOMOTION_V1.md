# RAC player ground locomotion V1

This note records the recovered source behavior behind the pure ground-frame
primitives in `include/openrc/rac_player_locomotion.hpp`. The reference is
SCES-50916 PAL v2.00 (`docs/REFERENCE_BUILD.md`). Each statement is marked
CONFIRMED (read directly from the executable code), INFERRED (consistent
interpretation, not proven), or UNKNOWN. The local forensic notes and scripts
that reproduce every address live in the ignored
`local/forensics/player-ground` directory; no game bytes are stored here.

## Numeric policy

The implementation evaluates host IEEE-754 binary32 in the source operation
order, one operation per statement so no fused multiply-add can form. The EE
FPU and VU0 rounding behavior is not emulated, so results are not
hardware-qualified (UNKNOWN for last-bit agreement). The tests compare with an
independent binary32 transcription of the same operation order.

## Address space

- CONFIRMED: the player update functions are not in the boot ELF image. They
  come from Veldin primary-extent-0 subrange 0, a 0x1930e4-byte raw image whose
  code maps as `file offset + 0x15f1b4` (1021 `jal` targets land on function
  prologues; the standard pace wrapper spans exactly `0x2122a0..0x2123ac`).
  Data used here maps as `file offset + 0x15f060` (pace table and the three
  131-entry dispatch tables match their code references).
- CONFIRMED: `gp = 0x166d00`; the player structure is at `0x13f450`, the pad
  structure at `0x13ca40`.
- CONFIRMED: dispatch tables T1 `0x1e81a0` (update), T2 `0x1e83b0` (entry, run
  by `set_state 0x222b80`), T3 `0x1e8630` (transitions). State 0 uses
  T1 `0x218188`, T2 `0x222d9c`, T3 `0x22a7ac`; state 2 uses T1 `0x21c6d8`,
  T2 `0x223a60`, T3 `0x22bc04`.

## Frame timing

- CONFIRMED: `0x2623d0(a0 != 0)` writes the 50 Hz block: dt `[0x15ee6c] =
  0x3ca3d70b` (one ULP above the nearest binary32 of 0.02), dt squared
  `[0x15ee70] = 0x39d1b718`, time scale `[0x15ee60] = 1.2`, its square
  `[0x15ee64] = 0x3fb851ec`, frame-count scale `[0x15ee68] = 0x3f555555`.
  `RacPlayerFrameTimingV1` defaults to these values.
- INFERRED: retail PAL play selects this branch; the selector byte was not
  traced to its first write.
- CONFIRMED: `0x1feed0(n)` converts a 60 Hz frame count:
  `(int)(0.5 + n * [0x15ee68])`. PAL values used below: 4→3, 5→4, 24→20,
  25→21. UNKNOWN: rounding-sensitive inputs such as 15.

## Per-frame order and the one-sample delay

CONFIRMED order inside `0x211d28`, called once per update:

1. `0x211ee8` stores `min(|v|, 1)` of the vector still at player
   `+0x1d20/+0x1d24` into `+0x229c`. This is the previous frame's sample.
2. `0x211d64` copies the new left-stick floats from pad `+0x108/+0x10c`.
3. If the new vector is strictly shorter than 0.25 (`0x3e800000`), both
   components are replaced by `bit13 - bit15` and `bit14 - bit12` of the pad
   word at `+0x1b0` (`0x211d70..0x211ddc`). INFERRED: those bits are the
   d-pad Up/Right/Down/Left, because ELF `0x218238..0x218250` builds the word
   as `~((b0 << 8) | b1)` from the DualShock button bytes.
4. T1 runs the state update, including the edge probe described below;
   collision and measurement follow (`0x221a98`); `0x221d50` advances the
   per-state frame counters and countdowns.
5. The caller then runs T3 transitions (`0x22a340`).

Therefore every transition reads the previous gated sample while the target
pace and target yaw read the current one (CONFIRMED). The magnitude seen by
the source is either zero (or a digital direction) or at least 0.25, which the
existing `map_rac_player_standard_ground_movement_v1` does not apply; the
additive `gate_rac_player_stick_sample_v1` reproduces the gate.

## Transitions

- CONFIRMED state 0 → 2 request (`0x22aef0`): `0.22 < previous magnitude`,
  strict. Earlier T3[0] exits take precedence, including pad buttons, the
  vertical guard below, a non-zero countdown `+0x1c4`, and, during the first
  20 PAL frames after states 1 or 30 while `+0x308` is zero, a veto
  (`0x22ae6c..0x22aee8`): T3 recomputes the target yaw from the current
  sample (`0x211f80`) and suppresses the request while its angular distance
  to `[0x166ed8]` (`0x2001a0`: `|a - b|`, folded to `2π - d` at or above π)
  exceeds `1.2217305` (`0x3f9c61aa`). Not implemented.
- CONFIRMED vertical guards: the probe `0x212e70` increments `+0x30e` every
  frame and resets it (`0x213230..0x2132a4`) when the signed probe distance
  `+0x2dc` is below 0.02 (`0x3ca3d70a`) and either the angle `+0x2e0` is at
  most `0.8726646` (`0x3f5f66f3`), the byte `+0x20b3` is 1, or the category
  `+0x208c` is 22. State 0 falls to state 6 when `3 < +0x30e` and
  `0.4 < +0x2dc`; state 2 checks `4 <= +0x30e` and `1.35 < +0x2dc`.
  INFERRED: these are frames without supporting ground, height above ground,
  and ground slope.
- CONFIRMED state 2 stop path (`0x22c104`): only state 2 itself opens it,
  with `previous magnitude < 0.17`, strict. Inside it, a measured speed
  `+0x160` above `2.7 * dt` selects the skid state 3 (unless `+0x20aa`);
  otherwise state 0 is entered only after more than 21 PAL frames in state 2.
  At or above 0.17 the substate helper `0x2293e8` runs instead.
- CONFIRMED substate helper extension: the 2.35/1.90 hysteresis is evaluated
  only when more than 3 PAL frames passed since the last state or substate
  change and `+0x3bc` is clear.
- CONFIRMED substate helper early exit (`0x22940c..0x22946c`): when the byte
  `+0x20a4` is 3, the helper only writes the animation rate
  `+0xa90 = +0x160 * 54` raised to at least 0.7; when it is 1, `+0x160 * 90`
  raised to at least 0.5. It then returns without the counter gate or the
  hysteresis. UNKNOWN: the meaning of `+0x20a4` (written at `0x211130` and
  `0x21128c`) and whether it is ever 1 or 3 in state 2.

The pure step reports `idle_requests_ground_move` and
`ground_move_opens_stop_path` but does not switch states: the remaining guards
need the collision probe, counters, and flags that this layer does not own.
It also does not apply the edge probe below, which can zero the pace inside
the same state update.

## State 2 entry

- CONFIRMED (`0x223a60`): substate 0; pace `+0x194` = planar length of the
  velocity `+0x110`, limited by `7 * dt` (`0x40e00000`, PAL limit
  `0x3e0f5c2a`), unless the previous or second previous category is 4 (pace
  kept). A reversal of more than 90 degrees after state 3 or category 7 zeroes
  the pace. Flags redirect the entry to states 47, 115, or 63.
- `enter_rac_player_ground_move_v1` implements only the standard limit; the
  caller supplies the measured planar velocity length.

## Target pace and target yaw

- CONFIRMED (`0x211f80`, wrapper `0x2122a0`): a zero sample gives target
  pace 0 and target yaw equal to the current facing; otherwise
  `target = (m < 0.82 ? 0.9 : 5.7) * dt` per frame and the target yaw is the
  stick angle `0x1ff860(-y, -x)` plus `[0x166ed8]`, wrapped once.
- INFERRED: `0x1ff860` is an atan2-type polynomial and `[0x166ed8]` is the
  camera yaw (maintained by `0x1f3e20` beside the stick transform matrix
  `0x1670d0`). The caller computes the target yaw.

## Pace smoothing

- CONFIRMED `0x25c8c0`: `delta = T - v; v += (s < delta) ? s :
  (delta < -s ? -s : delta)`; returns `|T - v|`.
- CONFIRMED `0x212740`: the acceleration step is used only while the actual
  pace is below the target, otherwise the deceleration step.
- CONFIRMED state 2: acceleration `(dt^2 * 7.5) * scale`, deceleration
  `dt^2 * 8.5` (`0x40f00000`, `0x41080000`). The scale is 0 when the substate
  is 0, the previous magnitude is below 0.8 (`0x3f4ccccd`), and the signed
  remaining turn left by the previous frame exceeds `0.5235988`
  (`0x3f060a92`); otherwise 1.
- CONFIRMED state 0: target 0, deceleration `dt^2 * 12.6` (`0x4149999a`),
  no facing turn. The edge probe runs earlier in the same update.
- PAL results (host binary32): 0 → fast target `0x3de978d6` in 39 frames,
  back to 0 in 34 frames, idle decay in 23 frames.

## Turning

- CONFIRMED (`0x2124e8` → `0x25ccf0`, `0x25c7f0`): facing `+0x98` and angular
  velocity `+0x184` follow
  `omega += k * delta - d * omega`, clamped to `±max` when `max > 0`, then to
  `±|delta|`; `facing = wrap(facing + omega)`. When the remaining angle is
  below `max * 0.01` (`0x3c23d70a`) the facing snaps to the target and omega
  becomes zero. The signed remainder is stored at `+0x188`. Wraps are single
  `±2π` corrections with `π = 0x40490fdb`.
- CONFIRMED coefficients (`ts2` = time scale squared, `m` = previous
  magnitude): substate 1 uses `ts2*0.008`, `ts2*0.15`, `dt*9.948377`; with
  `0.82 < m`, `b = (m+1)*0.5` and `(ts2*0.019)*b`, `ts2*0.1`,
  `(dt*9.948377)*b`; otherwise `b = m+0.35` and `(ts2*0.005)*b`, `ts2*0.1`,
  `(dt*4.712389)*b`.

## Edge probe

- CONFIRMED callers of `0x2178a0(scale, minimum)`: the state-2 update calls
  `0x2178a0(5.0, 0.2)` at `0x21cb5c..0x21cb7c` whenever the halfword
  `+0x1f4` is non-zero, after the pace smoothing `0x212740` and the velocity
  update `0x212790`. The update shared by states 0, 1, 3, 4, 61, and 128
  calls `0x2178a0(3.7, 0.0)` unconditionally at `0x218204`, before the idle
  pace decay.
- CONFIRMED `0x2178a0` returns without effect when the planar length of the
  velocity `+0xe0` is zero; when `0.5 < previous magnitude`, the category
  `+0x208c` is not 6, `+0x1f4` is zero, and the byte `+0x12e7` is zero (never
  on the state-2 call); or when `+0x30e` is non-zero. Otherwise it builds the
  offset `+0xe0 * scale` (scale 2.0 in state 32). When `0 < minimum` and the
  offset is planar-shorter than the minimum, its planar part is cleared
  (`0x234420` → `0x1ff500`) and `minimum * (C(r), S(r))` is added
  (`0x233f88`, `C = 0x1ff798`, `S = 0x1ff7b0`, `r` = the word at
  `[+0x2080] + 0x48`). The offset's z is set to zero, the position `+0x80`
  is added, and `0x1efff0` probes the segment from 0.3 above that point to
  0.2 below it (0.35 in state 32, 0.7 when the byte `+0x20a4` is 2).
- CONFIRMED: unless `0x1efff0` returns non-zero, the word `[0x173f5c]` is
  positive, and `0x2345b0(0x173f80)` is at most `0.8726646` (`0x3f5f66f3`),
  the probe calls `0x2144a0(0.0)`. `0x2144a0(f)` multiplies the xyz of
  `+0x100`, `+0x110`, `+0xe0`, and `+0x150` and the pace `+0x194` by `f`
  (`0x214504..0x21450c`), so the pace and the velocities become zero in that
  same update.
- CONFIRMED: the vector helpers above (`0x234250`, `0x234420`, `0x233f88`,
  `0x2343a0`, `0x234090`, `0x2345b0`) are described for byte
  `+0x20b3 == 0`; for 1 or 2 they take separate matrix-transform paths
  (UNKNOWN). On that path `0x2345b0` returns `0x1ff860(z, planar length)` of
  its vector.
- INFERRED: the probe looks for walkable ground ahead of the player (the
  limit equals the slope bound of `0x212e70`, `C`/`S` are cosine/sine, `r` is
  the model yaw, and `0x173f80` is the hit normal), so it stops Ratchet at
  ledges and steep surfaces. Whether it also triggers at walls is not proven.
- CONFIRMED: `+0x1f4` is a halfword countdown; `0x221d50` decrements it
  once per update through `0x1fef48` (call `0x221e5c`).
- UNKNOWN: who arms `+0x1f4` and what it means; the internals of
  `0x1efff0` and of its result block `0x173f40`.
- CONFIRMED for direct calls only (`reach_pace.py`): from the state-2 update
  `0x21c6d8`, the reachable writers of `+0x194` are the smoothing `0x212740`
  (through `0x25c8c0`) and `0x2144a0`, which is reached only through
  `0x2178a0`. T3[2] `0x22bc04` without `set_state`, and the per-frame hero
  update `0x2076e8` without the T1 switch and `set_state`, reach none.
  UNKNOWN: the 21 indirect `jalr`/`jr` transfers that the walk does not
  follow.

## Wall contact

- CONFIRMED: the measured speeds `+0x160` (3-D) and `+0x164` (planar) are
  computed from the processed velocity `+0x110` in `0x213f38`; they select
  the skid stop and seed the next state-2 entry.
- INFERRED: wall contact acts through those measured speeds if the collision
  in `0x213f38` changes `+0x110`.
- UNKNOWN: the collision and slide arithmetic in `0x213f38` and callees.

## Not implemented (UNKNOWN or exception-only)

- The edge probe `0x2178a0` and its zeroing `0x2144a0(0.0)`: in state 2 after
  the pace step while `+0x1f4` is non-zero, and in state 0 before the idle
  decay. A caller of `step_rac_player_ground_frame_v1` must not treat its
  pace as final in those frames.
- The `+0x20b3 != 0` turn path `0x212430` (earlier read as strafe/lock; the
  meaning of `+0x20b3` is UNKNOWN), the skid flag `+0x3be`
  (target scale `clamp(2.618 - +0x188, 0.2, 1)`, deceleration `dt^2 * 17`,
  turn factors 1.1/1.2), `+0x20a8` (acceleration `max(1 - 0.55*turn, 0)`),
  item-dependent turn coefficients, the external push `+0x930`, and the
  states 47/63/115 formulas.
- The velocity vector `0x263cc8` (`(C(yaw)*v)*C(p)`, `(S(yaw)*v)*C(p)`,
  `S(p)*v`) awaits proof of the trigonometric helpers.
- Animation rate `+0xa90` from the substate helper:
  `min(max(A*157, 0.6), 4.0)` for slot 3 and `min(max(A*14, 0.6), 2.2)` for
  slot 4 (CONFIRMED arithmetic, not wired).
