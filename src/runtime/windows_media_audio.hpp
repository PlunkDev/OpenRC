#pragma once
#include "openrc/media_clip.hpp"
#include "openrc/audio_clip.hpp"
#include <memory>
namespace openrc::runtime {
// Per-playback PCM device. Its sample position is the A/V clock. The neutral
// clip must outlive this owner; no audio decode or source format enters here.
class WindowsMediaAudioV1 final {
public:
  explicit WindowsMediaAudioV1(const MediaClipV1 &clip);
  explicit WindowsMediaAudioV1(const AudioClipV1 &clip);
  ~WindowsMediaAudioV1();
  WindowsMediaAudioV1(const WindowsMediaAudioV1 &) = delete;
  WindowsMediaAudioV1 &operator=(const WindowsMediaAudioV1 &) = delete;
  void start();
  [[nodiscard]] std::uint64_t played_samples() const;
  // Natural buffer completion only. A reset marking its buffer WHDR_DONE
  // does not turn an interrupted playback into natural completion.
  [[nodiscard]] bool finished() const;
  // Stops playback, returns/unprepares its buffer and closes the device.
  // Successful repetition is harmless. Device failures throw and retain the
  // unfinished ownership step for retry. Preserves the pre-reset sample clock.
  void stop_and_retire();
  [[nodiscard]] bool retired() const noexcept;
private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};
}
