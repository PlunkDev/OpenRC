# RenderSceneV1

`RenderSceneV1` is the neutral, planet-independent static render resource for a
prepared level. In `LevelPackageV1` it uses:

- resource ID `world/render-scene`;
- type ID `openrc.render-scene`;
- schema version `1`;
- `upsert` operation in the base package;
- `overlay_replaceable`, but not `overlay_removable`.

The last flag policy is applied by the package compiler, not embedded in these
bytes. Replacing the whole resource is enough for a high-resolution texture or
level-geometry mod, while a resolved graphical level cannot silently lose its
required scene.

V1 is intentionally a **static, unlit** contract. It can represent terrain,
static props and repeated class meshes today without importing RAC/PS2 names or
offsets into the runtime format. Animated actors, skinning, lights, fog,
particles and translucent blending require separate resources or a later scene
schema. A camera is bootstrap/player state and is not part of this resource.

## Coordinate and shading contract

- Coordinates are right-handed, +Z up, in natural world units.
- A mesh stores local positions. Every visible mesh is referenced by at least
  one affine 3x4 instance; baked world-space geometry uses an identity instance.
- Matrices are row-major. Rows 0, 1 and 2 produce world X, Y and Z. Elements 3,
  7 and 11 are translation. There is no homogeneous divide.
- Triangle indices are unsigned 32-bit **mesh-local** indices with canonical
  counter-clockwise front faces. Materials explicitly select double-sided
  drawing when required.
- `rgba8` packs R in bits 0..7, G in 8..15, B in 16..23 and A in 24..31.
- Unlit output multiplies material base color, vertex color when enabled, and
  the sampled base-color texture when present.
- V1 alpha is either `opaque`, or `mask`. A masked fragment passes when final
  alpha is at least `alpha_cutoff_rgba8 / 255`. Blending is not guessed.
- Addressing is limited to `repeat` and `clamp_to_edge`. Minification and
  magnification are `nearest` or `linear`; mip selection is `none`, `nearest`
  or `linear`.
- Textures are tightly packed, row-major RGBA8. Each has a `linear` or `srgb`
  sampling color space. Mip zero is mandatory. Further mips halve each
  dimension using `max(1, previous / 2)` and the chain ends at its first
  `1x1` level.

There are deliberately no normals in V1: the currently recovered complete
cross-planet data can feed an unlit renderer, but it cannot truthfully provide
one uniform authored normal stream. Normals must not be invented merely to fill
a native structure.

## Logical model and canonical form

The public model is in `include/openrc/render_scene.hpp`:

- `RenderSceneTextureV1` owns one or more `RenderSceneTextureMipV1` records;
- `RenderSceneMaterialV1` contains the complete bounded V1 draw policy;
- `RenderSceneMeshV1` owns vertices, local indices and draw ranges;
- `RenderSceneInstanceV1` maps a mesh into the world with an affine 3x4 matrix;
- `RenderSceneV1` owns those four global tables.

Texture, material, mesh and instance IDs are dense zero-based table indices.
The encoder sorts each table by ID and then requires IDs `0..count-1`. Mips stay
in level order. Draws stay in triangle order and must be non-empty, contiguous,
triangle-aligned ranges that cover their mesh index buffer exactly. Every mesh
must be instantiated, every material must be used by a draw, and every texture
must be used by a material. This excludes multiple encodings containing dead
payloads. A fully empty scene (all four tables empty) is nevertheless a valid
canonical resource, so preparation can represent a level for which no static
art has been recovered without inventing geometry.

All floating-point fields are finite IEEE-754 binary32 values. The writer maps
both signed zeros to positive zero; the reader rejects negative zero. Readers
also reject unknown enum values and flags, non-zero reserved bytes, missing or
orphaned references, invalid mip chains, invalid local indices, alternate table
offsets and trailing bytes.

`canonicalize_render_scene_v1` performs table sorting and float-zero
canonicalization, then validates the result. `validate_render_scene_v1`
requires an already-canonical object. Both operations take explicit hard
limits. `encode_render_scene_v1` and `decode_render_scene_v1` add an encoded-byte
limit and translate model errors into `RenderSceneIoError`.

## Binary envelope

All integers and binary32 bit patterns are little-endian. Records are written
field by field; C++ object layout is never serialized. The file is one exact
concatenation with no alignment gaps:

```text
0x100-byte header
texture table       (32 bytes each)
texture-mip table   (32 bytes each)
material table      (32 bytes each)
mesh table          (48 bytes each)
draw-range table    (24 bytes each)
vertex table        (24 bytes each)
instance table      (64 bytes each)
local-index table   (4 bytes each)
RGBA8 pixel data
```

