# ActorLibraryV1

`ActorLibraryV1` is the versioned, source-independent actor asset contract used
by OpenRC packages. Its LevelPackageV1 identity is:

| Field | Value |
| --- | --- |
| resource ID | `actors/library` |
| type ID | `openrc.actor-library` |
| schema version | 1 |

The resource contains render-ready PC data, not RAC model records. WAD offsets,
RAC class IDs, VIF packets, GS texture layouts, and other PS2 serialization
details terminate in the asset compiler.

## Data model

An actor library owns two canonical tables:

- rigs, identified by a library-local dense ID and a stable semantic key;
- models, identified the same way and linked to a rig by semantic key.

A rig contains a parent-first joint hierarchy. Exactly one joint is the root;
every other joint references an earlier joint. Each joint stores a general
row-major 3x4 affine local bind transform and inverse bind transform. Scale and
shear are representable and must not be silently discarded.

A model owns its textures, materials, skinned meshes, triangle topology, and
complete draw-range partition. Vertices are in model bind space and retain
position, normal, UV, color, and up to three joint influences. Influence
weights keep their exact integer numerators and source sum, preserving the
difference between encodings whose totals are 255 and 256.

Semantic keys are the cross-resource identity. Dense IDs are only canonical
indices inside one library and must not be persisted as process-global class
IDs. This allows a later planet or explicit overlay to replace a model without
rewriting every entity scene that refers to it.

## Pose and rendering semantics

For each joint, the native pose path computes:

```text
global_current = parent_global * local_current
skin_transform = global_current * inverse_bind
```

Using the authored local bind transforms as `local_current` therefore produces
an identity skin palette within floating-point precision. The CPU skinning path
blends positions with the exact normalized integer weights, transforms normals
through the corresponding inverse-transpose matrices, then applies the entity's
model-to-world transform. Mesh topology and material ranges remain immutable.

ActorLibraryV1 deliberately contains no animation clips or playback state.
Those require a separately versioned neutral contract; source animation tables
must not be copied into this resource merely to make the current bind pose move.

## Canonical form and validation

Encoding is field-by-field little-endian and never copies a native C++ object
layout. Canonicalization sorts ID-bearing tables, normalizes finite signed zero,
orders active skin influences, computes missing content digests, and then
validates the complete library under explicit caller limits.

Validation includes hierarchy shape, finite and non-singular transforms, unique
semantic keys, valid rig/material/texture/joint references, exact skin-weight
sums, complete triangle draw partitions, texture dimensions and byte counts,
canonical ordering, current content digests, and aggregate allocation bounds.
The model digest incorporates the referenced rig content digest, so changing a
rig invalidates models compiled against it even though the relationship is
named by a semantic key.

## Current compiler output

The current native-game compiler emits one Ratchet rig and one textured
high-LOD model in every supported level package, using semantic keys
`actors/ratchet/rig` and `actors/ratchet/high`. The renderer resolves that model
through EntitySceneV1 and CPU-skins the bind pose at the deterministic player
transform.

This proves the reusable actor boundary; it does not complete character
presentation. Animation decoding/playback, state selection and blending,
attachments, metal/bangle passes, effects, and fidelity to the original camera
remain open work.
