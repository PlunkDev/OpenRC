# RAC1 original frontend textures

Compiler-side texture recovery from the already-decoded global frontend WAD.
This is neither a replacement menu nor a new runtime asset system. The WAD
decompressor remains unchanged; this reader interprets one bounded resource
inside its output, distinct from 2FIP images and level Moby textures.

## Source layout and ownership

The established header fields are:

| Offset | Meaning used by this reader |
| --- | --- |
| `+0x04` | Shared-data base, relative to the decoded WAD start |
| `+0x0c` | End of the texture descriptor directory / next metadata section |
| `+0x58` | Source texture count |
| `+0x5c` | Texture descriptor directory, relative to decoded start |
| `+0x68` | Texture payload start, relative to shared-data base |
| `+0x80` | Following resource start, relative to shared-data base |

Each 16-byte descriptor contains four little-endian words: palette byte offset,
pixel byte offset, width and height. Both offsets refer to the texture payload,
not the WAD start. Source IDs and all original words/ranges are retained.
Every texture has linear PSMT8 indices and a 1024-byte RGBA32 palette.

The supported layout has aligned directories and payloads, with complete
payload coverage after deduplicating exact same-kind ranges. Real palette
sharing and whole-image aliases are preserved without deleting IDs. Partial
overlaps, cross-kind aliases and unclaimed gaps are outside the established
format and are rejected. Unrelated WAD sections are not reinterpreted or
certified by this parser.

Raw palette bytes, including original alpha and GS storage order, and raw
indices remain owned by the result. Canonical RGBA additionally uses the same
shared `ps2_palette.hpp` utilities as existing 2FIP and Moby decoders. Moving
those helpers did not change their results or public data contracts: 2FIP
still retains raw alpha until export, while Moby retains canonical RGBA.

## Bounded transfer domain

The source relocates offsets into 16-byte units held in unsigned halfwords;
unaligned offsets or values above `0xffff0` are rejected, not silently truncated.
Dimensions must be positive powers of two, at most 2048 and within caller
limits; 2048 is a transfer representation bound, not a hardware sampling
guarantee. Small widths retain the original upload template's buffer width
instead of inventing a zero-width buffer. The source emits one IMAGE packet
and one DMA reference per image, without a large-image chunking loop. Pixel
data must therefore occupy a nonzero whole number of quadwords fitting that
IMAGE packet's 15-bit loop field (`1..0x7fff` quadwords).

Transfer field widths can be cross-checked in the public
[PS2SDK GS register definitions](https://ps2dev.github.io/ps2sdk/gs__gp_8h.html).
Representability in those fields is not proof of every possible texture's
sampling behavior. Canonical RGBA alpha expansion is the existing asset-export
convention, not a complete recreation of GS blending or alpha testing.

Input size, texture count, dimensions, individual/aggregate pixel counts and
aggregate owned byte output are caller-bounded. Alias entries still consume
their full output budget; shared source offsets cannot bypass allocation
limits. Source extents and output budgets are checked before image expansion.

## Source comparison and regressions

The reference frontend has 40 descriptors, 33 unique palette ranges and 39
unique pixel ranges. Texture 19 aliases texture 0 completely; font textures
1, 2 and 3 share a real palette. The audited native corpus probe matched every
descriptor/range, 370,944 index bytes and 40,960 palette bytes counted per source
ID, then independently checked all 1,483,776 expanded RGBA bytes. Original
palettes retained their raw alpha values; this corpus spans `0..128`.

Nine synthetic test groups cover exact boundaries, all palette positions and
alpha byte values,
ownership, aliases, neighboring-resource isolation, malformed metadata,
aggregate limits and source transfer constraints. Oversized transfer fixtures
have complete source data and permissive caller limits, so their rejection
actually exercises the original transfer-field limits. Shared palette helpers
are exhaustively tested over all 256 inputs, and existing 2FIP/Moby regressions
still pass. Original assets, hashes and detailed source traces remain in ignored
`local/forensics`; no source image or game-data test fixture is distributed.

## Frontend status

The source New Game node's font selection and the original font metrics are
described in [shared text and metrics](RAC_TEXT_BANK_V1.md). Font atlas recovery
does not implement the layout owner, original draw-state setup, focus/input,
animation, audio or transitions. Those dependencies must be closed before
defining and integrating a neutral prepared UI resource. No host font, fake
menu button or generated replacement asset is introduced here.
