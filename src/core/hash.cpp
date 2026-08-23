#include "openrc/hash.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace openrc {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] constexpr std::uint32_t rotate_right(
    const std::uint32_t value,
    const unsigned int count) {
    return (value >> count) | (value << (32U - count));
}

[[nodiscard]] std::uint8_t byte_value(const std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

} // namespace

Sha256::Sha256()
    : state_{
          0x6a09e667U,
          0xbb67ae85U,
          0x3c6ef372U,
          0xa54ff53aU,
          0x510e527fU,
          0x9b05688cU,
          0x1f83d9abU,
          0x5be0cd19U} {}

void Sha256::update(const std::span<const std::byte> data) {
    if (finished_) {
        throw std::logic_error("Cannot update a finalized SHA-256 digest");
    }

    total_size_ += static_cast<std::uint64_t>(data.size());
    std::size_t input_offset = 0;
    while (input_offset < data.size()) {
        const auto available = buffer_.size() - buffer_size_;
        const auto copy_size = std::min(available, data.size() - input_offset);
        std::copy_n(data.data() + input_offset, copy_size, buffer_.data() + buffer_size_);
        buffer_size_ += copy_size;
        input_offset += copy_size;

        if (buffer_size_ == buffer_.size()) {
            transform(buffer_.data());
            buffer_size_ = 0;
        }
    }
}

std::array<std::byte, 32> Sha256::finish() {
    if (finished_) {
        throw std::logic_error("SHA-256 digest was already finalized");
    }

    const auto bit_length = total_size_ * 8U;
    buffer_[buffer_size_++] = std::byte{0x80};

    if (buffer_size_ > 56) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.end(), std::byte{0});
        transform(buffer_.data());
        buffer_size_ = 0;
    }

    std::fill(
        buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_),
        buffer_.begin() + 56,
        std::byte{0});
    for (std::size_t index = 0; index < 8; ++index) {
        buffer_[56 + index] = std::byte{
            static_cast<std::uint8_t>(bit_length >> ((7U - index) * 8U))};
    }
    transform(buffer_.data());

    std::array<std::byte, 32> digest{};
    for (std::size_t state_index = 0; state_index < state_.size(); ++state_index) {
        for (std::size_t byte_index = 0; byte_index < 4; ++byte_index) {
            digest[state_index * 4 + byte_index] = std::byte{
                static_cast<std::uint8_t>(
                    state_[state_index] >> ((3U - byte_index) * 8U))};
        }
    }

    finished_ = true;
    return digest;
}

void Sha256::transform(const std::byte* block) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4;
        schedule[index] =
            (static_cast<std::uint32_t>(byte_value(block[offset])) << 24U) |
            (static_cast<std::uint32_t>(byte_value(block[offset + 1])) << 16U) |
            (static_cast<std::uint32_t>(byte_value(block[offset + 2])) << 8U) |
            static_cast<std::uint32_t>(byte_value(block[offset + 3]));
    }

    for (std::size_t index = 16; index < schedule.size(); ++index) {
        const auto value_15 = schedule[index - 15];
        const auto value_2 = schedule[index - 2];
        const auto sigma_0 =
            rotate_right(value_15, 7) ^ rotate_right(value_15, 18) ^ (value_15 >> 3U);
        const auto sigma_1 =
            rotate_right(value_2, 17) ^ rotate_right(value_2, 19) ^ (value_2 >> 10U);
        schedule[index] = schedule[index - 16] + sigma_0 + schedule[index - 7] + sigma_1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];

    for (std::size_t index = 0; index < schedule.size(); ++index) {
        const auto sum_1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto temporary_1 = h + sum_1 + choice + kRoundConstants[index] + schedule[index];
        const auto sum_0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary_2 = sum_0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary_1;
        d = c;
        c = b;
        b = a;
        a = temporary_1 + temporary_2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::string hex_digest(const std::span<const std::byte> digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto value : digest) {
        output << std::setw(2) << static_cast<unsigned int>(byte_value(value));
    }
    return output.str();
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open file for SHA-256: " + path.string());
    }

    Sha256 hash;
    std::vector<char> buffer(1024U * 1024U);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read_size = input.gcount();
        if (read_size > 0) {
            hash.update(std::span{
                reinterpret_cast<const std::byte*>(buffer.data()),
                static_cast<std::size_t>(read_size)});
        }
    }

    if (!input.eof()) {
        throw std::runtime_error("Failed while reading file for SHA-256: " + path.string());
    }
    return hex_digest(hash.finish());
}

} // namespace openrc
