# RAC placement admission and initial progress

These are **compiler-side source-analysis components**, not a running enemy
program or a replacement game-state system. The native package profile remains
at eight resources. Runtime code does not consume RAC placement fields, PS2
addresses, or the PS2D save envelope.

## Placement inputs and one source step

`RacGameplayMobyAdmissionV1` preserves the placement record's signed selector,
policy word, signed key, and two signed count inputs from offsets
`+0x04..+0x17`. The structural gameplay parser does not discard unknown policy
bits, normalize unused negative indices, or assign unproven health/reward
semantics to the count fields.

`evaluate_rac_moby_admission_v1` reproduces the bounded scalar admission step
recovered from the supported Veldin loader (`0x243210..0x2433d8`), including
its registration helper (`0x286068..0x2860d0`). Inputs are explicit current
source-table values for a caller-selected level; missing input is not zero.
Only accesses actually reached by the source path require supplied values.

| Source operation | Preserved behavior |
| --- | --- |
| Zero policy | Bypass all table reads; retain auxiliary byte `0xfe` |
| Registration policy | Scan 64 slots backwards; the first matching **or empty** key wins; preserve the adjacent halfword |
| Suppression | Reject after the optional registration write |
| Selector policies | Choose the primary/alternate path using the exact `0xff` comparison and required policy bit |
| Bit gates | Preserve branch priority, including alternate-bit priority over primary-bit rejection |
| Count transform | Wrap the increment to 32 bits, apply the source sign adjustment and arithmetic shift, then truncate constructor fields |

An ordinary source rejection still commits an earlier registration write.
Missing, malformed, or unsupported reached input throws without committing
that staged write. Results expose the decision, intermediate count bits,
registration write, and (only when admitted) planned scalar constructor
fields. The evaluator does not execute the constructor or write a live actor.

This is not an arbitrary-address memory emulator. Reached selector reads are
limited to the 16-byte cache plus its explicitly supplied index `-1` prefix;
reached bit reads use the selected 64-word row. Caller allocation limits cannot
extend those supported domains. This prevents independent input vectors from
silently misrepresenting a same-step read that aliases the registration write.
The real Veldin data reaches selector `-1` in six placements: that byte must
come from the source cache's actual preceding memory, not an invented zero or
the preceding persistent level row.

The caller must provide source-correct current inputs on **every step**,
including refreshing read views affected by earlier operations. The source
loader interleaves admission, accepted construction, and the next placement;
running admission over a whole bank before any constructors is not established
as equivalent. This API does not maintain aliases between separate vectors or
recover the entire intervening write graph.

## Initial-progress template

`parse_rac_initial_progress_template_v1` reads the full PS2D sector envelope
used by the source reset routine. It validates the structural envelope, the
complete 47-tag primary and 11-tag repeated descriptor layouts, all 20 repeated
rows, exact payload sizes, and every original record checksum. Descriptor
order is not imposed on input tags: the original reader searches by tag.
Missing, duplicate, unknown, wrong-sized, corrupt, or over-limit data fails.

Only the following established fields are exposed:

- Primary tag `0`: encoded source level, preserved as signed data.
- Repeated tag `0xbbc`: 16 selector bytes per level row.
- Repeated tag `0xbbd`: 64 little-endian primary bit words per row.
- Repeated tag `0xbbe`: 64 signed key/unsigned auxiliary-halfword pairs per row.

The supported disc template encodes level **-1**. The source reset wrapper
separately writes level **0**; decoding must not collapse those two operations.
The three extracted table families contain zero bytes in all 20 source rows,
verified against the actual payload, not inferred from empty memory. Tests use
nonzero synthetic data in every row to ensure the parser does not hardcode
those observed values.

The boot-image reset and Veldin-overlay reset use equivalent descriptor tables
and copy/checksum contracts. This establishes the template interpretation, not
the complete post-reset state at first placement admission. Suppression,
alternate-bit/checkpoint state, and the byte before the selector cache are not
supplied by these extracted template fields. Their producers and subsequent
mutations must be accounted for separately.

The compiler bridge now preserves these decoded fields in a bounded neutral
[session-state artifact](SESSION_STATE_V1.md). Key, auxiliary-halfword, and
word views share canonical registration bytes. The artifact survives binary
roundtrip, session ownership, level replacement, and schema-bound snapshot
restoration without reapplying initial values. It does not provide the
remaining live inputs or execute the source reset wrapper.

## Verification scope

The local supported-disc corpus check independently compares all five decoded
placement inputs against raw records in 38 gameplay banks: 32,464 records in
total. Conditional source selectors range from -1 to 12; nonzero-policy keys
are nonnegative and at most 2046. This verifies decoding and the observed
input domain across levels, not execution of every level's admission policy.
The actual template also passes the complete schema/checksum decoder and
agrees with an independent payload audit.

An explicit Veldin file/template-state experiment evaluates all 296 placement
records using template row 0 and the separately supplied bytes from the exact
overlay file. In that experiment all records are admitted, without registration
writes or count transforms. **This is not a live new-game oracle**: constructors
are not executed, and the complete write history between reset and admission
has not been established. It must not be used to hardcode an all-live roster.

Synthetic regressions separately exercise policy precedence, count arithmetic
edges, all registration scan cases, writes surviving rejection, missing-input
rollback, supported index boundaries, rejected same-step aliases, and
caller-refreshed cross-step aliases. Template tests cover every extracted byte
in all rows, reordered tags, malformed layouts, checksums, and allocation limits.
The supported-ISO compiler probe additionally checks every decoded template
field after neutral binary roundtrip and session creation, preserves state
through levels 0, 1, and 0, and restores the exact snapshot. These are storage
and mapping checks, not evidence of live AI or first-admission state.

## Integration still required

The eventual neutral level-load policy must run over the complete authored
placement catalog, using session-owned persistent state, before granting live
entity capabilities. Omitted placements need explicit absent live identities;
visibility is not a substitute for absence. Surviving group members and links
must use the actual source-to-live mapping. This policy must not require a
render model, rig, or normal-update callback merely to decide whether a
placement exists.

The session-state owner, initial-artifact bridge, and source-to-neutral
[single-placement admission compiler/executor](PLACEMENT_ADMISSION_V1.md)
are implemented. A plan survives binary roundtrip and executes against the
owned current bytes, preserving registration aliases and ordinary-rejection
writes. This is not yet the complete ordered load policy or native frontend
publication. In particular, persistent selector rows and the source's cached
selector bytes have different lifetimes: the initial loader copies the row
while an in-place reload path skips that copy.
Those are explicit source operations, not an automatic refresh on every load.

These components deliberately do not mark original initialization, subsequent
animation selection, enemy AI, or new-game/save/checkpoint integration complete.
The existing [behavior foundation](ACTOR_BEHAVIOR_SCENE_V1.md) remains a
separate integration boundary.
