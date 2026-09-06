# SessionStateV1

`SessionStateV1` owns neutral persistent bytes for one game session. Level
replacement does not recreate it. This is a storage and validation contract,
not an implementation of RAC progression, enemy AI, checkpoint rules, or a
disk save format. The native prepared-game profile still has eight resources;
the Launcher/compiler publication path does not yet supply this artifact.

## Canonical storage and views

A schema declares an exact identity, keyed byte buffers, and keyed unsigned
little-endian `u8`, `u16`, or `u32` views. Each view identifies its buffer,
offset, count, and stride. Keys are opaque, case-sensitive, non-whitespace
printable ASCII. The runtime does not interpret source addresses or the
spelling of compiler-generated keys.

Views may overlap, be unaligned, or use a stride smaller than their scalar
width. They always read the same canonical bytes; there are no independently
decoded caches to become stale. Copies own independent storage and retain
their view bindings without pointers into the original object.

An initial image must provide every declared buffer in full, including bytes
outside all views. Missing images and short images are errors, never implied
zeros. Buffer declarations, view declarations, and initial images are
canonicalized by exact key order. Duplicate keys are rejected.

Callers must supply positive allocation limits for buffer/view counts, key
lengths and aggregates, storage sizes, view element counts and aggregates, and
batch writes. Bounds arithmetic is checked before indexing or internal image
copies. Limits do not authorize a view outside its declared buffer.

## Mutations and restoration

A write batch specifies an expected revision and exact typed values. Every
write is validated before any bytes change. Valid writes commit in caller
order, so the last overlapping write wins. The revision increases once per
nonempty successful batch; an empty batch still validates the revision.
Unknown views, wrong types, out-of-range indices, excess high bits, stale
revisions, or revision overflow leave the whole state unchanged.

A snapshot contains the schema identity and digest, revision, and complete
canonical buffer images. Restoration requires the externally trusted schema
and limits. It validates the entire snapshot before replacement and never
replays initial values over restored bytes. Schema, initial-content, and
runtime-state hashes use separate SHA-256 domains; the state hash includes
the revision and all stored bytes, even those outside views.

`GameSessionV1` optionally owns this state. Omitting the initial contract
leaves it absent, and writes then fail explicitly. Its snapshot includes the
persistent bytes, and a snapshot containing those bytes cannot be restored
through the schema-less constructor. Loading or unloading a `WorldV1` does
not reset them. A level-request reason is not itself a progression reset;
new initial state requires an explicitly new session.

`RuntimeGameplaySessionV1` accepts the initial contract and limits once in
its options. Existing transactional frame and level staging preserves the
owned state, including transitions with or without optional entity content.
Writes through the public runtime API are rejected while a frame is being
processed, preventing writes to the committed session from being silently
lost when the staged frame commits. Nested frame advancement is also rejected.
Movement mappers remain pure transforms, not gameplay mutation callbacks.

## Initial-artifact binary I/O

The `ORSSINIT` version-1 artifact encodes one schema and its complete initial
image. It is deliberately not a serialized runtime snapshot or a user save.
All fields use explicit little-endian encoding, with a `0xc0`-byte header,
48-byte buffer records, 64-byte view records, canonical key strings, and
exactly one data image per buffer. Views reference buffer indices rather than
duplicating aliased bytes. The header carries schema and initial-data digests.

Encoding checks output bounds before canonical copies. Decoding checks counts,
aggregates, every record, key budgets, and exact section partitions before
allocating result containers. Noncanonical ordering, missing storage,
nonzero reserved fields, trailing data, invalid references, or mismatching
digests fail. Logical key budgets include references and image labels even
when those labels are represented by indices on disk.

## Compiler-side RAC template bridge

`compile_rac_initial_progress_v1` converts the already validated
[original template](RAC_MOBY_ADMISSION_V1.md) into two neutral buffers and
101 views. One buffer preserves the encoded source-level word. The other
preserves all 20 selector, primary-bit, and registration rows. The registration
key, adjacent halfword, and complete word are views of the same bytes.

Every recovered value is copied, including the encoded level `-1`. The
compiler does not perform the reset wrapper's separate level-0 store, invent
the selector-cache prefix, initialize suppression/alternate state, or infer
live new-game values from overlay file defaults.

Synthetic integration tests populate every row with nonzero values, pass the
result through binary I/O into `GameSessionV1`, check every source field and
registration alias, mutate state, transition through levels 0, 1, and 0, and
restore the exact snapshot with its trusted schema. A local supported-ISO
probe repeats the complete field comparison and lifecycle roundtrip against
the actual template. Neither test executes original constructors or proves
the live first-admission state.

The Release portable build passes all 117 tests and PE import audits. The
published CLI also passes package-only level smoke checks for levels 0 and 1
with the unchanged eight-resource profile. Those checks guard existing
package behavior; they do not claim that the native frontend uses the new
initial-state artifact yet.

## Remaining integration boundary

Source admission still needs a neutral level-load policy, the complete
authored placement catalog, and explicit ordered state operations. The
source copies persistent selectors to a separate cache on one loader path
but skips that copy on another; a level replacement must not automatically
invent a cache refresh. Accepted construction must remain interleaved with
later admission steps wherever the source requires it.

This store does not yet connect behavior `session_shared` random streams,
publish a new native resource, synchronize existing item totals into RAC
progress, or reconstruct disk saves. These require their own established
source contracts; none is implied by persistent-byte storage.
