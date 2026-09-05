# DestructibleSceneV1

`DestructibleSceneV1` is OpenRC's source-independent contract for objects that
can receive damage, be destroyed, and grant semantic inventory items. It is
stored as the optional `world/destructibles` resource with type
`openrc.destructible-scene` and schema version 1.

The runtime never sees a RAC class ID, PVar layout, WAD offset, or PS2 packet.
Those values stop at a compiler-only adapter. A level package connects the
neutral resources by one stable authored ID:

```text
EntitySceneV1 definition + transform + optional render binding
                         │
                         └── DestructibleSceneV1 definition
                                  ├── local hit sphere
                                  ├── health and accepted damage channels
                                  └── ordered semantic item drops
```

This separation is intentionally usable by every planet and by future original
mods. The renderer only knows that an entity's bound instance became disabled;
the inventory only knows keys such as `openrc.currency/bolts`; combat only emits
a neutral damage pulse. The generic schema permits an intentionally invisible
destructible with no render binding. The exact RAC1 native-game profile is
stricter and requires every compiled Bolt Crate to own a unique bound instance.

## Canonical data and limits

Definitions are strictly ordered by `authored_id`. Drops within one definition
are strictly ordered by `item_key`; duplicate keys are rejected rather than
silently merged. Health, drop amounts, keys, counts, aggregate key bytes, and
encoded bytes all have explicit caller limits. The absolute value of every
local hit-sphere center component and the hit radius also have separate finite
upper bounds, preventing corrupted coordinates from becoming an effectively
global hit volume. Unknown flags and damage channel bits are rejected.

The exact `ORDSTRC1` wire format uses disjoint header, definition, drop, and key
partitions. Decoding requires a canonical body and rejects trailing bytes,
overlap, integer overflow, reserved fields, invalid floating-point values, and
non-canonical keys. Encoding and decoding therefore produce one deterministic
byte representation for one logical scene.

## Runtime behavior

Damage is processed on fixed simulation ticks. A `GameplayDamagePulseV1`
contains an attack sequence, authored source, exactly one channel, damage, and a
world-space capsule. An entity can take damage only once from the same
`attack_sequence + source_authored_id`, even when an attack remains active over
several ticks.

Because V1 defines its local hit volume as a sphere, a referenced destructible
entity must use an exactly uniform positive scale. Rejecting non-uniform scale
keeps the world-space sphere exact instead of silently expanding it into a
false-positive bounding sphere. General affine render transforms remain valid
for entities which do not use `DestructibleSceneV1`.

V1 damage geometry has one shared deterministic world-space domain. Capsule
endpoint and hit-sphere center components are limited to `+/-67,108,864`
(`2^26`) world units, while capsule and transformed hit-sphere radii must be in
the inclusive range `1/64` through `2^26`. The coordinate limit is twice the
full signed-int32 Q6 collision range, and the minimum radius is one Q6 unit.
At the coordinate boundary, binary64 still provides over one million
representable steps across that minimum radius. A transformed destructible
sphere outside this domain rejects the level before any runtime state changes;
combat producers validate both their profile and completed world-space pulse
against the same limits before committing an attack tick.

On the hit that reaches zero health, the runtime emits, in order for that
entity:

1. `entity_damaged`;
2. `entity_destroyed`;
3. one `item_granted` event per canonical drop.

Events from different objects are ordered by authored ID. World removal,
health, enabled/destroyed flags, item totals, and the next tick are staged as
one transaction. Invalid input, allocation failure, or inventory overflow
leaves all of them unchanged.

`PlayerCombatV1` is a separate fixed-tick producer. Its current wrench profile
uses the input edge for `primary_action` and emits a melee capsule from the
post-movement player pose. The Windows frontend maps both `F` and the left mouse
button to that action. The destructible runtime has no wrench-specific branch,
so projectiles, explosives, enemies, switches, and modded attacks can reuse the
same damage boundary later.

## RAC1 Bolt Crate profile

The supported RAC1 compiler profile maps static Moby class 500 to the neutral
archetype `openrc.breakable/bolt-crate`. The model is compiled once into shared
neutral mesh/material/texture data; every source placement becomes an
independently addressable render instance, entity, and destructible. The same
class is excluded from the old flattened Moby geometry pass to prevent a hidden
duplicate from remaining after destruction.

The first profile assigns one health and grants one
`openrc.currency/bolts`. These values are explicit OpenRC policy while the
source PVar semantics remain unproven; they are not presented as recovered game
logic. Source table ordinals provide stable compiler-side authored IDs, but the
runtime only receives those neutral IDs.

## Package and Launcher compatibility

The generic compatibility loader lets packages made before this schema omit
`world/destructibles` and mounts an empty destructible scene. The exact current
native-game profile is deliberately stricter: every level must contain all
seven resources, including a structurally and cross-resource-valid
`world/destructibles`. Its compiler/profile version differs from the old
six-resource cache, so the Launcher rejects stale data before Play and the next
normal Prepare operation rebuilds the same content-addressed local installation
from the user's supported disc. There is still one OpenRC runtime and one
Launcher—the package is data prepared locally, not a second client.

Schema, compiler-adapter, package, replay, and runtime tests cover this path
without copyrighted data. The current seven-resource RAC profile is also
verified on freshly prepared real data across all 19 levels. Reuse and
package-only Veldin smokes pass, and the D3D11 smoke proves a real mounted crate
is submitted while visible, destroyed by the primary attack, grants its drop,
and is not submitted in the following frame.
