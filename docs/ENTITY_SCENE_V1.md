# EntitySceneV1

`EntitySceneV1` is the versioned, planet-independent authored-entity contract
used by OpenRC packages. Its LevelPackageV1 identity is:

| Field | Value |
| --- | --- |
| resource ID | `world/entities` |
| type ID | `openrc.entity-scene` |
| schema version | 1 |

The resource describes identities and component relationships. It does not
contain RAC moby records, PVars, source class IDs, PS2 addresses, executable
logic, or an implicit dispatch table for one retail build.

## Data model

Every entity definition has a sparse, stable `authored_id`, a semantic
`archetype_key`, flags, and an optional authoring-group ID retained only as
provenance. Separate canonical component tables may bind that authored ID to:

- a world transform;
- one RenderSceneV1 instance;
- one ActorLibraryV1 model plus a general affine model-to-entity transform;
- one local player slot.

Render and actor bindings are mutually exclusive. A non-player definition has
exactly one authored transform. A player-bound definition has no static
transform: LevelBootstrapV1 chooses its initial spawn and the deterministic
player simulation remains authoritative for its live world pose. Local player
slots are unique.

The runtime resolves a player actor only through neutral relationships:

```text
local player slot
  -> stable authored entity ID
  -> actor model semantic key
  -> actor rig semantic key
```

Missing, partial, duplicate, or dangling relationships are errors. The runtime
also checks that render instance IDs exist, that each instance has at most one
entity owner, and that a static instance's affine world transform agrees with
its entity transform. It checks that the scene level matches the package and
that player actor transforms are usable by the pose path before handing the
level to gameplay.

## Canonical form and validation

Encoding is field-by-field little-endian. Every component table is strictly
ordered by authored ID; strings follow the canonical semantic-key grammar.
Canonicalization normalizes signed zero and quaternion sign but does not repair
an invalid rotation or silently invent a component.

Validation requires known flags, valid definition references, finite
transforms, non-zero scale, unit quaternions within the schema tolerance,
unique player slots, legal component cardinality, canonical table ordering,
exact record/string partitions, no trailing bytes, and all explicit caller
limits.

Authored IDs remain stable within the level while model and archetype
relationships use semantic keys. This makes the schema suitable for later
planets and explicit mod overlays without exposing compiler-local table indices
or RAC-specific class behavior to the runtime.

## Current compiler output

The current compiler emits one initially enabled player definition for each
supported level. Authored ID 0 uses archetype `openrc.player/default`, player
slot 0, and actor model `actors/ratchet/high`. The model-to-entity transform is
identity; live placement comes from player simulation.

The earlier six-resource compiler path adds one initially enabled definition,
authored transform, and static render binding for each matching Bolt placement.
Its stable authored ID is the zero-based ordinal in the complete source
static-Moby table. Bolt behavior is not encoded here: the separate
[GameplaySceneV1](GAMEPLAY_SCENE_V1.md) resource references the same authored
ID and assigns the overlap and semantic inventory grant. The Bolt model itself
is baked in bind pose into `RenderSceneV1`, so these definitions do not require
an actor binding or expose a RAC class ID to the runtime.

The current seven-resource compiler path also emits definitions, transforms,
and render bindings for compiler-recognized Bolt Crates. The separate
[DestructibleSceneV1](DESTRUCTIBLE_SCENE_V1.md) resource owns their health,
damage channels, hit volumes, and drops; EntitySceneV1 remains only the shared
identity and structural-binding layer. The seven-resource crate path now has
fresh all-level preparation/reuse, package-only Veldin gameplay, and graphical
visible-to-destroyed crate evidence. Enemies, weapons,
scripts, save persistence, menus, and reconstructed original camera behavior
are not implemented merely because these first interactive entities are
packaged.
