# RAC gameplay camera V1

This note records what is recovered about the original Ratchet & Clank PAL
v2.00 gameplay camera on Veldin and which part of it OpenRC models. Evidence
was taken from the boot executable `SCES_509.16` and the Veldin level overlay
(primary extent LBA 1886019, overlay SHA256 `922fd06e…876c79`). Addresses are
EE virtual addresses after the overlay is installed. Every claim is labeled
CONFIRMED (read directly from source instructions/data), INFERRED, or UNKNOWN.

The developer `ThirdPersonCameraV1` remains a separate, unchanged OpenRC
policy. The new `RacGameplay*V1` API is additive and is not connected to the
renderer or runtime.

## Projection `1f7bc8` / `1f7d00`

CONFIRMED: level entry `2465f8` calls `23d9c0`, which calls `1f7bc8` and then
`1f7d00` (`2466e0`, `23dac4`, `23dacc`). `1f7bc8` installs these scalars in
projection owner `16cb40`:

| Field | Bits | Value | Meaning |
| --- | --- | --- | --- |
| `+a0` | `42000000` | 32.0 | near distance |
| `+a4` | `49360000` | 745472.0 | far distance |
| `+b0` | `3f2147ae` | 0.63 | horizontal half-angle tangent |
| `+200`/`+204` | runtime | `0.5 * s16[1519d0/1519d2]` | viewport half size |

The float stores occur in the order `+a0`, `+b0`, `+a4`. The display
dimensions at `1519d0/1519d2` are runtime state (UNKNOWN statically), so the
API takes the half sizes from its caller.

CONFIRMED: `1f7d00` reads the word at `15ee80`. Nonzero selects the PAL factor
`3f418937` (0.756), zero selects `3f466666` (0.775), and `+b4 = +b0 * factor`.
The matrix at `16cb40+c0` is then written in this operand order, with
`t = near * (far - near)` and depth scale `cafffbe0` (-8388080.0):

| Entry | Formula |
| --- | --- |
| `+c0` | `half_width / (+b0 * near)` |
| `+d4` | `half_height / (+b4 * near)` |
| `+e8` | `((far + near) / t) * depth_scale` |
| `+f8` | `(((near * -2) * far) / t) * depth_scale` |
| `+ec` | `(1 / near) * +210` (fog/depth-range term, not modeled) |
| others | zero |

CONFIRMED: cutscenes copy scene camera record `+1c` into `+b0`
(`28f858`, `291ce0`, `29eb30`) and gameplay restores `0.63` afterwards
(`291b20`, `29ea50`, `299f00`). `2913bc..2913f8` temporarily raises `+b0` to
at least 0.63 around one render pass and restores it; its purpose is UNKNOWN.
The world-unit scale applied before projection (`vf30.z` at `gp-24800`) is
UNKNOWN, so near/far are reported only as raw source scalars.

`rac_gameplay_projection_v1` returns `+b0`, `+b4`, `+c0`, `+d4`, `+e8` and
`+f8`. `rac_gameplay_projection_defaults_v1` supplies the `1f7bc8` scalars.

## Gameplay camera type 0

CONFIRMED: `lvl.camvtbl` (`1eac00`, 20-byte records) maps camera type 0 to
init `2e8210` and update `2eb0d8`; the slot dispatcher `1ebda0` calls each
selected camera's update. INFERRED: type 0 is the ordinary follow camera,
because it alone reads the player position (`13f4d0`) and the right stick.

The update order is `2e5b68`, `2e74b0` (special-state overrides), `2e6ce0`
(focus), `2eb060` (look input and eye), then `2e72e8`, which restores the
default spring constants every update.

### Spring helper `1eb5c0`

CONFIRMED and modeled by `rac_camera_spring_step_v1`:

```
delta    = target - current
velocity = velocity + (k * delta - d * velocity)
if max != 0: clamp velocity to [-max, max]
clamp velocity to [-|delta|, +|delta|]
return current + velocity
```

The angular variant `1eb6a8` wraps the difference and result to `[-pi, pi]`
and is not modeled.

### Focus follow (`2e6ce0`, default mode 0)

CONFIRMED: the player position and the focus `+160` are split along the up
vector `166f40` into vertical and horizontal parts. Each part follows the
player through `1eb5c0` per component with `k = 3c75c28f` (0.015),
`d = 3e4ccccd` (0.2) and no maximum. Horizontal velocities live at
`+176..+184` and vertical velocities at `+192..+200`. Initialization
`2e58e0` (when the player word `13f75c` is zero) sets the focus to the player
position and clears both velocity slots.

INFERRED: on ordinary Veldin ground the up vector is `(0, 0, 1)`.
`RacGameplayCameraV1` models only that case: each world axis follows
independently, one call to `step_pal_frame` per original update. The update
has no time-step factor. CONFIRMED: other camera modes and player states
replace these constants (`2e9a40`, `2e6be0`). The selecting state semantics
are UNKNOWN.

### Look input, pitch and distance

CONFIRMED (`2e9e60`, `2ea068`): yaw input is right stick X (pad `+0x100`),
negated when `15eee0 == 0`. Pitch input is the negated right stick Y
(`+0x104`), or the positive value when `15eedc == 0`. INFERRED: those words
are axis-inversion options. Pitch uses a strict 0.3 dead zone. The remaining
travel is scaled by `3fb6db6e` (1.4285714), as modeled by
`rac_gameplay_camera_pitch_response_v1`. It is then smoothed with a maximum
step of 0.01 or 0.02 and multiplied by `3f32b8c2` (40 degrees). The yaw step
per update is the smoothed input times one of `3c8efa35`, `3cb9dede` or
`3ce4c388` (1, 1.3 or 1.6 degrees), selected by `15eee4`. No button-driven
camera rotation was found in this path (UNKNOWN, not assumed).

CONFIRMED constants with INFERRED roles: the maximum eye offset length is
`40947ae1` (4.64, `2ea3f0`), and the eye-height scales are 2.0 and 1.5. The
mode-dependent heights are 3.0, 0.4 and 0.5 (`2e6fb0`). The final pitch
difference is clamped to `+-3f9c61aa` (70 degrees, `2ea4c0`). Eye smoothing
uses `1eb5c0`/`1eb6a8` with `k` 0.004, 0.005, 0.015 or 0.02 and `d` 0.2.
The full eye composition and the collision queries (`1efff0`, the moby
avoidance pass `2e91d0`) are UNKNOWN as a whole and are not modeled.

## Numeric qualification

The API evaluates single-precision values in the recovered operand order with
host IEEE arithmetic. It is not a bit-exact EE COP1 `MUL.S`/`DIV.S` model, and
`RacGameplayProjectionV1::ee_bit_exact` is `false`. Source constants are
stored as their exact bit patterns.
