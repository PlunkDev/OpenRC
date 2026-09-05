# PreparedGameV2 and LevelPackageV1

This document specifies the first versioned, planet-agnostic prepared-data
boundary for OpenRC. It is deliberately independent of the current D3D viewer,
the collision parser, and any one level. The existing JSON
`openrc-prepared-game` format version 1 remains valid and unchanged.

## Goals

- derive all copyrighted data locally from a user's supported disc image;
- produce deterministic, content-addressable bytes;
- keep the native runtime independent of raw PS2 container layouts;
- retain enough provenance to reproduce or audit every derived resource;
- permit strictly ordered mod layers without modifying the immutable base;
- reject corrupt, non-canonical, ambiguous, or unbounded input.

Both formats are little-endian. Integers have their C++ fixed-width size.
Strings are a `u32` byte length followed by canonical ASCII bytes and no NUL.
Digests are raw 32-byte SHA-256 values. There is no implicit alignment or
trailing padding.

## Common 64-byte header

| Offset | Size | Meaning |
| ---: | ---: | --- |
| `0x00` | 8 | format magic |
| `0x08` | 4 | container format version |
| `0x0c` | 4 | header size, exactly 64 |
| `0x10` | 8 | exact total file size |
| `0x18` | 4 | primary record count |
| `0x1c` | 4 | secondary record count |
| `0x20` | 32 | SHA-256 of every byte after the header |

The reader verifies the body digest before allocating variable records and
then verifies every nested resource digest while parsing.

## PreparedGameV2

Magic is `ORPREP2\0`, container version is 2. The primary count is the number
of level-package references; the secondary count is the number of optional mod
manifest references.

The body contains, in order:

1. `u32 content_api_version` and one zero `u32`;
2. strings `game_id`, `build_id`, `compiler_id`, `compiler_version`;
3. source image byte size and SHA-256;
4. a one-byte V1-manifest presence flag, seven zero bytes, and a 32-byte
   digest (zero when absent);
5. level references in strictly increasing `level_id` order;
6. overlay references in increasing `(priority, overlay_id)` order.

A level reference is:

```text
u32 level_id
u32 reserved_zero
string package_path
u64 package_bytes
sha256 package_sha256
```

Paths are lower-case forward-slash relative identifiers. Empty components,
`.` and `..`, backslashes, drive prefixes, and case aliases are impossible in
the canonical grammar. Package paths and level IDs are unique.

An optional overlay-manifest reference is:

```text
i32 priority
u32 content_api_version
string overlay_id
string manifest_path
u64 manifest_bytes
sha256 manifest_sha256
sha256 required_base_game_sha256
```

The required base digest identifies a PreparedGameV2 manifest with no attached
overlays. A launch profile may reference explicit overlays without altering
any base package. Every overlay in one profile must target that same digest.
The core format does not search arbitrary directories or silently discover
mods.

The V1 manifest digest is an optional compatibility bridge. PreparedGameV2 can
be emitted beside the existing V1 content-addressed directory and prove which
canonical V1 preparation it extends. Absence never invalidates V1 or changes
its existing layout.

## LevelPackageV1

Magic is `ORLVLPK1`, container version is 1. The primary count is the number of
resources; the secondary count is their aggregate provenance-record count.

The body begins with:

```text
u32 level_id
u32 content_api_version
u32 layer_kind             # 0 base, 1 overlay
i32 priority
u32 reserved_zero
string build_id
string layer_id
sha256 required_base_package_sha256
```

A base package uses layer ID `base`, priority zero, a zero required-base
digest, and only upsert operations. An overlay uses a different canonical ID,
a non-negative priority, and the exact SHA-256 of the canonical base package.

Resources follow in strictly increasing `resource_id` order:

```text
string resource_id
string type_id
u32 schema_version
u32 operation              # 0 upsert, 1 remove
u32 flags
u32 provenance_count
u64 payload_bytes
sha256 payload_sha256
provenance[provenance_count]
byte payload[payload_bytes]
```

`type_id` is open-ended and independently versioned, for example
`openrc.render-scene`, `openrc.collision-world`, `openrc.actor-library`,
`openrc.entity-scene`, `openrc.gameplay-scene`, or a mod-owned namespace. This
allows neutral resource schemas to evolve without changing the package
container.

