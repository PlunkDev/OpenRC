# RAC1 LevelFoundation package compiler

`compile_rac_level_foundation_package_v1` is the reusable clean-room boundary
between decoded Ratchet & Clank (2002) level assets and the first two native
runtime resources. It has no Veldin-specific path: the caller supplies a level
ID and the complete collision and gameplay assets for any supported level.

The public API is declared in
`openrc/rac_level_foundation_compile.hpp`.

## Input contract

`RacLevelFoundationCompileRequestV1` carries:

- the level ID, shared content API version, and supported disc build ID;
- one complete decoded RAC1 collision byte span;
- one complete decoded RAC1 gameplay-bank byte span;
- a stable logical locator for each source asset.

Locators use lower-case relative identifier syntax, such as
`rac1/level/018/core/collision`. Drive letters, backslashes, absolute paths,
empty components, `.` and `..` are rejected. They identify inputs in the
preparation graph and are never paths on the user's computer.

The compiler parses both byte spans itself with caller-provided limits. It
does not trust a structure parsed earlier by another stage. The complete input
span is the provenance range at logical offset zero; this makes its digest
unambiguous and avoids leaking WAD offsets into the runtime schema.

## Output resources

The result is a canonical base `LevelPackageV1` with exactly these foundation
resources:

| Resource ID | Type ID | Schema |
| --- | --- | ---: |
| `world/bootstrap` | `openrc.level-bootstrap` | 1 |
| `world/collision` | `openrc.collision-world` | 1 |

`world/bootstrap` is produced by the typed gameplay-bank adapter and contains
the absolute death plane and authored player spawn. `world/collision` is
produced by the RAC1 collision adapter and contains the neutral Q6 mesh and its
independently rebuilt uniform grid. The neutral payloads contain no source
record ranges, sparse-tree pointers, packed RAC1 vertices, WAD offsets, ISO
paths, or host paths.

Each resource has exactly two provenance records:

1. a `prepared_resource` record containing the logical source locator, exact
   complete-source byte count, and SHA-256;
2. an empty-range `generated` record naming the stable compiler pass.

The pass locators are
`compiler/openrc/rac1/level-bootstrap-v1` and
`compiler/openrc/rac1/collision-world-v1`.

Both resources set `overlay_replaceable`, allowing an explicit compatible mod
layer to replace them. Neither sets `overlay_removable`, because a runnable
level always requires a bootstrap and collision world. Existing
`LevelPackageV1` resolution enforces those flags.

## Determinism and policy

The compiler requires the collision adapter and collision serializer to use
the same `CollisionWorldBuildLimitsV1`, including grid-cell size. It then
serializes and parses the complete `LevelPackageV1` once before returning, so
the returned structure has canonical resource/provenance order and populated
payload digests. Equal source bytes, locators, build identity, level identity,
content API, and policies therefore produce byte-identical package output.

Every stage is bounded independently: source parsers, collision compilation,
collision payload encoding, bootstrap encoding, and package encoding all use
explicit caller limits. Errors from a source parser, semantic adapter, neutral
serializer, or package validator are reported as
`RacLevelFoundationCompileError` with their stage preserved in the message.
