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

The header count equals the size of primary-extent-3 table 0 on all 19 levels.
The complete sweep validates 20,016 descriptors, 47,894,288 bytes of chained
block envelopes, and 368,974,896 trailing bytes. Declared and parsed counts
both range from 444 to 2,144. The 19 final envelopes add 44,960 bytes.

| Level | Decoded bytes | Count / records | Directory end | Chain end | Raw WadV1 SHA-256 | Decoded SHA-256 |
| ---: | ---: | ---: | ---: | ---: | --- | --- |
| 0 | 16,791,232 | 460 / 460 | `0x7300` | `0x113EE0` | `737fec3eff206fa57125451387feac69946f045643567587097b1f9c0403302d` | `f316516a3aba1d3fc6b75b8eda1fffa13fd67fa911ee09d3026c99a07ad7d957` |
| 18 | 22,661,824 | 1,144 / 1,144 | `0x11E00` | `0x2A6380` | `0221dc2b9e25ad62c3dedcdfd480dc2e7967fac15c8821dd6c750a5471f826ce` | `f26bba8b9a87f133c6e72101e4e8e758409467b80bcbf0c34568e7fd4282bc77` |

The leading floats and regular packet patterns are consistent with spatial
scene records. The boot ELF's `.data` also retains `tfrag geom` as one label in
a diagnostic memory map beside occlusion, sky, collision, and other categories;
it is not a format signature or a decoder reference. Those clues justify a
diagnostic-rendering experiment but do not yet justify labelling the blocks as
terrain, collision, or models in the parser contract.

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

| Level | Subrange-2 bytes / SHA-256 | Table offset | Opaque header | First WAD | Last WAD | Indexed size |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | `0x7DE0` / `3f048365b73b522a4e7b046478e2cf9d6a3b4445f34e80645ea08e4ea90efbb4` | `0x7C90` | `0x0091489B` | `0xF96130` | `0x1000D80` | `0x10036C0` |
| 18 | `0xA770` / `140edae2f88d5c0231b262a7ee4335722967d10a37955552169af32af4ca2769` | `0xA620` | `0x00CA9C27` | `0x152F550` | `0x159A180` | `0x159CAC0` |

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

Therefore VAGp header rates cannot be transferred to SBlk. Any future SBlk WAV
export must require a named explicit policy, such as caller-supplied Hz or an
explicit 48 kHz unpitched SPU-native diagnostic convention. The latter would
be a playback choice, not recovered block metadata.

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

The boot ELF also contains 43 DVP overlay sections and paths such as
`cdrom0:\DATA\LEVELS\LEVEL`, `cdrom0:\CODE\I5\PARAM.TXT;1`, and the
`occ_sample_deltas.dat`/`occ_samp.dat` resources. The next Stage 1 targets are
SBlk sample-rate and loop playback metadata, the larger per-level decoded
containers that remain opaque, and the eventual R5900/VU/IOP execution
boundary.
