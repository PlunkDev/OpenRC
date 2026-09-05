# OpenRC reference build

The first OpenRC reference build is the European PAL release of Ratchet & Clank
(2002). This file records interoperability metadata only; no game data is stored
in the repository.

| Field | Value |
| --- | --- |
| Serial | `SCES-50916` |
| Region | PAL |
| Languages | English, French, German, Spanish, Italian |
| Reported revision | v2.00 |
| ISO volume ID | `RATCHETANDCLANK` |
| ISO size | 4,214,784,000 bytes |
| ISO SHA-256 | `0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260` |
| Boot path | `cdrom0:\SCES_509.16;1` |
| Boot executable size | 1,388,100 bytes |
| Boot executable LBA | 290 |
| Boot executable SHA-256 | `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b` |
| ELF entry point | `0x0012D868` |
| ELF load range | `0x00100080`–`0x0023E730` (end exclusive) |
| ELF headers | 1 program header, 61 section headers, 1 `PT_LOAD` |

The SHA-256 identifies the exact locally tested image. OpenRC must not assume
that every disc with the same serial is byte-identical; later revisions or
pressings may share a serial and require separate executable hashes.

## ISO 9660 inventory

The reference image contains three regular files in its ISO 9660 directory
tree. Hashes below were calculated through the streamed OpenRC reader.

| ISO path | LBA | Size | SHA-256 |
| --- | ---: | ---: | --- |
| `/IOPRP243.IMG;1` | 968 | 264,449 | `bc91fc6ce5f8afc30b9431688b0c02e90249600982401a419d971ec736326b64` |
| `/SCES_509.16;1` | 290 | 1,388,100 | `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b` |
| `/SYSTEM.CNF;1` | 289 | 58 | `010568864c3a99ce6625330ef6abcb0e8fd8c72ebed952c7754094122b387e70` |

Their combined payload is only 1,652,607 bytes. The remaining image space is
not represented as ordinary files in this directory tree (and includes both
game data and filesystem/disc overhead), so extracting ISO records alone is
not enough to reconstruct levels.

## DiscTocV1

The boot executable has no symbol or debug table, but its loader code directly
reveals the hidden disc index:

- LBA 1098–1499 is a 402-sector zero-filled gap after `IOPRP243.IMG`;
- the global TOC starts at LBA 1500 and occupies six sectors;
- its header is `(version = 1, byte_size = 0x2960)`;
- byte range `0x8..0xEFF` contains 479 `(LBA, sector_count)` slots;
- 476 slots are populated and form one continuous chain from LBA 1506 to
  14365; three slots are empty;
- signature probes classify them as 424 `WAD`, 37 `VAGp`, 13 `2FIP`,
  one `PS2D`, and one unclassified code/data blob.

At global-TOC offset `0x28C8` there are 19 level descriptors. Each points to a
five-sector local TOC with the header `(level_id, 0x2434)`; IDs run from 0
through 18. The first four extent records in every local TOC are contiguous
and start immediately after that TOC; for levels 0–17 they end exactly at the
next level's TOC. For
example, level 0 has its TOC at LBA 1,886,014 and its first primary extent is
`(LBA 1,886,019, 6,569 sectors)`. The descriptor's second field is retained as
an uninterpreted auxiliary value until its meaning is proven.

This layout is corroborated by the boot code: it reads six sectors from LBA
1500, copies exactly `0x2960` bytes, indexes the level table with an eight-byte
stride, then reads five sectors and copies `0x2434` bytes for the selected
level.

## Local TOC asset tables

The remainder of each `0x2434`-byte local TOC now has a bounded, neutral data
model:

- offset `0x028` contains 30 optional `(LBA, exact_bytes)` VAG records;
- offset `0x148` contains 11 left-packed music VAG LBAs;
- offset `0x184` contains 15 blocks of `0x250` bytes;
- every block contains six speech-variant VAG LBAs followed by two independent
  arrays of 71 WAD LBAs.

A populated 71-slot WAD array is a left-packed sequence of WAD records followed
by one terminal zero-filled sector; remaining slots are zero. Across all 19
levels, the reference image contains 792 validated VAG references, 4,081 WAD
run records, and 275 terminal zero sectors. These 5,148 unique references form
one continuous occupied-sector chain from LBA 1,544,375 through 1,876,901.

The four primary level extents are intentionally reported without speculative
gameplay names:

- primary extent 0 begins with 16 aligned `(relative_offset, exact_bytes)`
  slots; 11 are populated per level, and six of those are WAD records;
- primary extents 1 and 2 are one sector-padded WAD each;
- primary extent 3 begins with three counts and two zero reserved fields,
  followed by three tables of opaque `(u32, u32)` pairs.

The parser validates VAG and WAD sizes, left-packing, bounds, alignment, exact
sector occupancy, zero gaps and padding, terminal sectors, and count arithmetic.
It preserves unknown fields as opaque data rather than assigning meanings that
have not yet been demonstrated.

## WadV1

The reference build stores compressed records in a wrapper with this confirmed
layout:

- bytes `0x00..0x02` are ASCII `WAD`;
- the unaligned little-endian `u32` at `0x03` is the complete compressed record
  size, including the header;
- bytes `0x07..0x0F` are nine opaque auxiliary bytes;
- the LZ stream occupies `[0x10, total_size)`;
- the surrounding TOC extent contains exactly `ceil(total_size / 2048)`
  sectors and its remaining sector padding is zero.

These invariants hold for all 424 global WAD records and all 38 WAD records in
the second and third primary level extents. The decoder starts with empty
output, supports literal, short, medium, and far match commands, validates
overlapping backreferences byte by byte, and recognizes explicit `0x1000`
stream-alignment markers. The compressed stream has no decoded-size field or
terminal token, so every caller must provide a decoded-output limit.

The implementation was written from an independent behavioral specification
and repository-owned synthetic vectors. No code from GPL-licensed Wrench or
other external format tools is incorporated.

Representative read-only validation vectors:

| Record | Compressed bytes | Decoded bytes | Decoded SHA-256 |
| --- | ---: | ---: | --- |
| Global slot 100, LBA 4149 | 38,978 | 66,592 | `1b4ac293d7e5b0cedad93cbccff4c9c01886d5e93e5a65e1520e419e6701d3e9` |
| Global slot 259, LBA 6875 | 142,490 | 617,296 | `debd9c338684b7c7f14c86c71d8c3295469577c21fabba480a88c4b567b61027` |
| Secondary WAD, LBA 14365 | 331,279 | 668,780 | `bfa898b04e3c056e9c3c7cfa6a0b0a2648b464c8d5a770a965ef37ac5ba6c88e` |

## 2FIP indexed textures

The global catalog contains 353 confirmed 2FIP textures: 13 direct extents and
340 exact decoded WadV1 payloads. Their bounded logical layout is:

- `0x00`: ASCII `2FIP`;
- `0x04`: an opaque little-endian identifier;
- `0x08`/`0x0C`: width and height;
- `0x10`: PS2 PSMT8 value `0x13`;
- `0x14`/`0x18`: zero, and `0x1C`: one;
- `0x20..0x41F`: 256 four-byte RGBA palette entries;
- `0x420..end`: one row-major palette index per pixel.