Known flags are:

- bit 0: an overlay may replace this visible resource;
- bit 1: an overlay may remove this visible resource.

Unknown flags are rejected. A remove operation has no payload and no flags,
but repeats the target's type and schema version to prevent an accidental
cross-type deletion.

Each provenance record is:

```text
u32 kind                   # ISO, prepared resource, generated, mod resource
u32 reserved_zero
string source_locator      # stable logical locator, never a host path
u64 source_offset
u64 source_bytes
sha256 source_sha256
```

Provenance is sorted by kind, locator, range, and digest. Direct source ranges
must be non-empty and hashed. A generated compiler-pass record may name an
empty range with a zero digest. Every overlay operation must include at least
one `mod_resource` provenance record.

## Current native-game resource profile

The current all-level asset compiler emits exactly eight
upsert resources in every base `LevelPackageV1`:

| Resource ID | Type ID | Schema | Runtime role |
| --- | --- | ---: | --- |
| `actors/animations` | `openrc.actor-animation-bank` | 1 | semantic joint-animation clips bound to exact rig content |
| `actors/library` | `openrc.actor-library` | 1 | semantic rigs and render-ready skinned actor models |
| `world/bootstrap` | `openrc.level-bootstrap` | 1 | authored spawn points and absolute death plane |
| `world/collision` | `openrc.collision-world` | 1 | exact-Q6 native collision geometry and search grid |
| `world/destructibles` | `openrc.destructible-scene` | 1 | neutral health, hit volumes, damage channels, and ordered item drops |
| `world/entities` | `openrc.entity-scene` | 1 | stable entity definitions and typed component bindings |
| `world/gameplay` | `openrc.gameplay-scene` | 1 | deterministic overlap collectibles and semantic inventory grants |
| `world/render-scene` | `openrc.render-scene` | 1 | static textures, materials, meshes, and instances |

This is an asset-compiler profile, not a special Veldin container version. The
target profile uses the same resource IDs and neutral schemas for all 19
supported level packages. LevelPackageV1 still permits other independently
versioned resource types and explicit overlays.

The runtime compatibility loader requires bootstrap, collision, and render
scene. It accepts both actor library and entity scene as one feature pair so
older three-resource development packages remain readable; a package exposing
only one half is rejected. Actor animations, gameplay, and destructible scenes
are optional compatibility extensions for older or independently authored
packages. An animation bank requires an actor library and every clip must match
an exact semantic rig key, canonical rig-content digest, and joint count;
gameplay and destructible scenes each require entity scene whenever present.
The combined loader also cross-validates the level ID, every semantic
entity-to-model-to-rig relationship, and every collectible or destructible
reference to an entity definition with an authored transform before gameplay
receives the content. New native-game publications require all eight resources.
Their compiler ID and profile version differ from the earlier seven-resource
cache, so exact current-profile validation rejects old data rather than
silently mounting a mixture. Fresh preparation, exact validation of all 19
supported levels, and a second reuse pass now verify the eight-resource profile
on the reference input; package-only and graphical Veldin destruction smokes
pass against that same publication.

RAC class IDs, WAD and ELF offsets, VIF/VU/GIF/GS commands, and PS2 texture
layouts are compiler-only inputs. They are not fields in these eight runtime
resources. Cross-resource references use semantic keys, while dense numeric IDs
remain local to a canonical resource table. This lets later planets and mod
overlays reuse or replace actor, entity, and gameplay data without embedding
source-format dispatch in the native runtime. See
[ActorLibraryV1](ACTOR_LIBRARY_V1.md), [EntitySceneV1](ENTITY_SCENE_V1.md),
[GameplaySceneV1](GAMEPLAY_SCENE_V1.md), and
[DestructibleSceneV1](DESTRUCTIBLE_SCENE_V1.md) for those public contracts.

### Current player-animation adapter policy

The compiler publishes every occupied per-level Ratchet sequence under
`actors/animations`, addressed by the numeric key
`actors/ratchet/source-sequence/NNN`. Veldin has 134 such clips; the exact
native profile records and validates the build-specific count for all 19
levels. Source offsets and packed records still stop at the compiler boundary,
but slot identity remains explicit so the original player state machine can be
connected without first guessing semantic names.

