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
also checks that render instance IDs exist, the scene level matches the package,
and player actor transforms are usable by the pose path before handing the
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

EntitySceneV1 currently carries structural bindings only. Interactive props,
pickups, crates, enemies, weapons, scripts, persistence, menus, and reconstructed
original camera behavior are not implemented merely because the player entity
is packaged.