The exact logical size is `0x420 + width * height`. Direct extents use the
minimum whole-sector envelope and zero padding; decoded WAD payloads end at the
logical boundary. The reference dimensions range from 128x128 through
512x512.

PSMT8 palette lookup exchanges bits 3 and 4 of the pixel index before reading
the stored RGBA entry. Pixels are already ordered left-to-right and
top-to-bottom. Reference alpha entries are either `0x00` or `0x80`; platform
RGBA export maps them to 0 and 255. The original field at `0x04` remains
opaque because duplicate values can identify different bytes and common
content-hash algorithms do not explain it.

Representative exact logical hashes:

| Record | Dimensions | Logical bytes | SHA-256 |
| --- | ---: | ---: | --- |
| Direct slot 0, LBA 1506 | 128x128 | 17,440 | `ebf9cf57e19c290af254a0669747541ab3b6edba6afa2c9472ecfa7879c86ad5` |
| Decoded slot 100, LBA 4149 | 256x256 | 66,592 | `1b4ac293d7e5b0cedad93cbccff4c9c01886d5e93e5a65e1520e419e6701d3e9` |
| Decoded slot 376, LBA 11509 | 512x512 | 263,200 | `f30a7f14be067b9552d88162cb2e2fc7768d21aeb879bf53f5b02a95fcc7b09d` |

## Seven-region boundary tables

Global slots 259-296 form two 19-record series. Each WadV1 output starts with
eight little-endian offsets that divide the exact decoded buffer into seven
bounded regions. For all 38 records:

- the first boundary is `0x20`, immediately after the eight-word table;
- every boundary is `0x10`-aligned and strictly greater than its predecessor;
- the eighth boundary equals the decoded-buffer size exactly.

OpenRC reports only neutral `{offset, size}` regions. The reference corpus
consistently gives region 2 size `0x48000`, region 3 size `0x40000`, and
regions 4-6 size `0x4420`, but these observations are not yet assigned
gameplay meanings or required by the generic parser.

Slot 259 at LBA 6875 decodes to 617,296 bytes with boundaries
`32, 5040, 7920, 302832, 564976, 582416, 599856, 617296` and SHA-256
`debd9c338684b7c7f14c86c71d8c3295469577c21fabba480a88c4b567b61027`.

## MapArtV1

The two boundary-table series at global slots 259-277 and 278-296 correspond
positionally to level IDs 0-18. Each matching pair has identical boundaries.
Regions 0, 1, 4, 5, and 6 are byte-identical between the two series; regions 2
and 3 remain bounded but semantically opaque.

Region 0 begins with little-endian words `256` and `32`, followed by 256
strictly ascending little-endian 16-bit record ends. Record data begins at
relative offset `0x208`; record `i` occupies
`[8 + previous_end, 8 + end[i])`, with the initial previous end equal to
`0x200`. The last logical byte is followed only by the minimum zero padding
needed for a `0x10` boundary.

Regions 4-6 are exact 128x128 2FIP images with no padding. Within each
three-image set they share the complete header and palette while retaining
separate indexed-pixel planes. OpenRC keeps regions 0-3 as owned bytes, reports
the 256 bounded records, and can compose regions 4|5|6 into a 384x128 TGA.
All 38 reference payloads pass this contract.

## PS2D save bundle

Global slot 1 at LBA 1515 occupies 46 sectors. Its first `0x18` bytes contain
three contiguous `(offset, size)` records. Their logical end is `0x16F3C`;
the remaining 196 sector-envelope bytes are zero.

- record 0 is a fixed `0x3C4`-byte `icon.sys` structure with `PS2D` magic,
  bounded title/filename fields, and a zero reserved tail;
- record 1 is the version-`0x00010000` memory-card icon model with 12 owned
  24-byte vertices, a 36-byte animation block, and `0x8000` texture bytes;
- record 2 contains one primary and 20 repeated tagged records. Each record
  declares `record_size - 8`, uses bounded `(key, payload_size, payload)`
  entries with zero alignment padding, and ends at an exact
  `FFFFFFFF/0` sentinel.

Unknown words, vertex fields, texture interpretation, and tag meanings remain
opaque in the public API.

## SceneBlockDirectoryV1

Primary-extent-0 subrange 10 is a WadV1 record on all 19 levels. Its decoded
buffer begins with a neutral fixed-stride descriptor structure:

- header word 0 is `0x40` and word 1 is the complete descriptor count;
- header word 2 is a finite positive float and words 3-15 are zero;
- the first `count - 1` descriptors follow the header, while the final
  descriptor begins at `directory_bytes = count * 0x40` and intentionally
  overlaps the first block's fixed `0x40`-byte prefix;
- each descriptor preserves 16 raw little-endian words; words 0-3 are finite
  floats with a positive fourth value, word 4 is a block offset, and word 14
  is an opaque size;
- offsets and opaque sizes are `0x10`-aligned; the first offset equals the
  overlapped descriptor offset and each following offset is exactly
  `previous_offset + 0x40 + previous_opaque_size`;
- descriptor words 5-7, 11, 12, and 14 encode nine neutral boundaries for the
  opaque remainder. With `lo(x)` and `hi(x)` denoting the two 16-bit halves,
  the ordered boundaries are
  `[lo(w5), hi(w5), lo(w7), lo(w6), hi(w6), hi(w7), lo(w12), hi(w11), w14]`;
- `lo(w5) = 0`, `hi(w11) = hi(w12)`, word 13 is `0xFFFF00FF`, and
  `w14 = hi(w11) + lo(w11) * 0x10`.

OpenRC owns each complete `0x40 + opaque_size` block envelope. It exposes the
first `0x40` bytes and the remainder only as neutral ranges because there is
not yet enough evidence to call either one a header or payload. The overlap
does not create overlapping block envelopes: it stores the final descriptor in
the first envelope's prefix, and that descriptor points to one final envelope
after the preceding chain. The last block still does not end the decoded WAD:
every level has a large, non-empty trailing range, which is retained
independently rather than rejected or discarded.

The nine relative boundaries are all `0x10`-aligned and strictly increasing,
so they partition every opaque remainder into exactly eight non-empty neutral
sections. OpenRC exposes decoded-input ranges into the already owned block
envelope; it does not copy the 46,613,264 section bytes a second time. Words
8-10 and 15 remain opaque.

The complete 19-level corpus contains 160,128 sections:

| Section | Aggregate bytes | Per-block size range |
| ---: | ---: | ---: |
| 0 | 1,816,864 | `0x30`-`0x130` |
| 1 | 960,768 | `0x30` exactly |
| 2 | 12,463,472 | `0xD0`-`0x6A0` |
| 3 | 1,766,976 | `0x30`-`0x130` |
| 4 | 13,786,032 | `0x60`-`0x5B0` |
| 5 | 4,208,464 | `0x10`-`0x170` |
| 6 | 8,574,048 | `0x30`-`0x2E0` |
| 7 | 3,036,640 | `0x10`-`0x420` |

