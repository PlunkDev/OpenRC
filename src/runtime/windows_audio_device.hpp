#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openrc::runtime {
struct WindowsAudioOutputDeviceV1 {
    std::uint32_t index = 0, channels = 0;
    std::wstring name;
};
struct WindowsAudioOutputSelectionV1 {
    // UINT32_MAX is the ordinary Windows mapper when no override is supplied.
    std::uint32_t index = UINT32_MAX;
    std::wstring name;
    bool explicit_selection = false;
};
// Enumeration and selection never open a device or submit any audio.
[[nodiscard]] std::vector<WindowsAudioOutputDeviceV1> enumerate_windows_audio_outputs_v1();
// OPENRC_AUDIO_DEVICE: decimal WinMM index.
// OPENRC_AUDIO_DEVICE_NAME: exact name, either uniquely resolved or checked
// against the selected index. An invalid explicit selection never falls back.
// With neither override, read audio-output.txt from application_paths().local_data:
// one exact UTF-8 device name, at most 4096 bytes, optional final CRLF. An
// existing invalid file or unavailable saved device blocks opening audio.
// OPENRC_AUDIO_DEVICE_REQUIRED=1 rejects the unconfigured Windows mapper.
[[nodiscard]] WindowsAudioOutputSelectionV1 select_windows_audio_output_v1();
[[nodiscard]] std::string windows_audio_output_name_utf8_v1(const std::wstring&);
}
