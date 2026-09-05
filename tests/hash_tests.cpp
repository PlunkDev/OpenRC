#include "openrc/hash.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class TemporaryFile final {
public:
    TemporaryFile() {
        const auto suffix = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path() /
                ("openrc-hash-test-" + std::to_string(suffix) + ".bin");
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write("abc", 3);
        if (!output) {
            throw std::runtime_error("could not create SHA-256 test file");
        }
    }

    ~TemporaryFile() {
        std::error_code ignored;
        static_cast<void>(std::filesystem::remove(path_, ignored));
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::string digest(const std::string_view value) {
    openrc::Sha256 hash;
    hash.update(std::span{
        reinterpret_cast<const std::byte*>(value.data()),
        value.size()});
    return openrc::hex_digest(hash.finish());
}
void expect_equal(
    const std::string& actual,
    const std::string& expected,
    const std::string& name) {
    if (actual != expected) {
        throw std::runtime_error(name + " mismatch: " + actual);
    }
}

} // namespace

int main() {
    try {
        expect_equal(
            digest(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "empty SHA-256");
        expect_equal(
            digest("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "abc SHA-256");
        expect_equal(
            digest("The quick brown fox jumps over the lazy dog"),
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
            "sentence SHA-256");
        const TemporaryFile file;
        constexpr std::string_view expected_file_digest =
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        expect_equal(
            openrc::hex_digest(openrc::sha256_file_digest(file.path())),
            std::string(expected_file_digest),
            "raw file SHA-256");
        expect_equal(
            openrc::sha256_file(file.path()),
            std::string(expected_file_digest),
            "hex file SHA-256");
        std::cout << "OpenRC SHA-256 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC SHA-256 tests failed: " << error.what() << '\n';
        return 1;
    }
}
