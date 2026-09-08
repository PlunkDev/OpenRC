# RAC1 shared keyed text banks

Compiler-side source format, not a prepared UI resource or menu implementation.
The source bank is shared by frontend and gameplay text and is distinct from
the existing timed five-language subtitle format.

`parse_rac_text_bank_v1` accepts one exact logical bank. Its little-endian
8-byte header gives row count and logical byte size including the header.
Each 16-byte row contains a bank-relative text offset, full 32-bit lookup key,
and two preserved auxiliary words whose meanings are not yet assigned.
Text remains an owned, NUL-delimited byte stream: glyph/control values are not
converted to Unicode, stripped or assigned speculative formatting semantics.

The supported retail layout has consecutive text regions starting immediately
after the row table, each ending in minimum zero four-byte alignment. The
last region closes the declared bank extent. Empty banks occupy eight bytes.
Offset aliases, gaps, extra suffixes and nonzero/nonminimal padding are rejected
as outside the established format rather than normalized into guessed content.

`parse_rac_text_bank_directory_v1` consumes the complete sector envelope:
eight ordered offsets followed by the banks, with minimum zero 16-byte bank
alignment and minimum zero 2048-byte final alignment. Empty and partial slots
remain in place. Input, row and text-byte limits apply to the entire directory.
Public ranges are absolute in the caller's input; raw row-relative offsets are
also preserved. A string cannot borrow its terminator from another bank or
from outer padding.

`find_rac_text_bank_entry_v1` preserves original first-match lookup in source
order, including duplicate/unsorted keys. A missing key returns null. The
original renderer's separate fallback behavior and locale selection are not
implemented by this parser. Numeric slots are not automatically host locales.

## Source qualification and verification

The owned PAL v2.00 source establishes bank selection/relocation and lookup:
boot `1eb228..1eb334`, first-match lookup `1fe4d0..1fe538`, and pointer retrieval
`1fe540..1fe57c`. A second loader at `21e3f8..21e474` confirms the logical-size
header interpretation. Local source identities, bank hashes and raw audits are
recorded in ignored `local/forensics/rac-startup-text-and-flow-evidence.md`.

The audited native corpus probe matched all eight slots, 8,743 rows and 467,449
non-NUL text bytes against independent reads of the original sector extent.
Both standalone and directory parsers preserve every source row, auxiliary
word, byte range and glyph/control byte; first-match results also agree.
Synthetic tests cover malformed lengths/counts/offsets, ownership boundaries,
padding, aliases, aggregate limits, empty banks and duplicate key selection.
No original text bank or game-data fixture is distributed with the repository.

## Original font metrics

`parse_rac_font_metric_table_v1` consumes exactly 232 four-byte rows (928
bytes); `parse_rac_font_metric_tables_v1` consumes the three consecutive tables
(2784 bytes). Each row preserves unsigned atlas U/V, signed destination Y
offset, and a signed fourth byte used either as ordinary pen advance or accent
X offset. That fourth byte is not a universal glyph width. All control/empty
rows and accent rows through index `0xe7` remain in original byte-indexed order;
there is no invented 256-row table or Unicode mapping.

Original metric reads are shared by integer glyph owner `1f6668` and floating
owner `1f69f0`. Source table addresses are `1df3d0`, `1df770` and `1dfb10`.
The audited native probe checked all 696 rows against independent signed-byte
reads of the hash-qualified original ELF. Both parser entrypoints matched.
Synthetic tests cover all field byte values, all truncated prefixes, trailing
data, table separation and result ownership. No original metric fixture is
distributed. Detailed source identities, consumer traces and font-atlas leads
remain in ignored `local/forensics/rac-frontend-glyph-next.md`.

The original New Game text node selects font 3 and the multiline/layout
owner's floating glyph path, including measurement, shadow and color passes.
These are recovered dependencies, not implemented menu behavior. The original
[frontend textures](RAC_FRONTEND_TEXTURE_V1.md), including all three font
atlases, are now decoded and source-compared. Their prepared UI resource,
render-state contract and complete layout behavior are not yet implemented.

**PARTIAL frontend:** keyed text and font-metric decoding are implemented and
verified. Control-stream interpretation, font rendering, actual menu
presentation, navigation, audio and transitions remain. There is no replacement
Start button, menu renderer or new runtime here.
