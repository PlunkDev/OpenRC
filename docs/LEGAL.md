# Legal and project boundaries

OpenRC is an independent interoperability and preservation project. This file is
a project policy, not legal advice.

## Repository rules

The repository must not contain:

- Ratchet & Clank disc images or extracted copyrighted assets;
- original executable bytes, level data, music, dialogue, movies, or textures;
- PlayStation 2 BIOS files;
- proprietary Sony or Insomniac SDK components;
- leaked source code, symbols, documentation, or other non-public material;
- links or instructions for obtaining unauthorized copies.

Allowed project material includes:

- independently written source code and tests;
- abstract descriptions of file formats and behavior;
- hashes, sizes, addresses, names, and other interoperability metadata where
  appropriate;
- scripts that operate locally on a user's legally obtained copy.

## Runtime distribution model

OpenRC distributions will contain the launcher, tools, and runtime only. On first
use, the user supplies a compatible disc image. Required data is extracted and
converted locally under the OpenRC local-data directory.

## Third-party code

Every imported dependency must have a compatible license and attribution. Wrench
is GPL-3.0 licensed; linking or copying its implementation would affect OpenRC's
license obligations. Until the project license is chosen, Wrench may be studied
as evidence that formats are understood by the community, but its source must
not be copied into OpenRC.

The current WadV1 and PS ADPCM decoders and the 2FIP, boundary-table,
MapArtV1, PS2D, VAGp, SBlk, SBlk audio-bank, SceneBlockDirectoryV1, and
SceneBlock VIF/VU processing and phase grouping, DVP overlay-table inventory,
the neutral DVP VU microprogram decoder and control/access inventory, and
CompanionTerminalWadIndexV1 parsers are independent implementations based on
locally observed behavior, public PS2/binutils documentation, and
repository-owned synthetic test vectors; they incorporate no Wrench source
code.

The GPL-licensed GNU binutils
[DVP opcode table](https://github.com/ps2dev/binutils-gdb/blob/dvp-v2.45.1/opcodes/dvp-opc.c)
is used only as a public primary reference for factual instruction names,
bit-field positions, masks, and encoded values. OpenRC does not copy that table
or its implementation and does not link against binutils. Its decoder and
tests are independently written from the public encoding facts, locally
observed behavior, and original synthetic vectors; no binutils source, binary,
or game microprogram bytes are distributed in this repository.

## Trademarks

Ratchet & Clank, PlayStation, Insomniac Games, and related names and marks belong
to their respective owners. OpenRC is not affiliated with or endorsed by Sony
Interactive Entertainment or Insomniac Games.
