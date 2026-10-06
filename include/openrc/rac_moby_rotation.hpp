#pragma once

#include "openrc/dvp_vu_execute.hpp"
#include "openrc/rac_moby_post.hpp"

namespace openrc {

struct RacMobyRotationEvaluationV1 {
  RacMobyPostRotationResultV1 rotation;
  std::uint64_t executed_instruction_pairs = 0U;
  std::uint32_t polynomial_calls = 0U;
  std::uint32_t composition_calls = 0U;
  // Preserve the shared executor's numerical qualifications, including its
  // conservative MIN/MAX warning. These are not physical-console results.
  std::vector<DvpVuExecutionWarningV1> warnings;
};

// Compiler-side owner of the actual resident VU0 d18 program. No instruction
// bytes are embedded or published in a neutral runtime package. Decode once,
// then evaluate the source post-step's rotation requests in authored order.
// This is a value projection, not a retained architectural VU-state adapter.
class RacMobyRotationSourceV1 final {
public:
  explicit RacMobyRotationSourceV1(std::span<const std::byte> boot_executable);

  // XYZ are raw source angles with magnitude <= source pi (40490fda), covering
  // the audited Veldin input domain. No normalization or epsilon zero test.
  // The source never reads rotation.w; even an exceptional W is irrelevant.
  // Scratch, flags, I and ACC start unknown. Only actual source instructions
  // may establish their values before use. VF20..22 must finish fully known.
  // The returned response advances RacMobyPostV1 only through rotation;
  // scale/derived/spatial and the rest of accepted construction remain owned
  // by their existing continuations. No spatial completion is manufactured.
  [[nodiscard]] RacMobyRotationEvaluationV1
  evaluate(const RacMobyPostRotationRequestV1 &request) const;

private:
  DvpVuProgramV1 program_;
};

} // namespace openrc
