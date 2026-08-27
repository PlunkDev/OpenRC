# OpenRC roadmap

This roadmap describes technical milestones rather than release dates.

## Stage 0 — Foundation

- [x] C++20 and CMake project structure
- [x] ISO 9660 and `SYSTEM.CNF` inspection
- [x] PAL and NTSC-U/C build recognition
- [x] native Windows launcher shell
- [x] `PlunkDev/OpenRC` configuration and local-data directories
- [x] synthetic disc-parser tests
- [x] validate the inspector against the PAL v2.00 retail image
- [x] record the SHA-256 and basic metadata of the reference image
- [x] hash and inventory the boot executable and disc filesystem

## Stage 1 — Disc and executable inventory

- [x] extract regular ISO files into the local-data directory
- [x] preserve a deterministic manifest containing sizes, extents, and hashes
- [x] inventory ELF program headers, sections, and embedded DVP/VU overlays
- [x] parse the DVP overlay table and map every LMA/VMA record to real code
- [x] decode the confirmed VU microprogram under explicit bounds into neutral
  typed control-flow/access metadata while preserving raw and unknown data
- [x] export the boot ELF for analysis
- [x] establish the PAL v2.00 executable and image as the reference build
- [x] identify and validate the `DiscTocV1` index used outside regular ISO files
- [x] map level IDs 0–18 to their four primary raw-disc extents
- [x] identify the `WadV1` header and sector-padding contract
- [x] implement a bounded clean-room decoder for the WadV1 LZ stream
- [x] inventory the first decoded bundle of nested WAD and ELF records
- [x] expose and validate the remaining local TOC asset subtables
- [x] decode indexed `2FIP` textures and export normalized RGBA/TGA images
- [x] split the 38 decoded boundary-table payloads into seven bounded regions
- [x] parse the MapArtV1 record table and compose its three 2FIP preview layers
- [x] parse the PS2D memory-card icon and tagged save-template bundle
- [x] expose per-level SBlk descriptor groups and fixed-size item data
- [x] identify and validate the per-level SBlk PS2 ADPCM bank directory
- [x] implement a bounded common SBlk/VAGp PS ADPCM frame decoder
- [x] parse VAGp V1 metadata and export bounded canonical PCM16 WAV diagnostics
- [x] prove SBlk tuning is signed per-reference note/fine with no intrinsic Hz
- [ ] add explicit-policy SBlk sample export and reconstruct remaining voice playback
- [x] inventory embedded IOP/IRX module metadata, relocations, and imports
- [x] validate the neutral per-level SceneBlockDirectoryV1 block chain
- [x] partition every scene-block remainder into eight verified neutral sections
- [x] expose the bounded VIF stream spanning scene-block sections 0-4
- [x] execute its bounded UNPACK writes into a conservative neutral VU1 snapshot
- [x] group VIF/VU commands into exact neutral phases with inherited state
- [x] reconstruct the exact SceneBlock task preamble and entry-specific packet
- [x] carry VIF/VU state across the double-buffered 328-qword input banks
- [x] identify and upload the caller's four-qword frame transform to both banks
- [x] execute a real record through normal VU termination and complete XGKICKs
- [x] decode ordered GIF/GS state into known vertices and emitted primitives
- [x] export the first auto-fit wireframe preview from real scene-block geometry
- [x] validate the companion index for the shared terminal WadV1 bank
- [ ] identify the semantic formats inside decoded WAD payloads
- [ ] inventory R5900, VU0, VU1, IOP, GS, and system-call boundaries

## Stage 2 — Native level viewer

- [x] bind a prepared ELF back to the selected ISO before native execution
- [x] open a D3D11 window and submit the first decoded SceneBlock triangle batch
- [x] connect launcher Play to the adjacent native runtime
- [x] recover entry-16 source XYZ through the game's descriptor/index stream
- [x] add an isolated debug orbit and decoded-GS comparison view
- [x] independently execute and merge all supported records for a raw level view
- [ ] recover the original per-frame camera transform from its EE caller
- [ ] parse terrain, collision, instances, models, and textures
- [ ] render a classified, textured Veldin scene in a native window
- [ ] compare geometry and transforms with reference captures

## Stage 3 — First playable vertical slice

- load Ratchet and his animations
- implement controller input and camera behavior
- reconstruct movement, collision, and basic interactions
- implement one weapon and one enemy type
- load and save isolated test state

## Stage 4 — Game-complete runtime

- reconstruct every planet and gameplay subsystem
- replace audio, cutscene, menu, save, and transition systems
- regression-test progression from a new game through the ending
- support PAL and NTSC timing differences

## Stage 5 — Modern platform features

- scalable resolution and aspect ratio
- high-frame-rate correctness
- controller remapping and accessibility options
- portable saves and launcher updates
- modding interfaces built on documented, original OpenRC formats
