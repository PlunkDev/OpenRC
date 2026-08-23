#include "openrc/hash.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

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
        std::cout << "OpenRC SHA-256 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenRC SHA-256 tests failed: " << error.what() << '\n';
        return 1;
    }
}