As a separate clean-room check, concatenated sections 0-4 form one exact VIF
stream in each block and total 30,794,112 bytes across the corpus. The
individual section boundaries may split a VIFcode from its payload and must
not be parsed as independent streams. All 20,016 streams end exactly at
`hi(w7)`, with no IRQ bits or unknown commands. The observed instruction set
is NOP, STCYCL, STMOD, STROW, and UNPACK V3-16/V4-32/V4-16/V4-8: 925,997
commands in total, including 318,044 UNPACKs. Their physical payloads occupy
27,090,124 bytes, including 58,146 required four-byte-alignment bytes, all of
which are zero in the complete corpus. Opcode names, VIFcode fields, and
`NUM = 0` behavior follow the
[PS2SDK packet2 VIF types](https://github.com/ps2dev/ps2sdk/blob/master/ee/packet2/include/packet2_types.h)
and its
[VIF construction helpers](https://github.com/ps2dev/ps2sdk/blob/master/ee/packet2/include/packet2_vif.h).

`SceneBlockVifStreamV1` now exposes this as bounded, metadata-only command
records. Code, physical payload, and complete packet ranges are relative to
the supplied stream; logical payload size excludes the separately reported
zero alignment suffix. The parser keeps STCYCL state for UNPACK sizing,
supports the specified `NUM = 0` encoding of 256 output vectors, requires an
exact end at the section-4 boundary, and rejects IRQ, masked or unobserved
opcodes, reserved fields, non-zero padding, truncation, overshoot, and caller
limit violations. It neither copies payload bytes nor retains pointers into
the caller's storage.

`SceneBlockVuSnapshotV1` executes those commands in order into a neutral VU1
data-memory image of 1024 128-bit qwords. It models the reset cycle as `1/1`,
tracks STCYCL, STROW, and all three legal STMOD modes, performs signed or
unsigned component expansion, applies row addition modulo 2^32, and wraps
every destination through the ten-bit VU1 qword address. TOPS is mandatory
caller state rather than a value inferred from the stream. The semantics were
implemented from the
[Emotion Engine User's Manual](https://usermanual.wiki/Pdf/EEUsersManual.1748563585.pdf)
and checked independently against PCSX2's
[VIF command handling](https://github.com/PCSX2/pcsx2/blob/3e29183a37e74cbc8c17bda8afb63c2d9bc6fd14/pcsx2/Vif_Codes.cpp)
and
[UNPACK implementation](https://github.com/PCSX2/pcsx2/blob/3e29183a37e74cbc8c17bda8afb63c2d9bc6fd14/pcsx2/Vif_Unpack.cpp).

The snapshot does not invent values which the evidence cannot establish.
V3-16 uses the same hardware unpack path as V4: W reads the immediately
following 16-bit element, becomes a known zero when that lookahead begins at a
new 128-bit source qword, and remains unknown only when the caller's bounded
input omits required lookahead bytes. STCYCL fill-generated vectors remain
unknown. Every output write preserves its command and output-vector indices,
optional consumed-input index, unwrapped and wrapped destinations, addition
mode, and exact per-lane source range. Final qwords retain their write count and
an index back to the last ordered write, so deliberate overlap remains
auditable without retaining payload pointers.

With explicit diagnostic `TOPS=0`, the complete corpus executes 3,602,759
vector writes into relative qword addresses 0-327. Summed independently per
stream, 3,106,660 qword destinations are unique and 496,099 writes overwrite
an earlier value; every stream contains overlap and every observed overwrite
is V4-8 over V4-8. No write wraps at this diagnostic TOPS. All observed UNPACK
commands request external TOPS, so these addresses are intentionally reported
as relative diagnostics rather than claimed runtime VU1 locations. The older
partial-qword totals based on an always-unknown V3 W model are no longer used
as reference facts; provenance now distinguishes payload, lookahead,
qword-boundary zero, unavailable lookahead, and cycle-fill sources.

`SceneBlockVuCommandPhaseV1` adds a derived, neutral partition over this
ordered snapshot. A phase contains the maximal non-NOP control prefix followed
by UNPACK commands; the first control after data opens the next phase. Its
command and byte ranges form a gapless partition, with NOPs immediately before
that boundary retained by the preceding phase. Each phase owns copied
`state_before`/`state_after` values, one exact contiguous write range, and
sorted disjoint qword runs, so STCYCL holes and ten-bit address wrap are never
collapsed into a misleading min/max interval.

The full corpus forms 254,052 phases, 9-13 per stream. Ignoring NOP placement
leaves five exact whole-stream sequences; collapsing the multiplicity of
adjacent equal UNPACK commands leaves two structural skeletons, used by 18,477
and 1,539 streams. These are regression observations, not public parser
acceptance rules. All observed phases have zero internal overwrite. The
496,099 overwrite writes occur between phases, remain exclusively V4-8 over
V4-8, and are independently recovered by the phase accounting.

The three remainder tail sections remain opaque, but two stronger structural
facts are now established over all 20,016 blocks. In qwords,
`section6_size` is exactly `2 * section5_size` for 10,196 streams and one
qword larger for the other 9,820. The first section-6 qword is also an exact
copy of one ROW payload which occurs twice in that stream: always STROW ordinal
2, then ordinal 4 in the short phase skeleton or ordinal 6 in the long one.
It is never the final ROW. Sections 5 and 7 have no STROW match, and none of
the three tails is a literal copy of a fully known ordered write or final VU
qword. This supports a shared cardinality driver and one ROW template, but not
terrain, vertex, or transform names in the API.

The header count equals the size of primary-extent-3 table 0 on all 19 levels.
The complete sweep validates 20,016 descriptors, 47,894,288 bytes of chained
block envelopes, and 368,974,896 trailing bytes. Declared and parsed counts
both range from 444 to 2,144. The 19 final envelopes add 44,960 bytes.

| Level | Decoded bytes | Count / records | Directory end | Chain end | Raw WadV1 SHA-256 | Decoded SHA-256 |
| ---: | ---: | ---: | ---: | ---: | --- | --- |
| 0 | 16,791,232 | 460 / 460 | `0x7300` | `0x113EE0` | `737fec3eff206fa57125451387feac69946f045643567587097b1f9c0403302d` | `f316516a3aba1d3fc6b75b8eda1fffa13fd67fa911ee09d3026c99a07ad7d957` |
| 18 | 22,661,824 | 1,144 / 1,144 | `0x11E00` | `0x2A6380` | `0221dc2b9e25ad62c3dedcdfd480dc2e7967fac15c8821dd6c750a5471f826ce` | `f26bba8b9a87f133c6e72101e4e8e758409467b80bcbf0c34568e7fd4282bc77` |

The EE executable provides an independent link to the runtime consumer.
Function `0x204918` reads this exact offset/count header, advances to
`base + word0`, walks `count` records at stride `0x40`, and relocates the
word-4 pointer at record offset `0x10`. Render task `0x2352C8` consumes the
same relocated pointer/count pair, iterates the same records, and assembles
their VIF chain. This confirms that the clean-room directory and runtime
records are the same structure rather than merely similar data.

That task uploads DVP overlay group `55907` itself: seven 0x800-byte chunks
at VU VMAs `0x0000` through `0x3000`, followed by one 0x260-byte chunk at
`0x3800`. The upload contains exactly `0x3AA` DMA qwords: eight 8-byte VIF
headers plus `0x3A60` bytes of microcode. Its headers encode FLUSHA/MPG and
successive MPG destinations, while later task code selects MSCAL entries
6, 8, 10, 14, 16, or 20 from record fields. A later MSCAL 2 occurs only
after the task uploads the separate overlay group `903379`; although `55907`
contains a decoded dispatcher at entry 2, this task does not call it there.
Overlay groups `57843` and `13859` are separate render passes.

Before processing records, the task UNPACKs 15 constant qwords to VU RAM
`656..670`, executes entry 0, and configures VIF1 `BASE=0, OFFSET=328`.
Entry 0 establishes the observed control registers and loads qword 664. Each
record then uses an entry-specific selection of its remainder ranges plus a
V4_8 tail from section 5; it is not equivalent to executing the complete
public sections-0-through-4 diagnostic snapshot. TOP/TOPS alternates between
the two 328-qword input banks and state is carried between calls. Exact task
packet reconstruction is therefore required before claiming a SceneBlock
render invocation.

The bounded neutral decoder reads each little-endian eight-byte pair as a
lower word followed by an upper word, retains both raw words, and decodes the
recognized instruction subset required by this overlay. Unknown upper or lower
halves remain explicit. It exposes upper I/E/M/D/T flags, typed operands,
direct and indirect control transfers, VU1-wrapped direct targets, and typed
data-memory, XTOP, and XGKICK accesses. Instruction names, masks, values,
and operand fields are checked against the public GNU binutils
[DVP opcode table](https://github.com/ps2dev/binutils-gdb/blob/dvp-v2.45.1/opcodes/dvp-opc.c).

The real `0x3A60`-byte program contains 1,868 instruction pairs and decodes
without an unknown half. Only four upper words carry a flag, all `E`; the
program has no observed `I`, `M`, `D`, or `T` flag. Its control inventory has
216 direct branches and 23 indirect transfers, all of the latter `JR vi15`.
Eight direct branches cross the linear zero boundary; applying the VU1
11-bit instruction-PC wrap (`target_pair & 0x7FF`) places every direct target
inside the loaded program.

The bounded-functional executor now runs decoded program metadata from an
explicit partially known state. On the real eight-chunk `55907` program, the
microcode-only diagnostic reaches normal E termination from decoded entry 2
after four pairs. Entry 6 advances 20 pairs before stopping at an
indeterminate memory address when started without the task seed and record
packet. That stop is intentional evidence of the missing runtime input, not a
zero-filled guess. The executor models paired pre-state reads, one branch/E
delay pair, STATUS/CLIP latency 4, Q latency 7, queued stores, and bounded
synchronous XGKICK/GIFtag snapshots; full FMAC/load scoreboarding and live
PATH1 transfer remain outside this milestone.

The EE caller at `0x2346C0` constructs a four-qword frame transform and calls
helper `0x234BA0` with destinations 5 and 333 and a count of four. That helper
emits `STCYCL 4/4` plus unsigned V4-32 UNPACK, proving that VU RAM `5..8` and
`333..336` are shared frame inputs for the two 328-qword record banks. Modeling
those uploads removes the last unknown dependency in the tested level-0 record
0, entry-16 path. With a deterministic identity debug transform, the real path
terminates normally after 1,832 instruction pairs, produces two complete
XGKICK packets, and decodes to 80 fully known vertices and 62 emitted
triangle-strip primitives. This is a diagnostic camera input, not the game's
live runtime camera.

The GIF/GS decoder consumes all complete XGKICK events as one ordered stream.
It carries attributes and pending primitive assembly between events, applies
PRIM resets even for repeated values, ignores PRE and other tag fields for
`NLOOP=0`, dispatches A+D on address bits 0..6, and snapshots the selected
XYOFFSET/SCISSOR context on vertices and primitives. It now also decodes and
retains raw/typed TEX0 and CLAMP for both contexts. Both context copies remain
in each vertex and primitive snapshot when PRMODE has not yet selected one,
which is required by the observed tfrag packets that explicitly program only
the `_1` registers. The CLI can rasterize the
emitted known-XY primitives into an auto-fit wireframe TGA. The same decoded
result now feeds the first native D3D11 viewer: the tested level-0, record-0,
entry-16 invocation uploads 80 known vertices and draws 62 wireframe triangles
in a resizable GPU-backed window.

The same invocation now has a separately validated pre-projection source path.
VU RAM qwords `250..269` contain the 80 per-submit descriptor indices. Each
selected descriptor's W lane points to one signed XYZ qword, and the immediately
following qword supplies RGBA. Following that chain produces 80 vertices in GS
order, 73 unique descriptor qwords, and 71 unique position qwords; recovered
RGBA agrees with the decoded GS vertex on all 80 submissions. Position XYZ is
interpreted as signed integer input to the confirmed `ITOF0` path, without an
invented `/16` scale. The public recovery result retains the index qword/lane,
descriptor W lane, position/color qwords, and their last VIF write indices.

The qword-250 address is specific to record 0, not a level-wide constant. Across
all 460 Veldin records, every one of the 263 entry-16 executions that reaches a
normal E termination has exactly one unsigned V4-8 UNPACK whose output-vector
count equals `ceil(GS vertices / 4)`. Its first written qword is the descriptor
index base; the observed bases span qwords 157..293. Following the same
index/descriptor-W/position/adjacent-color chain from that derived base gives
exact RGBA agreement for every submission in all 263 records. Their merged
source geometry contains 22,428 vertices and 18,660 emitted triangles, with
bounds `(94915, 74933, 6144)` through `(238337, 314859, 44553)` in the recovered
signed source coordinates.

Entry 16 decodes complete GS events for 325 records in total, yielding 24,954
raster vertices and 20,782 emitted triangles. The additional 62 records stop on
still-indeterminate runtime memory after a partial captured pass and do not have
a complete exact source-index candidate; 135 records produce no XGKICK event in
this standalone pass. Full-level mode preserves those distinctions rather than
zero-filling or claiming that the raw aggregate is a complete gameplay frame.

With identity diagnostic frame input, the affine stage computes
`q5*x + q6*y + q7*z + q8`, and the later DIV by the resulting W followed by
MULQ confirms that qwords `5..8` are a full homogeneous transform into the
pre-viewport projection path. Recovering the live values uploaded by the EE
caller remains separate work. The native viewer therefore uses an isolated
PC-side Z-up debug orbit for the merged source mesh and keeps the aggregate
decoded GS 2D output behind `Tab`; neither mode is claimed to be the original
gameplay camera or a classified/playable Veldin scene.

The eight chunks form one continuous decoded instruction run. None of the 243
control transfers has a missing delay slot, and no instruction-run boundary,
entrypoint, direct target, or other control transfer lies in one. Under the
context-insensitive CFG contract this partitions the program into 287 basic
blocks and 423 edges. `BAL` contributes a call edge while its decoded
continuation address is kept as metadata rather than added as a speculative
return edge.

Each decoded MSCAL entry is an unconditional dispatcher branch and its single
branch-delay pair executes the identical `XTOP vi14`. The complete program has
12 XTOP sites, all targeting `vi14`, and six XGKICK sites, all sourced from
`vi01`. Its typed memory inventory contains 395 LQ, 94 LQI, 159 ILW, 126 ILWR,
48 SQ, 192 SQI, 16 ISW, and four ISWR instructions. TOP-relative accesses
include integer loads from the header region and vector loads from qwords 5-8.
These are control/access facts only: the decoder does not execute the program,
name geometry, or interpret the emitted GS packets.

The task's common preamble sets `BASE=0`, `OFFSET=328`, and initial `DBF=0`.
The first record therefore exposes TOP/TOPS qword 0; subsequent record calls
alternate the two input banks at qwords 0 and 328 as DBF toggles. The trailing
batch reset restores BASE/OFFSET to `0/0` and STCYCL to `4/4` only after that
sequence. Public diagnostics still take TOP/TOPS explicitly so an arbitrary
call cannot silently assume its position in the carried batch state.
The leading floats and regular packet patterns remain consistent with spatial
scene records, but the parser still does not label individual blocks as
terrain, collision, or models.

## CompanionTerminalWadIndexV1

Primary-extent-0 subrange 2 independently indexes a terminal chain inside the
decoded subrange-10 buffer. Its terminal-index fields begin at offset `0x80`:

- `0x80` is a non-zero record count and `0x84` is a `0x10`-aligned table
  offset at or after `0x90`;
- `0x88` is an owned opaque word and `0x8C` equals the complete decoded
  subrange-10 size;
- the table contains `count` 16-byte records and ends exactly at subrange-2
  EOF; bytes before it are preserved without assigning semantics.

Each record is `(target_offset, opaque_word, logical_size, reserved)`. The
reserved word is zero, the target offset is `0x10`-aligned, and the exact
target range begins with WadV1 whose unaligned size field matches
`logical_size`. Every record after the first begins at the previous logical
end rounded up to `0x40`. Inter-record and final alignment bytes are all zero,
while the large target prefix before the first WAD remains opaque.

Every level contains 21 records. The full sweep validates 399/399 logical
WADs, 8,494,672 logical bytes, and 15,024 padding bytes. Each level contributes
the same 447,088 logical bytes and decodes them to 1,045,232 bytes; its padding
ranges from 768 to 816 bytes. The opaque prefixes before the terminal chain
total 409,640,512 bytes and range from 16,343,344 to 24,283,648 bytes per
level.
Across levels, records at the same ordinal have identical opaque words,
logical sizes, compressed payload hashes, decoded sizes, and decoded hashes.
Only the nine auxiliary WadV1 header bytes vary, so this is a shared asset bank
rather than per-level scene geometry.

The 21 decoded payload hashes are now identified as strict `RacMobyClassV1`
model cores. They account for 21 unique payloads and all 399 companion
observations. The standalone shared-bank probe requires the observed `0xFF`
format byte at class-header offset `0x0B`; local level-core models legitimately
use other values and are validated under their separate owning context.

| Level | Subrange-2 bytes / SHA-256 | Table offset | Opaque header | First WAD | Last WAD | Indexed size |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | `0x7DE0` / `3f048365b73b522a4e7b046478e2cf9d6a3b4445f34e80645ea08e4ea90efbb4` | `0x7C90` | `0x0091489B` | `0xF96130` | `0x1000D80` | `0x10036C0` |
| 18 | `0xA770` / `140edae2f88d5c0231b262a7ee4335722967d10a37955552169af32af4ca2769` | `0xA620` | `0x00CA9C27` | `0x152F550` | `0x159A180` | `0x159CAC0` |

## Decoded WadV1 corpus inventory

The streaming inventory now reaches every currently indexed or structurally
owned WadV1 payload without retaining the corpus bytes. It processes one
decoded payload at a time, hashes it once, runs strict semantic probes only on
the first occurrence of each decoded SHA-256, and retains owned provenance and
classification metadata. The exact PAL v2.00 sweep contains:

| Source | Observations |
| --- | ---: |
| Global TOC WAD slots | 424 |
| WAD immediately following the global extent chain | 1 |
| Local resource-block WAD runs | 4,081 |
| Primary-extent-0 WAD subranges | 114 |
| Primary extents 1 and 2 | 38 |
| Nested records in the global-tail WadBundleV1 | 12 |
| Companion terminal records | 399 |
| **Total** | **5,069** |

Those observations decode to 1,093,912,988 bytes and deduplicate to 4,605
payloads totaling 1,066,576,972 bytes. Strict complete-payload probes produce
4,531 recognized unique payloads, 74 unknown unique payloads, and zero
ambiguous payloads:

| Semantic format | Unique payloads | Observations |
| --- | ---: | ---: |
| `TwoFipV1` | 335 | 340 |
| `MapArtV1` | 36 | 38 |
| `SceneBlockDirectoryV1` | 19 | 19 |
| `WadBundleV1` | 1 | 1 |
| `SceneAnimationBankV1` | 4,081 | 4,081 |
| `RacGameplayBankV1` | 38 | 38 |
| `RacMobyClassV1` | 21 | 399 |

The optional observation TSV is 1,114,480 bytes with SHA-256
`1c7c8ed2f8a141a92eb9c75eaabd64a963bb8c0c6ed52e3160ff72caafd1505e`.
Its nested rows identify the exact parent observation as well as the parent's
deduplicated payload. The production scene-directory probe uses a separate
4096-record allocation cap; the largest reference directory contains 2,144
records. Scene-animation probing has independent limits of 256 actor tracks,
65,536 total frame ranges, and 4,096 subtitle entries.
The gameplay-bank probe is allocation-constant beyond its fixed ranges and
bounded class/instance metadata; the Moby-class probe has its own packet and
input envelopes. Unknown remains an explicit result rather than a guessed
format. The 74 remaining unique payloads occupy 31 candidate families and
continue to provide the queue for the next semantic pass. The matching family
TSV is 10,880 bytes with SHA-256
`34afaf05993ed81dff13cdece5a397f42cecdfa9863dd10a7dfc59d974764e74`.

## RAC1 gameplay instance banks

Primary extents 1 and 2 are the regional gameplay banks for each level. All
38 decoded payloads have the same RAC1 directory contract: 37 little-endian
words occupy bytes `0x00..0x93`, slot `0x90` is zero, bytes `0x94..0x9F` are
zero alignment padding, and every other slot is a distinct 16-byte-aligned
pointer in the decoded envelope. The 36 pointers cover level settings, eight
localized help-message banks, lights, cameras, sounds, moby/tie/shrub classes
and instances, pvars and fixups, paths, spatial volumes, collision/light grids,
and occlusion mappings.

Header slots are not numerically monotonic. `RacGameplayBankV1` validates the
known physical serialization order before deriving disjoint borrowed ranges.
Its semantic anchors are the exact 0x50-byte RAC1 level-settings block, a
non-empty bounded moby class list, and the moby instance block. The latter has
a 0x10-byte header, two zero reserved words, and `static_count` records whose
first word is exactly the proven RAC1 record size `0x78`. The parser exposes
moby-class IDs plus static and spawnable counts. Each static instance owns its
class ID, scale, position, rotation, group/rooting fields, pvar/occlusion/mode
references, and light index. Offset `0x20` is retained as an integer-like raw
word: although external tooling labels it a floating draw distance, all 16,232
PAL records observed here contain small integer bit patterns from 0 through
1,023, so OpenRC does not currently invent floating-point units for it.

For every level, the NTSC and PAL payloads have equal decoded sizes and
identical first `0xA0` bytes. Every regional byte difference lies only in the
pvar-data block selected by header slot `0x58`; OpenRC preserves those bytes as
opaque and does not yet label them as timing constants. The production sweep
matches exactly 38 unique payloads/38 observations and introduces no ambiguous
classification.

## RAC1 level core and Moby model packets

`RacLevelCoreIndexV1` binds raw primary-extent-0 subrange 2 to the encoded and
decoded subrange-10 asset WAD. It validates the fixed `0xBC` header and exact
RAC1 `0x24` trailer, parses 0x20-byte Moby class entries and texture slots,
parses the 0x10-byte shared-gadget table, validates its exact WAD chain, and
derives every local class asset range only from proven neighboring boundaries.
For every level, the resulting class IDs match the regional gameplay bank in
exact count and order.

Across levels 0-18, the index contains 3,645 Moby class entries: 2,972 local
model occurrences, 399 shared-gadget occurrences, and 274 external or zero
model references. The matching gameplay banks contain 16,232 static
placements. All 2,972 local model cores and all 399 shared occurrences parse as
`RacMobyClassV1`, including meshless classes, shared/out-of-order packet
storage, sequence metadata, optional skeleton/common-translation ranges, and
owned high-LOD, low-LOD, metal, and shadow ranges.

The same index preserves Ratchet's fixed 256 sequence slots without collapsing
zero entries or aliases. Across all 19 levels, 1,804 unique non-zero sequence
assets contain 33,977 regular frames. Every frame has one eight-byte signed
XYZW quaternion per each of Ratchet's 111 joints, followed by sparse eight-byte
scale and translation records. The parser validates the redundant partition
boundaries and 16-byte payload envelope before the compiler assigns transform
semantics.

The PAL executable independently establishes quaternion conversion as
`s16 / 32768` followed by normalization, scale as `u16 / 4096`, and sparse
translation as an absolute replacement for the common local translation.
Scale tag bit 15 selects local hierarchical scale; a clear bit selects a
terminal scale applied after hierarchy so it does not affect children. The
compiler composes `R * S + T`, parent hierarchy, terminal scale, and finally
the recovered inverse bind. The all-level sweep decodes 90,514,728 finite
matrix components. It intentionally retains 25,267 singular global matrices
and the corresponding skin matrices caused by authored zero scale components.
Veldin sequence 0 frame 0 position-skins all 5,583 high-LOD Ratchet vertices to
finite bounds X `[-0.391791, 0.376465]`, Y `[-0.465786, 0.388369]`, and Z
`[-0.0195114, 1.54662]`. Normal transformation for those singular poses remains
separate until the original VU0 policy is proven; no epsilon or identity repair
is substituted.

The matching player-control audit identifies three grounded locomotion slots:
slot 0 is idle (10 frames, phase rate `0.125`), slot 3 is walk (33 frames,
phase rate `0.25`), and slot 4 is run (23 frames, phase rate `0.5`). A non-zero
sequence word at `+0x18` overrides the per-frame phase rate; zero retains the
current frame's rate. The source player increments phase once per PAL 50 Hz
update and wraps these sequences on a cycle boundary. Trigger words are audio
events, not melee-hit notifications. No jump, fall, landing, or wrench semantic
mapping is claimed by this evidence.

OpenRC's current neutral profile therefore publishes only those three confirmed
looping clips. The runtime converts 50 source updates to its 60 fixed ticks with
an integer accumulator and holds the last grounded pose while airborne. This
timing and selection policy is deterministic and package-only; it does not
embed the source slot table in the runtime.

The regular packet decoder validates signed TOPS-relative VIF UNPACKs for
fixed-12 texture coordinates, V4-8 strip indices, and optional V4-32 AD-GIF
texture primitives. It reconstructs delayed 512-entry vertex-cache indices,
restart strips, texture switches, and non-degenerate triangle topology. A
logical secret-index terminator may precede zero vector padding; non-zero bytes
after it remain a hard format error. The full LOD assembler resets state
between high and low LOD, but executes each LOD's packets in table order while
carrying the 512-entry vertex cache and current texture. All local and shared
model occurrences across the 19 levels validate as 31,809 regular packets,
2,409,108 source vertex records, 2,760,391 transfer vertices, and 2,729,636
triangles. Exactly 232,096 duplicate transfers inherit a cache entry from an
earlier packet, and every one resolves without a fabricated source.

Veldin alone links 125 classes to 96 local model cores, 21 shared gadgets, and
eight external/zero references, with 124 texture-table entries and 296 static
placements. Including its shared gadgets, 1,176 regular packets contain 91,136
source records, 102,855 transfer vertices, 104,957 reconstructed triangles,
and 8,477 resolved cross-packet duplicate transfers.

Primary-extent-0 subrange 3 is the raw GS RAM image paired with the decoded
subrange-10 core. The tfrag and Moby tables use the same validated 0x10-byte
record and separate table-local index domains. Every tfrag bank on all 19
levels decodes under the same bounds, with 42–155 entries per level. Veldin's
78 tfrag textures contain 878,592 base pixels and 3,514,368 RGBA bytes.
Across all 19 levels, the separate 3,896 Moby texture records decode
to 42,187,520 linear PSMT8 base pixels and 168,750,080 RGBA bytes. Every
`textures_base_offset + data_offset + width * height` envelope and every
`palette_block * 0x100 + 0x400` CLUT envelope is in range. The decoder swaps
GS CLUT address bits 3/4, doubles PS2 alpha below `0x80` and saturates the rest,
but performs no pixel unswizzle or vertical flip for RAC1. Veldin contributes
124 images and 1,592,320 base pixels; texture 0 is a valid 256x256 Ratchet
atlas. Packet texture numbers remain local class slots and are resolved through
each class's 16-byte slot map before scene material batches are formed.

The production static-scene path assembles only high LOD for classes without
joints, compacts vertices actually referenced by triangles, and applies the
verified instance matrix order `T * S * Rz * Ry * Rx`; class scale was already
applied during packet decoding and is not multiplied twice. On Veldin, five
classes account for 133 rendered placements, 20,370 output vertices, 13,130
triangles, and world-space diagnostic bounds X `[81.5255, 206.495]`, Y
`[85.0339, 301.295]`, Z `[29.4775, 76.6971]`. Another 153 placements use
jointed models and ten use external/zero ownership, so both groups are skipped
rather than drawn incorrectly. Across all levels, the same bounded path builds
9,122 placements into 3,214,949 vertices and 2,628,565 triangles, while 6,237
animated and 873 external/zero placements remain pending. Metal/bangle meshes,
skeletal bind/animation transforms, and original visibility remain separate
work. The native Veldin viewer now submits the 133 supported static placements
through their contiguous material batches, samples the decoded RGBA base images
with perspective-correct UVs, rejects only alpha-zero texels, and uses a D24
depth buffer. Explicitly untextured batches retain a wireframe fallback rather
than receiving a guessed material.

The entry-16 tfrag stream supplies complete STQ on the recovered source
vertices. The runtime converts it to logical `S/Q,T/Q`, groups emitted
triangles by their snapshotted texture state, and treats the fully-known
table-index-shaped TEX0 low word as the tfrag table index. PRMODECONT/PRMODE is
not emitted by these captured packets, so a documented RAC1 fallback accepts
the material only when exactly one GS context was programmed; two programmed
but unselected contexts remain unresolved. The same adapter requires TME and
perspective STQ; missing STQ, disabled texture mapping, and FST/UV packets stay
on the wireframe path until their coordinate convention is implemented.
Veldin's 78-image tfrag bank and
these bounded material runs now use the same D3D11 sampling, alpha rejection,
and depth path as static Mobys. Unresolved materials stay on the wireframe
fallback.

The CLI keeps those reported bounds in world units. The current recovered
SceneBlock source batch deliberately preserves the signed integer inputs to
`ITOF0`, and the reference tfrag/model decoders establish a scale of 1,024 raw
units per world unit. Runtime integration therefore selects an explicit
`scene_block_itof0_units` policy and scales the complete transformed Moby
position by 1,024 before merging; mixing the two domains directly is rejected
by design and the conversion has a deterministic test.

## VAGp V1 audio

The disc tables reference 37 global VAGp extents and 792 distinct per-level
VAGp LBAs. All 829 records use the same bounded big-endian `0x30`-byte header:

- bytes `0x00..0x03` are `VAGp` and the version at `0x04` is `0x20`;
- the reserved words at `0x08`, `0x14`, `0x18`, and `0x1C` are zero;
- `0x0C` gives a non-empty, 16-byte-aligned ADPCM payload size;
- `0x10` gives the sample rate, and `0x20..0x2F` preserves a raw 16-byte name;
- the exact logical size is `0x30 + payload_size`; disc extents use its minimum
  2048-byte sector envelope and contain only zero padding after that boundary.

The payload grammar is exactly `Z, 0*, 1, 7`: one full zero lead-in frame,
ordinary content frames, one flag-1 frame, and a final flag-7 frame. Every
global record declares 44,056 Hz. Of the local records, 663 declare 44,056 Hz
and 129 declare 44,100 Hz. The name field is not assumed to end in NUL: 35
global and 121 local names occupy all 16 bytes.

OpenRC decodes every frame linearly and reports the playable content as
`[1, frame_count - 1)`. Canonical WAV export retains the flag-1 frame, omits
the zero lead-in and terminal flag-7 frame, and writes mono signed PCM16LE at
the rate declared by the asset. Two real-image controls cover both observed
rates:

| Record | Rate | Logical / payload bytes | Content samples | WAV bytes | PCM SHA-256 | WAV SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| Global slot 51, LBA 3079 | 44,056 | 18,880 / 18,832 | 32,900 | 65,844 | `eab3dd430eb55a9e3c26533a04e403728957e31f353216edea5a8bc6e71b53b2` | `9f93904b0453fded3567925e26a2115c2776c16c22ae3f79b21e051e36ff365b` |
| Local music, LBA 1544375 | 44,100 | 193,616 / 193,568 | 338,688 | 677,420 | `dec12ffca1c39f85cf41d0321612271c08169e97f62301a572f4941debeaf207` | `c059ffd53719386ca5b3433f2b32c1dc0fb3dcc823bc1099433ad57765805b17` |

## SBlkBundleV3

Primary-extent-0 subrange 1 of every level contains an exact two-record
wrapper. It starts with little-endian version `3` and record count `2`,
followed by two contiguous `(offset, size)` pairs. The first record has `SBlk`
magic; the second is the corresponding PS2 ADPCM frame bank.

The SBlk record uses a fixed `0x3C`-byte header and a table of 12-byte
descriptors:

```text
u32 type
u32 packed_count
u32 data_offset
```

The item count is the low 16 bits of `packed_count`; upper bits remain opaque
flags. Counts are nonzero, the first data offset is zero, and every following
offset equals the previous offset plus `previous_count * 0x28`. Item data
starts at the end of the descriptor table and ends exactly with the SBlk
record. Unknown descriptor types, flags, and header words are reported rather
than rejected.

Across all 19 levels, OpenRC validates 5,382 descriptors and 10,466 fixed-size
items. Each item is preserved as ten little-endian words. Word zero is a tag,
word seven is reserved and zero, and tag `1` uses word six as a 16-byte-aligned
offset into the secondary bank. Offset zero is a valid sample reference.

The 6,965 tag-`1` references identify 4,788 unique blocks; 2,177 references
therefore share a block. Sorted unique offsets plus the secondary-record end
partition the bank into exact non-empty ranges. Every referenced range begins
with a zero 16-byte frame, while extra zero frames inside ranges are permitted
and cannot be used as a directory. The complete 32,292,064-byte corpus contains
2,018,254 PS2 ADPCM frames. All observed frames use predictor `0..4`, shift
`0..12`, and flag values `0, 1, 2, 3, 6, 7`. Unknown item tags, descriptor
types, flags, and other item words remain owned raw metadata.

The 989snd runtime establishes that SBlk does not carry a block sample rate in
Hz. For every tag-1 reference, record byte `0x0A` (word-2 bits 16-23) is a
signed `center_note` and byte `0x0B` (bits 24-31) is a signed `center_fine`.
The module loads both with signed-byte instructions, passes them to
`sceSdNote2Pitch`, and writes the result to the SPU2 per-voice pitch register.
Mutable invocation note/fine values supply the other side of that conversion.

This tuning belongs to a reference, not to the physical ADPCM block:

- 6,965 references resolve to 4,788 physical blocks;
- 461 blocks are referenced with more than one center-note/fine pair;
- exact audio hashing gives 514 duplicate-payload groups, of which 79 vary in
  center-note/fine despite identical stored audio bytes;
- none of the 4,788 SBlk blocks matches any of the 829 VAGp payloads, either
  exactly or after their known lead-in/control frames are stripped.

Therefore VAGp header rates cannot be transferred to SBlk. SBlk WAV export
requires a named explicit policy: either caller-supplied Hz or the explicit
48 kHz unpitched SPU-native diagnostic convention. The latter is a playback
choice, not recovered block metadata. The encoder decodes only the declared
content-frame range and reports any loop as a half-open sample interval.

Every block follows one of three exact flag grammars, where `Z` is a full zero
lead-in or padding frame: `Z, 0*, 1, 7` for 4,200 one-shots, and either
`Z, 2+, 6, 2+, 3, [Z]` or `Z, 6, 2+, 3, [Z]` for 588 loops. Of those loops,
488 contain the optional final zero padding frame. The public report preserves
the content range plus loop-start, loop-end, and padding metadata.

The common clean-room PS ADPCM decoder consumes complete 16-byte frames
linearly and preserves every control flag as metadata; it does not assume a
sample rate, stop automatically, or synthesize loop playback. Each ordinary
frame yields 28 signed 16-bit samples in low-nibble/high-nibble order. The
five predictor coefficient pairs, divided by 64, are `(0,0)`, `(60,0)`,
`(115,-52)`, `(98,-55)`, and `(122,-60)`. History is updated only after
saturation. All low-three-bit flag combinations `0..7` are accepted and every
frame is decoded normally; stop and loop behavior belongs to the caller.

Two independent real-image controls validate the implementation. Global VAGp
slot 51 yields 32,956 linear samples with PCM SHA-256
`de26f66d432f1e2448a7096ad349061b2124ab24129d4dce21489e53e69e4d6a`;
its content range without the lead-in and terminal control frame yields 32,900
samples with SHA-256
`eab3dd430eb55a9e3c26533a04e403728957e31f353216edea5a8bc6e71b53b2`.
SBlk level-0 block 0 yields 13,328 linear samples with SHA-256
`2aa82d0b94b01095cb29712e19f235bf311bf4df5a8c80a45628ea7e97a3ae6e`;
its reported content range yields 13,272 samples with SHA-256
`6a47b154f357f14473b161b83eb84caf685537744cfe7800153b096dd39f93d8`.
Exporting that content with the named `spu-native-48000` policy produces a
26,588-byte canonical mono PCM16LE WAV with SHA-256
`7189be66d1707801a1f7ed6d2d701842dd4c3512affea945df619043034d813d`.

## First decoded bundle

The WAD at LBA 14365 occupies 162 sectors. Its 668,780-byte decoded output is a
bounded bundle with a `0xC0`-byte header, one implicit initial record, and 23
explicit `(offset, size)` slots. Active records begin at the next `0x40`
boundary, alignment gaps are zero, and the final active record ends exactly at
the decoded buffer boundary.

The exact reference bundle contains 12 nested WadV1 records, nine ELF32
little-endian MIPS records, and three empty slots. Every nested WAD declares
exactly its table-record size, with no sector padding inside the bundle. The
nine ELF records pass bounded header, table, segment, and section-range
validation. WadBundle uses the same span-based ELF parser as standalone files,
so record and file validation cannot silently diverge.

All nine ELF records are relocatable IOP/IRX modules: `e_type = 0xFF80`,
`e_machine = MIPS`, and `e_flags = 1`. Their `.iopmod` sections, relocation
tables, and bounded import stubs identify:

| Slot | Module | Version | Import libraries |
| ---: | --- | ---: | ---: |
| 13 | `sio2man` | `0x0205` | 8 |
| 14 | `mcman` | `0x0222` | 11 |
| 15 | `mcserv` | `0x0210` | 9 |
| 16 | `Dbc_Manager` | `0x0202` | 8 |
| 17 | `sio2d` | `0x0102` | 6 |
| 18 | `ds2u` | `0x0202` | 9 |
| 19 | `IOP_stash_daemon` | `0x0000` | 6 |
| 20 | `Sound_Device_Library` | `0x0303` | 5 |
| 21 | `989snd_Library` | `0x0209` | 13 |

The modules cover controller/memory-card services, debug/SIF transport,
general IOP staging, and audio. None contains a literal `WAD` or `2FIP`
reference; the texture/resource decoders therefore remain targets in the main
EE executable or other decoded data, rather than these IOP modules.

## Scene animation banks and localized subtitle tails

The structural-family pass profiles all 4,605 unique decoded WadV1 payloads
without retaining their asset bytes. On the reference image it produces 1,250
exact candidate keys. Candidate identity is based on a versioned canonical
feature key; its SHA-256 is only a printable ID. The semantic pass now proves
that exactly all 4,081 local resource-block WAD-run payloads, and no payloads
from another provenance class, are complete `SceneAnimationBankV1` records.

The aligned scene header accepts the two observed tags `0xFFFFFFF8` and
`0xFFFFFFFA`. Word 1 is either zero or an absolute offset to the optional
subtitle table, word 3 is the actor-track count, and the exact header size is
`align16(0x14 + actor_count * 4)`. The corpus contains 1 through 8 and 11 actor
tracks. A 0x20-byte camera-record stream follows the header and has either
`2 * frame_count - 1` records or the observed endpoint-trimmed
`2 * frame_count - 3` variant.

Each actor begins with a 0x10-byte header: class ID, total scene-record count,
scene-record index, and an absolute root-transform offset. The diagnostic CLI
labels observed class IDs `0` and `10` as Ratchet and Clank, while the parser
preserves every class ID without assigning names to the rest. The following
sequence owns a four-word neutral prefix, an 8-bit common frame count, fixed
`00 FF FF` controls, two zero words, and one aligned relative offset per frame.
Every observed offset
has zero high flag bits. A regular frame closes exactly at
`frame_start + 0x10 + data_size_qwords * 0x10`; the parser rejects non-zero
offset flags until another layout is independently demonstrated. Exactly one
16-byte root transform follows per frame, with a raw zero W word. Large camera
and frame bodies remain zero-copy ranges, while bounded metadata is owned.

When word 1 is non-zero, the scene ends with a PAL subtitle directory. Its
0x10-byte rows contain two timing values, five strictly ascending 16-bit offsets
in EN/FR/DE/ES/IT order, and a zero reserved halfword; a fixed 16-byte sentinel
closes the rows. Text is NUL-terminated with minimum four-byte padding and a
final 16-byte logical envelope. The shared parser preserves empty directories,
empty translations, the original single-byte encoding, and any opaque suffix
after the logical text end. The production probe validates the whole scene,
not merely the recognizable localization tail, and classifies the full corpus
with zero ambiguous matches.

## EE/R5900 boundary inventory

The EE inventory selects only allocated, file-backed executable sections that
map consistently through an executable `PT_LOAD`. It excludes both DVP overlay
placeholder sections and the `.vutext` code section referenced by their table.
The reference ELF then has exactly two EE code regions:

| Section | File range | Virtual address | Words |
| --- | --- | ---: | ---: |
| `core.text` | `0x00013300 + 119,288` | `0x00112380` | 29,822 |
| `.text` | `0x000EA000 + 349,872` | `0x001E9080` | 87,468 |

The 469,160 bytes contain 117,290 preserved instruction words. The bounded V1
subset classifies 43,475 words and keeps 73,815 explicitly unclassified. Its
16,934 control transfers comprise 270 direct jumps, 9,505 conditional
branches, 5,402 direct calls, four conditional link branches, 55 indirect
jumps, 67 indirect calls, and 1,631 `jr ra` returns. Every decoded transfer has
its delay slot, and every direct target remains inside selected EE code.

There are 90 raw `SYSCALL` sites. All 90 have an immediately preceding proven
constant write to `$v1`; 85 additionally form the exact adjacent
`immediate v1,zero; syscall; jr ra; delay-slot` wrapper. Direct JALs target
those wrappers 340 times across 60 distinct wrappers. This is an executable
region/call/syscall-boundary inventory, not yet a recovered function map or
context-sensitive call graph.

The boot ELF also contains 43 DVP overlay records. OpenRC now parses the
canonical 12-byte `name/lma/vma` records, validates their linked overlay
string table and one-to-one overlay-section names, maps each LMA range through
its file-backed `PT_LOAD.p_paddr`, and resolves the real bytes in the allocated
`.vutext` section. The processor-specific
section identifiers and record layout follow the public
[GNU binutils MIPS ELF definitions](https://mirror.iscas.ac.cn/git/sourceware.org/git/binutils-gdb/-/blame/7ae26f2731023f088ea805a39e47d1680055f88e/include/elf/mips.h?page=1).
The `.DVP.overlay.*` sections in this executable are placeholder ranges, not
the code source. The image also contains paths such as
`cdrom0:\DATA\LEVELS\LEVEL`, `cdrom0:\CODE\I5\PARAM.TXT;1`, and the
`occ_sample_deltas.dat`/`occ_samp.dat` resources. The confirmed `55907`
bytes feed the bounded neutral decoder, exact SceneBlock task execution,
ordered GS output, and the native diagnostic wireframe viewer. Assigning
gameplay semantics to that geometry remains future work alongside the
remaining audio and asset-format analysis.
