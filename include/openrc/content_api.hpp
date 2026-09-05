#pragma once

#include <cstdint>

namespace openrc {

// Version of the source-independent resource contracts consumed by this
// runtime. Disc/build profiles are asset-compiler concerns and must not leak
// into native gameplay or renderer dispatch.
inline constexpr std::uint32_t kOpenRcContentApiVersionV1 = 1U;

} // namespace openrc
