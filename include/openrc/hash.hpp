#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace openrc {

class Sha256 final {
public:
    Sha256();

    void update(std::span<const std::byte> data);
    [[nodiscard]] std::array<std::byte, 32> finish();

private:
    void transform(const std::byte* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::byte, 64> buffer_{};
    std::uint64_t total_size_ = 0;
    std::size_t buffer_size_ = 0;
    bool finished_ = false;
};

[[nodiscard]] std::string hex_digest(std::span<const std::byte> digest);
[[nodiscard]] std::string sha256_file(const std::filesystem::path& path);

} // namespace openrc