The 256-byte header is:

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | 8 bytes | `ORRSCN\0\0` |
| `0x08` | `u32` | I/O format version, `1` |
| `0x0c` | `u32` | header bytes, `0x100` |
| `0x10` | `u64` | exact total byte count |
| `0x18` | `u32` | scene schema version, `1` |
| `0x1c` | `u32` | right-handed Z-up world units, `1` |
| `0x20` | `u32` | position/UV/RGBA8 vertex format, `1` |
| `0x24` | `u32` | triangle-list topology, `1` |
| `0x28` | `u32` | RGBA8 texture format, `1` |
| `0x2c` | `u32` | zero |
| `0x30..0x44` | 6 x `u32` | texture, mip, material, mesh, draw and instance counts |
| `0x48` | `u64` | aggregate vertex count |
| `0x50` | `u64` | aggregate index count |
| `0x58` | `u64` | aggregate pixel-data bytes |
| `0x60..0x7c` | 8 x `u32` | exact record sizes in the order above |
| `0x80..0xc0` | 9 x `u64` | exact table/data offsets in the order above |
| `0xc8..0xff` | bytes | zero |

Offsets must equal the layout calculated from the counts. Pixel bytes must end
at `total_bytes`; no padding or suffix is accepted.

### Texture record, 32 bytes

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | dense texture ID |
| `0x04` | `u8` | color space: linear `0`, sRGB `1` |
| `0x05` | 3 bytes | zero |
| `0x08` | `u32` | first global mip record |
| `0x0c` | `u32` | mip count, at least one |
| `0x10` | `u32` | base width, equal to mip zero |
| `0x14` | `u32` | base height, equal to mip zero |
| `0x18` | 8 bytes | zero |

Texture mip ranges partition the global mip table in texture-ID order.

### Texture-mip record, 32 bytes

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | owning texture ID |
| `0x04` | `u32` | zero-based mip level |
| `0x08` | `u32` | width |
| `0x0c` | `u32` | height |
| `0x10` | `u64` | byte offset relative to the pixel-data area |
| `0x18` | `u64` | exactly `width * height * 4` bytes |

Mip pixel ranges partition the pixel-data area exactly in texture/level order.

### Material record, 32 bytes

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | dense material ID |
| `0x04` | `u32` | base texture ID, or `0xffffffff` |
| `0x08` | `u32` | packed base RGBA8 |
| `0x0c` | `u32` | bit 0 vertex color, bit 1 double-sided |
| `0x10` | `u8` | U address: repeat `0`, clamp `1` |
| `0x11` | `u8` | V address: repeat `0`, clamp `1` |
| `0x12` | `u8` | min filter: nearest `0`, linear `1` |
| `0x13` | `u8` | mag filter: nearest `0`, linear `1` |
| `0x14` | `u8` | mip filter: none `0`, nearest `1`, linear `2` |
| `0x15` | `u8` | alpha: opaque `0`, mask `1` |
| `0x16` | `u8` | alpha cutoff byte |
| `0x17` | 9 bytes | zero |

Opaque requires cutoff zero; mask requires non-zero. An untextured material
uses the canonical unused sampler tuple repeat/repeat/linear/linear/none. A mip
filter other than `none` requires the selected texture to have at least two
levels.

### Mesh and draw records

The 48-byte mesh record contains:

| Offset | Type | Meaning |
|---:|---|---|
| `0x00` | `u32` | dense mesh ID |
| `0x04` | `u32` | zero |
| `0x08` | `u64` | first aggregate vertex |
| `0x10` | `u64` | vertex count |
| `0x18` | `u64` | first aggregate index |
| `0x20` | `u64` | index count |
| `0x28` | `u32` | first global draw record |
| `0x2c` | `u32` | draw count |

Mesh ranges partition the aggregate tables in mesh-ID order. Every mesh has at
least one vertex, triangle and draw; a mesh has at most `2^32-1` vertices.

Each 24-byte draw record stores owning mesh ID (`u32` at `0x00`), material ID
(`u32` at `0x04`), mesh-local first index (`u64` at `0x08`) and index count
(`u64` at `0x10`).

Each 24-byte vertex stores binary32 X/Y/Z at `0x00..0x08`, binary32 U/V at
`0x0c..0x10`, and packed RGBA8 at `0x14`. Each index-table item is one local
`u32` vertex index.

### Instance record, 64 bytes

The instance record stores dense instance ID (`u32` at `0x00`), mesh ID (`u32`
at `0x04`), eight zero bytes, then twelve row-major binary32 affine elements at
`0x10..0x3f`.

## Compiler boundary and present limitations

A RAC compiler may initially emit one baked terrain mesh, one baked static-Moby
mesh and one baked TIE mesh, each with an identity instance. It must remap the
independent tfrag, Moby and TIE texture-bank indices into this one global table;
equal numeric source indices must never alias accidentally. The same schema can
later emit one mesh per recovered class plus many instances, without changing
the runtime resource.

The compiler, not this payload, owns build-specific facts such as the current
SceneBlock VITOF0-input-to-world scale. Source locators, ISO offsets, class IDs,
WAD names and host paths belong in `LevelPackageV1` provenance or compiler
inputs, never in `RenderSceneV1`.

The current diagnostic recovery has important honest gaps: some terrain packets
are not yet recovered, animated Moby models are skipped, and original GS clamp,
filter, translucency and draw-order semantics are not all known. A first
compiler should select explicit V1 fallback policies and record that generated
pass in package provenance; it must not label those policies as lossless source
facts. Shrubs, animated actors and other missing families can be added as new
meshes/resources after their semantics are recovered.
