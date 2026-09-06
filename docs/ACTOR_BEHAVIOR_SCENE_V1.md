# ActorBehaviorSceneV1

`ActorBehaviorSceneV1` is the source-independent package contract for native
per-instance actor state machines. It is stored as the optional
`world/actor-behaviors` resource with type `openrc.actor-behavior-scene` and
schema version 1.

The resource does not contain R5900 addresses, source class IDs, PVar offsets,
native pointers, or executable bytecode. Those identities end in the
compiler. A prepared level connects neutral data instead:

```text
EntitySceneV1 authored actor
        │
        ├── exact ActorLibraryV1 model + rig content
        ├── exact ActorAnimationBankV1 clip content
        └── ActorBehaviorSceneV1 program + independent instance state
                                      ├── typed fields and entity references
                                      ├── initial state and channel bindings
                                      └── instance/shared random state
```

## Exact program contract

A program has a stable semantic key and a native implementation key. Runtime
dispatch requires the exact tuple of implementation key, implementation ABI,
and program-layout digest. It never derives behavior from a model name, class
number, animation slot, or substring.

The layout digest pins the complete implementation-facing contract: exact
model and rig keys plus content digests, source update frequency, state and
animation-channel counts, typed field layouts, exact animation imports with
clip-content digests and frame counts, and random-algorithm imports.
Program IDs, the program semantic key, instance values, initial states, and
random seeds are data rather than implementation
layout and are excluded from that digest.

Every field has one portable type: boolean, signed or unsigned 32-bit integer,
finite `f32`, finite three-component vector, or an optional authored-entity
reference. An entity-reference field may explicitly require its non-null target
to have a transform, actor binding, or another behavior instance. Other field
types cannot set those relationship bits.
The mounted level supplies a copied, value-only entity-capability index.
The runtime enforces these same references during initialization, field writes,
and snapshot restoration; an absent index permits only null references.

## Per-instance initialization

Instances are ordered by authored entity ID and reference one dense program
ID. Each instance carries a complete field-major initial value image, its own
initial state ID, and its own state words for instance-scoped random imports.
Initial animation bindings are ordered by channel, select an exact imported
clip and source frame, and are available before the first behavior tick.
Channels omitted from this list begin unbound.

Keeping the state and animation selection on the instance is necessary for
source fidelity. Two placements using the same model and native implementation
may enter different states or clips without duplicating the program or adding
a runtime class table.

## Random streams and replay

Random algorithms are imported by an exact semantic algorithm key, ABI, scope,
and state-word count. State words remain opaque to the generic schema. An
instance-scoped import owns independent words in each actor. A level-shared
import resolves one scene stream and therefore preserves consumption order
across actors. The current runtime deliberately rejects `session_shared`
streams until a session-lifetime owner exists; it never silently converts them
to level state.
Instance-scoped imports cannot repeat a stream key within one program. Shared
imports can bind the same stream and consume the same words.

The caller declares the host tick frequency, while each program declares its
source update frequency. An integer accumulator preserves the source cadence:
50 Hz behavior performs five updates across six 60 Hz host ticks, starting with
an empty tick. Frequencies above the host rate are rejected by this runtime.
Every tick requires an explicit ordered list of scheduled authored IDs. Only
selected, due instances run, in the supplied order; state age counts actual
source invocations. Skipped instances retain the host-clock cadence phase but
do not advance state age or random streams. An empty schedule invokes none;
unknown or duplicate IDs reject the tick. Snapshots retain canonical authored-ID
order independently of execution order. The accumulator and host frequency are
included in snapshots and hashes.
A tick stages all field writes, transitions, per-instance and shared random
state, trace counters,
presentation state, and animation/presentation commands. Any callback,
validation, allocation, counter, or journal failure discards the complete
tick. Snapshots include all mutable state and produce a deterministic hash for
replay checks. Presentation visibility does not disable behavior callbacks.
The source scheduler must supply its recovered grouping, sleep, and
update-distance policy; the generic runtime does not infer that order from
authored IDs.

## Recovered actor selection and order

`runtime_actor_schedule.hpp` implements the recovered selection policy using
neutral actor identities. It is separate from behavior execution and from
the canonical ordering of package records and snapshots.

| Pass | Selection | Invocation order |
| --- | --- | --- |
| Initialization | Live slots without the direct-update skip flag | Supplied live-slot scan order |
| Normal update | Eligible direct actors, then live members of activated groups | Stable order buckets; within each bucket, direct scan followed by ascending group ID and source member order |
| Auxiliary list | Selected actors with the auxiliary flag | Direct scan followed by group/member order, before bucket ordering |

