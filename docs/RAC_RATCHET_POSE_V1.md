# RAC Ratchet sequence and pose recovery V1

## Scope

This is a compiler-side clean-room contract for regular Ratchet animation
frames in the supported PAL v2.00 build. It does not cross the package boundary:
the native runtime still consumes only neutral OpenRC resources and never sees
RAC offsets, tags, or packed records.

`RacLevelCoreIndexV1` preserves all 256 logical sequence slots. Zero slots stay
zero and duplicate offsets remain aliases. A separate sorted list owns one
bounded range for each unique non-zero sequence asset.

## Structural frame envelope

A sequence owns a `0x1c` header, one relative 32-bit offset per frame, a bounded
trigger-word list, optional pre-frame bytes, and regular frames. Non-zero high
offset bits are rejected because none occur in the complete supported corpus.

A regular frame has a `0x10` byte structural header followed by a qword-aligned
payload:

| Payload region | Size |
| --- | ---: |
| joint rotations | `joint_count * 8` |
| sparse scale records | `scale_count * 8` |
| sparse translation records | `translation_count * 8` |
| opaque alignment padding | `0..15` bytes |

The parser requires the declared translation offset to equal the end of the
rotation and scale regions, and the declared qword count to equal the aligned
end of the translation region. Padding is retained byte-for-byte and is not
assigned a meaning.

## Confirmed pose semantics

Each rotation record is `s16 x, y, z, w`. Components are divided by 32768 and
the quaternion is normalized. Each sparse scale record is `u16 sx, sy, sz,
tag`, with scale divided by 4096 and `joint = tag & 0xff`. Bit `0x8000` places
the scale in the local transform; without that bit, it is applied only to the
completed global transform for that joint. Other high tag bits are unsupported.

Each sparse translation record is `s16 tx, ty, tz, tag`. It replaces, rather
than adds to, the joint's common translation. Both common and sparse source
translations are converted to neutral model units with `class_scale / 1024`.

For joint `i`, the compiler evaluates:

```text
L_i = [ R(q_i) * diag(local_scale_i), local_translation_i ]
H_root = L_root
H_i = H_parent(i) * L_i
G_i = H_i * diag(terminal_scale_i)
Skin_i = G_i * InverseBind_i
```

Terminal scale is deliberately absent from `H`, so it cannot propagate to a
child. Sparse records for the same joint are rejected rather than resolved by
an invented precedence rule.

## Singular authored transforms

Zero scale is valid source behavior. Across 33,977 frames, it occurs on local
joint 12 Z, local joint 13 Z, and terminal joint 91 Y. Veldin sequence 0 frame 0
uses zero Z scale on joint 13, which directly influences 28 high-LOD vertices.

`RacRatchetPoseV1` therefore keeps finite singular global and skin matrices.
The general full-vertex skinning path remains strict because inverse-transpose
normal handling is undefined for a singular transform. The separate neutral
position-only skinning path accepts finite singular palettes and is suitable
for the current unlit renderer. Recovering the original VU0 normal policy is a
separate task; zero scale is never changed to an epsilon or identity.

## Neutral clip and runtime boundary

The native compiler converts explicitly selected source sequences into
`ActorAnimationBankV1`; packed records and source slots stop at that boundary.
Each neutral clip stores a semantic key, the exact rig key and canonical rig
digest, a pose for every joint in every frame, its integer source cadence, and
loop/clamp policy. Current clean-room evidence maps only idle slot 0, walk slot
3, and run slot 4, all as looping grounded-locomotion clips. No jump, fall,
landing, or wrench mapping is inferred.

The generic player advances the PAL 50 Hz source cadence on the 60 Hz fixed
runtime with an integer accumulator, interpolates normalized quaternions on the
shortest hemisphere, linearly interpolates translation and scale, then composes
the hierarchy and inverse bind described above. Airborne ticks hold the last
grounded palette. Because authored zero scales remain singular, the current
unlit renderer consumes the position-only skinning result while retaining the
source UVs and colors.

## Corpus verification

The PAL all-level regression covers:

- 19 levels;
- 1,804 unique sequences;
- 33,977 frames;
- 90,514,728 finite global/skin matrix components;
- 25,267 singular global matrices and 25,267 matching skin matrices;
- 5,583 finite position-skinned vertices for Veldin sequence 0 frame 0.

This proves the regular source decoder. The three confirmed locomotion clips
also pass neutral serialization, exact rig binding, deterministic cadence,
selection, interpolation, package mounting, and runtime playback. Remaining
animation semantics and original normal handling are separate open work.
