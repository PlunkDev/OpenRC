# Neutral state installation v1

`StateInstallationV1` applies an ordered prepared batch to an existing
`SessionStateV1`. It carries a level ID, the exact destination schema digest
and typed view/index/value writes. It contains no source addresses, executable
bytes, implicit fill operation, new session or gameplay admission.

The executor checks the level, schema, expected revision and complete batch
before changing state. It uses the existing atomic `apply_batch`: duplicate
and aliased writes retain their order, zero writes remain explicit, unrelated
buffers survive and success increments the same state's revision once.
`GameSessionV1::apply_state_installation` preserves the session owner and its
world metadata. Missing persistent state is an error.

## Encoding

All integers are little-endian. The 56-byte header contains magic `ORSTIN01`,
version u32, reserved-zero u32, body length u64 and body SHA-256. The body starts
with level u32, reserved-zero u32, schema SHA-256 and write count u64. Each
write has key length u32, value type u32, index u64, value bits u32,
reserved-zero u32, then the key bytes.

The decoder validates counts, lengths, digest, types, value widths, reserved
fields and exact end of input before allocating the decoded write collection.
Empty batches and zero schema digests are invalid. Default bounds are 64 MiB,
1,048,576 writes and 256 bytes per key. Shape validation alone does not prove
that a named destination view exists; execution checks the actual schema.

## Initial native level integration

Compiler profile `native-eight-resource-v14-level-installation` adds shared
resource `new-game/level-installation`, type `openrc.state-installation`,
schema 1. There are now 58 shared resources; the nineteen eight-resource level
payloads retain their preceding semantics.

The source compiler qualifies the actual initial overlay, reads its seven
installed sections and maps 3,457 bytes into seven existing semantic regions:
the transition request, target level, level-change request, selector cache,
collision query flags, alternate bits and saved local state. The destination
bindings belong to the level phase; overlapping former frontend addresses do
not identify the same semantic fields. In particular, the saved display
selector and all 267 persistent progress owners remain untouched.

Three level-local owners are part of the canonical initial schema, with their
actual boot images: selector cache (17 bytes), alternate bits (256 bytes) and
saved state (3,168 bytes). The schema has 361 buffers, 467 views and 61,131
bytes. Their later installation is an explicit operation, separate from the
frontend's fresh-game reset and later level-entry clears or restores.

Runtime executes `level/admit-prepared-sections` only after completed loading
and retirement of frontend presentation/audio owners. It takes the loaded
neutral content once into durable storage, applies the installation to the
existing canonical session and preserves the final framebuffer. The loaded
content outlives any later animation references. `level/enter` remains a
separate unimplemented consumer; `level_state_installed=1` does not establish
world entry, entity admission or playability. See `HANDOFF.md` for the latest
executed normal-flow checkpoint.

## Verification scope

The state installation tests cover nonzero values, ordered aliases, retained
unrelated state, malformed encodings and atomic level/schema/revision/write
failures. Native profile tests reject missing or reordered installation writes,
incorrect values and the wrong target level.

The original-source comparison in
`local/forensics/frontend-level-installation-compare-v1.log` compares every
installed byte with the actual original resident copier's result. It poisons
the canonical input first and verifies 354 other owners remain unchanged,
including all persistent progress owners, with the same session allocation and
one revision increment. This is component execution with declared prior-return
and kernel-completion fixtures, stopping before original level entry. It is
not a physical-console capture or evidence of playable Veldin.