The compiler-side gameplay parser retains dense source group IDs, signed list
offsets, ordered authored members, final-member markers, and bounded source
ranges. `compile_rac_actor_activation_groups_v1` converts those groups using
an explicit source-ordinal-to-optional-neutral-entity map. It removes omitted
members, preserves survivor order and empty group slots, and requires unique
surviving entity identities. It neither guesses save/progress eligibility nor
treats a source ordinal as a live slot. Runtime code sees no source halfwords,
addresses, or relocation tables.

The caller supplies candidates in actual live-slot scan order, stopping at
the source end sentinel. An inactive slot is distinct from that sentinel.
Direct selection first checks liveness and the skip flag. A remaining actor
either bypasses the range test or requires an explicit range result for that
update. An eligible grouped actor activates its group instead of immediately
entering an order bucket. The group pass includes every live member of an
activated group, even a sibling whose own direct skip/range check failed.
It does not shortcut a later direct actor's required range evaluation merely
because an earlier sibling already activated their group.

All counts, references, bucket indices, and output growth are bounded.
Missing required range results and duplicate selected actors fail explicitly;
the latter is not silently deduplicated into a different source order. No
distance formula, all-in-range default, or host-float approximation is hidden
inside selection.

The source loader rewrites the shared member payload in place. The compiler
therefore rejects overlapping source list ranges, whose later reads could
observe earlier remaps; it does not pretend independent remapping reproduces
that case. All 38 supported gameplay banks pass group parsing and have
non-overlapping lists, without duplicate membership or placement-group
mismatches. This corpus check validates the source data, not execution of
save-dependent admission or AI.

These functions build lists only. Original callback execution must still
recheck each actor's live state and current animation pre/post flags around
the callback, because earlier callbacks can change them. Initialization also
performs world queries, animation work, and shared-state mutations; a static
post-initialization state/clip pair is not a substitute for those operations.
The current native compiler does not yet publish or consume this schedule
through a complete source-backed behavior program.

## Binary and package boundaries

The canonical little-endian `ORABHVR1` payload uses disjoint program, field,
animation-import, random-import, random-stream, instance, initial-value,
random-word, initial-animation, and string partitions. Every count and
aggregate has an explicit caller limit. Decoding rejects alternate layouts,
overlap, overflow, non-zero reserved data, unknown flags or enum values,
non-canonical floats and keys, stale digests, incomplete state, orphan streams,
and trailing bytes.

The package attachment step requires direct source provenance and appends its
own generated-pass record. It preserves all existing base-package bytes and
adds one replaceable upsert. The runtime resource loader is optional for older
packages, but presence makes resource type, schema, operation, payload digest,
body, and level identity strict.

The combined level mount additionally requires the exact entity, actor-library,
and animation-bank feature set. It validates every program model, rig, and clip
digest; every instance definition, transform, and actor binding; initial frame
bounds; declared entity-reference capabilities; and ownership conflicts with
the automatic destructible runtime. The entity initially-enabled bit is
bridged explicitly into behavior presentation state instead of defaulting all
actors visible.
Fresh-load presentation commands initialize visibility and animation channels.
Snapshot restoration emits no initialization journal, because the presentation
consumer must restore its own playback state without restarting those clips.

## Current native-game status

This document describes the reusable behavior foundation, not a claim that a
RAC actor is already reconstructed. The current published native-game profile
still contains eight resources and keeps its temporary non-player
`/initial/` presentation fallback. It will move to nine resources only when the
first source-backed Veldin behavior program is compiled, registered, executed,
and covered by package-only and graphical regression tests. Publishing an
empty ninth resource would provide no gameplay evidence and would only create
another stale cache profile.

The execution substrate currently exposes fields, random streams, animation,
and visibility commands. Group parsing, explicit remapping, and selection
order are implemented separately. The compiler also preserves placement
admission inputs, evaluates a bounded source admission step with explicit state,
and decodes the original initial-progress template; see
[the admission contract](RAC_MOBY_ADMISSION_V1.md). These do not yet supply a
complete live new-game state or materialize actors in the native runtime.
Session-state and admission integration, exact range-query inputs,
initialization callbacks and animation phases, world transforms and queries,
damage, drops, audio, VFX, and graphical journal consumption remain required
for complete Veldin behavior integration.
