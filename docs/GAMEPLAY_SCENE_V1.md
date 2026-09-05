# GameplaySceneV1

`GameplaySceneV1` is the versioned, source-independent interaction contract for
authored level entities. Its LevelPackageV1 identity is:

| Field | Value |
| --- | --- |
| resource ID | `world/gameplay` |
| type ID | `openrc.gameplay-scene` |
| schema version | 1 |

V1 deliberately contains only deterministic overlap collectibles. It does not
contain RAC Moby records, source class IDs, PVars, script addresses, PS2 units,
or a game-specific behavior dispatch table.

## Data model

Each collectible references one sparse `EntitySceneV1` `authored_id` and owns:

- a canonical semantic `item_key` used by neutral inventory;
- a local-space sphere center;
- a positive `u32` amount;
- a finite, positive collection radius;
- zero flags, because V1 defines no optional collectible behavior.

Collectibles are canonically ordered by strictly increasing `authored_id`.
Duplicate IDs, unknown flags, unsafe or non-canonical keys, signed-zero or
non-finite centers, non-positive amounts, and non-positive or non-finite radii
are rejected under explicit caller limits.

The binary payload is little-endian, begins with the eight-byte `ORGMPSCN`
magic, uses an exact 96-byte header and fixed 48-byte collectible records, and
ends with one packed canonical key partition. Readers require the exact format
and schema versions, record width, table offsets, total size, reserved zeros,
and key partition; no native C++ object layout is copied to disk.

## Entity and transform relationship

The combined package loader requires every collectible ID to name an existing
entity definition with an authored transform. The collectible sphere center is
transformed by the entity's complete scale-then-rotate-then-translate transform.
For non-uniform scale, the runtime multiplies the radius by the largest absolute
axis scale. This conservative sphere contains the authored ellipsoid and avoids
missing a valid overlap.

`EntitySceneV1` remains authoritative for identity, initial enabled state,
transform, and optional `RenderSceneV1` binding. `GameplaySceneV1` adds only the
interaction and inventory meaning. This separation lets another compiler or a
mod reuse the same runtime without adopting RAC class numbers.

## Deterministic runtime semantics

On a transactional scene load, the runtime materializes every entity definition
into `WorldV1` and retains a stable `authored_id` to runtime entity mapping.
Player definitions are materialized too, but player simulation remains
authoritative for their live transform. Authored transforms are immutable for
the V1 collectible pass.

Each fixed tick tests the player's upright world-Z capsule against enabled,
uncollected collectible spheres in ascending `authored_id` order. A successful
collection:

1. adds `amount` to the `u64` total for the semantic `item_key`;
2. emits a canonical event carrying tick, authored ID, item key, and amount;
3. destroys the world entity and marks it disabled and collected exactly once;
4. disables any bound static render instance through the entity/render binding.

Inventory overflow is preflighted before mutation; overflow, an invalid tick, or
an invalid input leaves the tick state unchanged. Snapshots expose canonical
entity state and lexicographically ordered item totals. Inventory totals are
generic rather than Bolt-specific and survive a successful level load or
reload; persistent save-file integration is a separate future layer.

## Current RAC1 compiler policy

The RAC adapter exists only on the compiler side of the package boundary. For
the supported reference profile, source Moby class 13 is treated as the Bolt
collectible. That semantic label is high-confidence community metadata, not a
claim that the numeric class-to-name mapping was recovered from a neutral
runtime field.

Matching records use their stable zero-based ordinal in the complete static
Moby placement table as the neutral authored ID. The compiler converts the
source placement to normal world units, derives the local overlap sphere from
the model bounds, and creates matching entity, render, and gameplay bindings.
It compiles the Bolt model through a temporary neutral actor library, freezes
the high-LOD model in its authored bind pose, and appends ordinary static
`RenderSceneV1` mesh/material/texture data and instances. The temporary Bolt
actor identity does not enter the runtime package and the renderer needs no RAC
class dispatch.

The current profile emits item key `openrc.currency/bolts` and assigns
`amount = 1` to every matching placement. The amount is an explicit OpenRC
gameplay policy because no per-placement currency amount has yet been recovered
from the source data. Bind-pose presentation likewise does not claim recovered
spin, hover, effects, sound, or original pickup timing.

## Integration status

The neutral schema, compiler adapter, deterministic collection state, and
render-instance visibility path are verified end to end for the supported PAL
v2.00 image. A fresh preparation plus reuse validation covered all 19
six-resource packages. Package-only smokes on Veldin and a second level each
collected a real Bolt, credited its semantic inventory key, disabled the bound
render instance, and completed a D3D11 draw from prepared data alone.
