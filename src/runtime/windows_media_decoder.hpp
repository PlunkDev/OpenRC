#pragma once
#include "openrc/media_clip.hpp"
#include <memory>
#include <optional>

namespace openrc::runtime {
struct MediaVideoFrameV1 {
  std::uint32_t width = 0, height = 0;
  std::int64_t presentation_time_100ns = 0;
  std::int64_t duration_100ns = 0;
  std::vector<std::byte> rgba;
};

// Standard MPEG decoder adapter. The supplied neutral clip outlives this
// player. Output is in presentation order, including the final drained frames.
class WindowsMediaDecoderV1 final {
public:
  explicit WindowsMediaDecoderV1(const MediaClipV1 &clip);
  ~WindowsMediaDecoderV1();
  WindowsMediaDecoderV1(const WindowsMediaDecoderV1 &) = delete;
  WindowsMediaDecoderV1 &operator=(const WindowsMediaDecoderV1 &) = delete;
  [[nodiscard]] std::optional<MediaVideoFrameV1> next_frame();
  [[nodiscard]] bool input_available() const noexcept;
  [[nodiscard]] bool drained() const noexcept;
  // Stop accepting prepared packets; next_frame still returns the real
  // decoder's pending output until its drain completes. Does not report
  // presentation or audio completion.
  void stop_input_and_drain() noexcept;
private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};
} // namespace openrc::runtime
