#pragma once

#include "openrc/rac_initial_progress_template.hpp"
#include "openrc/session_state.hpp"

#include <array>
#include <string>

namespace openrc {

// Compiler-side bindings only. Runtime receives the neutral artifact and uses
// exact prepared view keys; it never interprets source level rows or addresses.
struct RacInitialProgressRowBindingsV1 {
  std::string selector_bytes;
  std::string primary_bit_words;
  std::string registration_keys;
  std::string registration_auxiliary;
  std::string registration_words;
};

struct RacInitialProgressCompilationV1 {
  SessionStateInitialV1 initial;
  std::string encoded_source_level;
  std::array<RacInitialProgressRowBindingsV1, kRacInitialProgressLevelRowsV1>
      rows;
};

// Preserves the entire decoded template input, including encoded L=-1. This
// is not the reset wrapper's separate L=0 store, nor a complete live new-game
// image. No suppression, cached-selector-prefix or checkpoint bytes are added.
[[nodiscard]] RacInitialProgressCompilationV1
compile_rac_initial_progress_v1(const RacInitialProgressTemplateV1 &source,
                                const SessionStateLimitsV1 &limits);

} // namespace openrc
