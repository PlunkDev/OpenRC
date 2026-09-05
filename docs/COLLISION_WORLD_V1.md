# CollisionWorldV1 binary resource

`CollisionWorldV1` is the platform-neutral collision payload used by the
native runtime after a level has been prepared. It contains no disc offsets,
PS2 sparse-tree pointers, or source-specific vertex encoding. A
`LevelPackageV1` identifies it with:

- resource ID `world/collision`
- type ID `openrc.collision-world`
- schema version `1`

The public constants live in `openrc/collision_world_io.hpp`.

## Coordinates and byte order

Every integer is little-endian. Positions are signed 32-bit Q6 values: one
integer step is exactly `1/64` of a natural world unit. The coordinate system
is right-handed and Z-up. No floating-point value occurs in this payload.

## Header

The fixed header is 128 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 8 | `ORCCOL\0\0` magic |
| `0x08` | 4 | I/O format version (`1`) |
| `0x0c` | 4 | header bytes (`128`) |
| `0x10` | 8 | exact total payload bytes |
| `0x18` | 4 | Q6 units per world unit (`64`) |
| `0x1c` | 4 | uniform-grid cell size in Q6 units |
| `0x20` | 8 | vertex count |
| `0x28` | 8 | triangle count |
| `0x30` | 8 | occupied grid-cell count |
| `0x38` | 8 | grid triangle-reference count |
| `0x40` | 8 | vertex-table offset |
| `0x48` | 8 | triangle-table offset |
| `0x50` | 8 | grid-cell-table offset |
| `0x58` | 8 | grid-reference-table offset |
| `0x60` | 32 | reserved zero bytes |

Tables are tightly packed in the order shown. The vertex offset must equal
`0x80`; every following offset must equal the exact end of its preceding
table; the declared total must equal both the calculated final table end and
the input span size. Gaps, truncation, and trailing data are rejected.

The grid cell size is policy, not advisory metadata. A reader accepts the
payload only when the header value exactly equals
`CollisionWorldIoLimitsV1::world.grid_cell_size_q6`.

## Records

Each vertex is 12 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | signed X in Q6 |
| `0x04` | 4 | signed Y in Q6 |
| `0x08` | 4 | signed Z in Q6 |

Each triangle is 20 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 12 | three unsigned 32-bit vertex indices |
| `0x0c` | 1 | layer (`0` world, `1` hero-only) |
| `0x0d` | 1 | surface flags; bit 0 means a raw source byte is present |
| `0x0e` | 1 | raw surface byte |
| `0x0f` | 1 | surface kind (`raw & 0x1f`) |
| `0x10` | 1 | sound ID (`raw >> 5`) |
| `0x11` | 3 | reserved zero bytes |

No gameplay meaning is assigned to kind or sound ID by this format. They are
an exact, validated split of the raw source byte. When bit 0 is clear, all
three surface bytes must be zero. All unknown layer values and flag bits are
rejected.

Each occupied uniform-grid cell is 24 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 4 | signed cell X |
| `0x04` | 4 | signed cell Y |
| `0x08` | 4 | signed cell Z |
| `0x0c` | 4 | non-zero reference count |
| `0x10` | 8 | first index in the reference table |

Cells are strictly ordered lexicographically by `(x, y, z)`. Reference
ranges are contiguous, begin at zero, have no gaps, and collectively own the
complete reference table. Each reference is one unsigned 32-bit triangle
index. References within one cell are strictly ascending, so duplicates and
non-canonical order are invalid.

## Rebuilt-grid validation

The serialized grid is an acceleration structure, never collision authority.
Both the encoder and decoder independently call `build_collision_world_v1`
on the mesh using the caller's build limits and grid-cell policy.

- The encoder rejects an in-memory world whose grid differs from the rebuilt
  grid instead of silently repairing it.
- The decoder first validates the encoded grid structurally, rebuilds a new
  grid from the decoded mesh, requires exact equality, and returns only the
  rebuilt world.
- `CollisionWorldError` from either rebuild is wrapped as
  `CollisionWorldIoError`.

Consequently, an old, corrupted, or malicious package cannot make a triangle
disappear from runtime queries merely by deleting its grid reference. Extra
references and altered cell coordinates are rejected for the same reason.

Counts, checked table sizes, exact offsets, total bytes, caller limits,
host-container limits, and every `u64` to `size_t` conversion are validated
before table allocation.

## Observed payload sizes

The uncompressed size is exactly:

`128 + vertices*12 + triangles*20 + cells*24 + references*4`

With four-world-unit cells, the PAL v2.00 Veldin corpus produces 24,116
vertices, 39,085 triangles, 8,234 cells, and 176,199 references: 1,973,632
bytes total. The largest of the 19 observed levels is approximately 8.19 MB;
all 19 together are approximately 87.91 MB before any package-level
compression.

A conservative per-level policy for the supported corpus is 32 MB encoded,
one million vertices, two million triangles, one million cells, and 16
million grid references.
