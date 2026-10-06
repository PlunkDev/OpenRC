#include "windows_audio_device.hpp"
#include "openrc/paths.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <limits>
#include <fstream>
#include <optional>
#include <stdexcept>

namespace openrc::runtime {
namespace {
std::optional<std::wstring> environment(const wchar_t* key) {
    SetLastError(ERROR_SUCCESS);
    const auto needed = GetEnvironmentVariableW(key, nullptr, 0);
    if (!needed) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND || GetLastError() == ERROR_SUCCESS) return std::nullopt;
        throw std::runtime_error("Cannot read audio output selection from the environment");
    }
    if (needed > 1024) throw std::runtime_error("Audio output selection exceeds its size bound");
    std::wstring value(needed, L'\0');
    const auto written = GetEnvironmentVariableW(key, value.data(), needed);
    if (!written || written >= needed) throw std::runtime_error("Audio output selection changed while being read");
    value.resize(written); return value;
}
std::optional<std::wstring> saved_output_name() {
    const auto path = application_paths().local_data / L"audio-output.txt";
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) return std::nullopt;
    if (error) throw std::runtime_error("Cannot inspect the saved OpenRC audio output selection");
    const auto size = std::filesystem::file_size(path, error);
    if (error || !size || size > 4096)
        throw std::runtime_error("Saved OpenRC audio output must be a nonempty file of at most 4096 bytes");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Cannot read the complete saved OpenRC audio output selection");
    if (bytes.ends_with("\r\n")) bytes.resize(bytes.size() - 2);
    if (bytes.empty() || std::any_of(bytes.begin(), bytes.end(),
            [](unsigned char value) { return value < 32U || value == 127U; }))
        throw std::runtime_error("Saved OpenRC audio output must contain one nonempty device name");
    const auto needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (!needed || needed > 1024)
        throw std::runtime_error("Saved OpenRC audio output name is not valid bounded UTF-8");
    std::wstring result(needed, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
            result.data(), needed) != needed)
        throw std::runtime_error("Cannot decode the saved OpenRC audio output name");
    return result;
}
}
std::vector<WindowsAudioOutputDeviceV1> enumerate_windows_audio_outputs_v1() {
    std::vector<WindowsAudioOutputDeviceV1> result;
    const auto count = waveOutGetNumDevs();
    if (count > 4096) throw std::runtime_error("Windows audio output count exceeds its bound");
    result.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        WAVEOUTCAPSW capabilities{};
        const auto status = waveOutGetDevCapsW(index, &capabilities, sizeof(capabilities));
        if (status != MMSYSERR_NOERROR)
            throw std::runtime_error("Cannot inspect Windows audio output " + std::to_string(index) +
                " (waveOut " + std::to_string(status) + ")");
        const auto end = std::find(std::begin(capabilities.szPname), std::end(capabilities.szPname), L'\0');
        result.push_back({index, capabilities.wChannels, std::wstring(capabilities.szPname, end)});
    }
    return result;
}
WindowsAudioOutputSelectionV1 select_windows_audio_output_v1() {
    const auto requested_index = environment(L"OPENRC_AUDIO_DEVICE");
    auto requested_name = environment(L"OPENRC_AUDIO_DEVICE_NAME");
    const auto required = environment(L"OPENRC_AUDIO_DEVICE_REQUIRED");
    if (required && *required != L"0" && *required != L"1")
        throw std::runtime_error("OPENRC_AUDIO_DEVICE_REQUIRED must be 0 or 1");
    if (!requested_index && !requested_name) requested_name = saved_output_name();
    if (!requested_index && !requested_name) {
        if (required && *required == L"1")
            throw std::runtime_error("An explicit OpenRC audio output is required; Windows default output was not opened");
        return {UINT32_MAX, L"Windows default output", false};
    }
    const auto devices = enumerate_windows_audio_outputs_v1();
    if (requested_index) {
        std::uint32_t index = 0;
        for (const auto digit : *requested_index) {
            if (digit < L'0' || digit > L'9' ||
                index > (std::numeric_limits<std::uint32_t>::max() - (digit - L'0')) / 10U)
                throw std::runtime_error("OPENRC_AUDIO_DEVICE must be a decimal WinMM device index");
            index = index * 10U + static_cast<unsigned>(digit - L'0');
        }
        const auto found = std::find_if(devices.begin(), devices.end(),
            [&](const auto& device) { return device.index == index; });
        if (found == devices.end()) throw std::runtime_error("Requested OpenRC audio output is unavailable");
        if (requested_name && found->name != *requested_name)
            throw std::runtime_error("Requested OpenRC audio output index no longer matches its required name");
        return {found->index, found->name, true};
    }
    const WindowsAudioOutputDeviceV1* selected = nullptr;
    for (const auto& device : devices) if (device.name == *requested_name) {
        if (selected) throw std::runtime_error("OpenRC audio output name is ambiguous; provide its device index too");
        selected = &device;
    }
    if (!selected) throw std::runtime_error("Requested OpenRC audio output name is unavailable");
    return {selected->index, selected->name, true};
}
std::string windows_audio_output_name_utf8_v1(const std::wstring& name) {
    if (name.empty()) return {};
    if (name.size() > 1024) throw std::runtime_error("Windows audio output name exceeds its size bound");
    const auto needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(),
        static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    if (!needed) throw std::runtime_error("Cannot encode Windows audio output name");
    std::string result(needed, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()),
        result.data(), needed, nullptr, nullptr) != needed)
        throw std::runtime_error("Cannot encode Windows audio output name");
    return result;
}
}
