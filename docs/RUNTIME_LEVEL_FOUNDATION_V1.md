# Runtime level foundation V1

`RuntimeLevelFoundationV1` is the source-independent boundary between a
resolved prepared level package and native gameplay. It has no ISO reader,
RAC-specific parser, filesystem path, or host-platform dependency.

The loader requires exactly one resource at each stable ID:

- `world/collision`, type `openrc.collision-world`, schema `1`
- `world/bootstrap`, type `openrc.level-bootstrap`, schema `1`

Additional resolved resources are allowed. Scene, entity, animation, and audio
loaders can consume them independently as those schemas are added.

## Validation contract

The caller supplies an expected content API version and explicit limits for
both payload decoders. Before decoding a required resource, the loader checks
that it is an `upsert`, has the exact type and schema, fits its byte limit, and
has a non-zero digest matching its payload. Duplicate required IDs are
rejected even if one copy otherwise looks valid.

`decode_collision_world_v1` rebuilds and verifies the collision grid.
`parse_level_bootstrap_v1` validates canonical spawn records, finite values,
the default spawn, and the absolute death plane. The loader additionally
requires the bootstrap level ID to match `ResolvedLevelPackageV1::level_id`
and range-checks every spawn through the Q6 collision conversion. This check
does not quantize or replace authored floating-point spawn coordinates.

`make_runtime_level_player_simulation_v1` combines an authored default or
explicit spawn with runtime-owned controller and fixed-tick policy. It always
takes the death height from the loaded bootstrap, validates the complete
`PlayerSimulationProfileV1`, verifies that the initial capsule fits the Q6
coordinate domain, and then delegates construction to `PlayerSimulationV1`.
The spawn ID becomes the initial checkpoint ID, so replay state is explicit
and deterministic.