Each clip records the exact `actors/ratchet/rig` content digest, 111 poses per
frame, and its integer 50 Hz PAL source cadence. The temporary presentation
path selects slots 0, 3, and 4; that is a preview compatibility policy, not a
final idle/walk/run classification. Playback advances those source
updates on the 60 Hz fixed gameplay clock with an integer accumulator, samples
shortest-hemisphere quaternion interpolation, and composes hierarchy plus
inverse bind. Authored zero scales remain finite singular palettes and are
rendered through the position-only skinning path; no epsilon repair or invented
normal transform is serialized. Airborne gameplay holds the last grounded pose
until jump/fall/landing mappings are proven. The wrench action likewise does
not select an unverified clip.

### Current collectible adapter policy

The supported RAC1 adapter is compiler-only. It interprets static Moby class 13
as the Bolt collectible using high-confidence community metadata, maps the
complete placement-table ordinal to a stable neutral authored ID, and emits
matching `world/entities`, `world/render-scene`, and `world/gameplay` records.
The Bolt high-LOD model is frozen in bind pose and baked into ordinary static
render instances; no RAC class ID reaches the runtime.

Every matching record currently grants `amount = 1` under semantic key
`openrc.currency/bolts`. That amount is an explicit OpenRC policy, not a value
claimed to have been recovered from a source placement. The overlap sphere is
derived from the model bounds and transformed through the neutral entity
transform. Original animation, effects, sound, and pickup timing remain outside
this V1 policy.

### Current destructible adapter policy

The same compiler boundary maps static Moby class 500 to neutral Bolt Crates.
It compiles the model once into shared render data and emits one entity,
render-instance binding, and `DestructibleSceneV1` definition per placement.
The current policy assigns one health and one `openrc.currency/bolts` drop;
those values are not claimed as recovered PVar semantics. `F` and the left
mouse button map to the runtime's neutral primary-action input, whose current
fixed-tick melee profile can damage these definitions without class-specific
dispatch.

The generic compatibility loader may still mount an older package with no
destructible scene. That compatibility is not permission to reuse it as a
current native-game installation: the Launcher validates the exact
eight-resource profile and rejects stale compiler/profile identities before
Play. The next Prepare operation rebuilds the same local cache from the
selected supported disc; it does not install or launch a second client.

## Determinism and overlays

The writers accept ordinary in-memory order but serialize levels, manifest
overlays, resources, and provenance canonically. Readers reject a different
order, duplicate identifiers or provenance, non-canonical paths, unknown enum
values, stale hashes, and trailing data.

`resolve_level_package_v1` validates every package, binds each overlay to the
same base digest/build/level, sorts layers by `(priority, layer_id)`, and then
applies resource operations. A replacement must retain the visible resource's
type and schema version. Later canonical layers may affect resources introduced
by earlier ones, subject to the flags on the currently visible resource.

The resolved result records the base digest and every applied layer digest.
It is an in-memory view; the immutable base and overlay bytes remain unchanged.

## Limits and trust boundary

Every public reader and writer requires explicit caller limits for:

- total input/output bytes;
- level, overlay, resource, and provenance counts;
- individual string and payload sizes;
- aggregate referenced-package, overlay-manifest, and payload sizes;
- the number of overlays applied in one resolution.

Counts in the fixed header are checked before reserve/allocation. Aggregate
arithmetic is overflow-checked. A valid SHA-256 never relaxes a structural or
allocation limit; hashes establish identity and integrity, not trust.

## Intended preparation layout

An asset compiler may place the new files below the current content-addressed
prepared-game directory without changing V1, for example:

```text
manifest.json                 # existing PreparedGameV1
prepared-v2.orpg              # PreparedGameV2
levels/000.orlvl              # LevelPackageV1
levels/001.orlvl
...
```

No level coordinate, Veldin-specific branch, or class behavior belongs in
these containers. Game/build profiles assign global semantics; each level
package supplies only versioned data resources. A future mod manifest format
can reference LevelPackageV1 overlay files through the already reserved
PreparedGameV2 overlay records.
