# Neutral audio gain table v1

`AudioGainTableV1` is an explicit finite-state table with state, level-index
and pan-index dimensions. Each cell supplies two signed channel gains and the
next state. Coordinates are plain indices: the caller owns any mapping from
live control values to them. There are no source register interpretations,
pan formulas, phase classes or game-specific families in the evaluator.

Cells use dense order `((state * level_count + level) * pan_count + pan)`.
The resource supplies its initial state and a positive gain denominator.
Gains are bounded by `[-gain_denominator,+gain_denominator]`; the denominator
is at most 2^30, matching the neutral stream control domain. Every next state
must name an admitted state.

`AudioGainTableResourceV1` validates once and owns an immutable shared table.
Each `AudioGainTablePlayerV1` holds its own state. `step(level,pan)` checks both
coordinates before changing that state, returns the selected gain pair, and
allocates no memory. `reset()` restores the supplied initial state. Copying
the resource shares its cells without sharing any player's current state.

The canonical little-endian encoding has magic `ORAGAIN1`, schema 1 and a
64-byte envelope with total byte count, zero reserved fields and SHA-256 of
the body. Its 32-byte shape contains five u32 values (state count, level
count, pan count, initial state, gain denominator), a zero u32 field and a
u64 cell count. Each 12-byte cell stores signed left/right gain bits followed
by its u32 next state. Total bytes are `96 + 12*cell_count`.

Default limits are 16 MiB, 1048576 cells, 256 states and 65536 entries in each
input dimension. Each dimension multiplication is checked before evaluation,
including when callers supply wider limits. Decode checks the exact payload
partition and all counts before allocating cells, then checks every cell's
signed numeric and next-state bounds. Unknown flags, trailing data and bad
digests are rejected.

Tests cover canonical signed encoding, exact limit endpoints, shared resource
lifetime and independent player trajectories, retained state/reset, atomic
coordinate rejection, signed extrema, every truncated prefix, rehashed invalid
shapes/cells and dimensions whose product would exceed uint64. Source gain
qualification and preparation of table families are separate compiler tasks.

All four test groups passed in the CMake-built portable executable after both
post-link and explicit PE import audits. The execution log is
`local/forensics/audio-gain-table-tests.log`.
