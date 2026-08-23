#include "openrc/wad.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kTestLogicalBlock = 2;
constexpr std::uint64_t kTestExtentOffset = kTestLogicalBlock * openrc::kWadSectorSize;

void write_le32(std::uint8_t* target, const std::uint32_t value) {
    target[0] = static_cast<std::uint8_t>(value & 0xffU);
    target[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    target[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    target[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

struct SyntheticWad {
    std::vector<std::uint8_t> image;
    std::uint32_t total_bytes = 0;
    std::uint64_t sector_count = 0;
};

SyntheticWad make_wad(
    const std::vector<std::uint8_t>& stream,
    const std::array<std::uint8_t, openrc::kWadV1AuxiliarySize>& auxiliary = {}) {
    const auto total = static_cast<std::uint64_t>(openrc::kWadV1HeaderSize) + stream.size();
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("synthetic WAD is too large");
    }
    const auto sectors = (total + openrc::kWadSectorSize - 1U) / openrc::kWadSectorSize;
    const auto extent_bytes = sectors * openrc::kWadSectorSize;

    SyntheticWad result;
    result.total_bytes = static_cast<std::uint32_t>(total);
    result.sector_count = sectors;
    result.image.resize(static_cast<std::size_t>(kTestExtentOffset + extent_bytes + 37U), 0);
    std::fill(
        result.image.begin(),
        result.image.begin() + static_cast<std::ptrdiff_t>(kTestExtentOffset),
        0xa5U);
    std::fill(
        result.image.begin() + static_cast<std::ptrdiff_t>(kTestExtentOffset + extent_bytes),
        result.image.end(),
        0x7cU);

    auto* header = result.image.data() + kTestExtentOffset;
    header[0] = 'W';
    header[1] = 'A';
    header[2] = 'D';
    write_le32(header + 3, result.total_bytes);
    std::copy(auxiliary.begin(), auxiliary.end(), header + 7);
    std::copy(stream.begin(), stream.end(), header + openrc::kWadV1HeaderSize);
    return result;
}

void write_image(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& image) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(image.data()),
        static_cast<std::streamsize>(image.size()));
    if (!output) {
        throw std::runtime_error("failed to create synthetic WAD image");
    }
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_inspect_rejected(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& image,
    const std::uint64_t logical_block,
    const std::uint64_t sector_count,
    const std::string& message) {
    write_image(path, image);
    bool rejected = false;
    try {
        (void)openrc::inspect_wad(path, logical_block, sector_count);
    } catch (const openrc::WadError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void expect_decode_rejected(
    const std::filesystem::path& path,
    const SyntheticWad& wad,
    const std::uint64_t cap,
    const std::string& message) {
    write_image(path, wad.image);
    bool rejected = false;
    try {
        (void)openrc::decode_wad(path, kTestLogicalBlock, wad.sector_count, cap);
    } catch (const openrc::WadError&) {
        rejected = true;
    }
    expect(rejected, message);
}

std::string decoded_text(const std::vector<std::byte>& bytes) {
    return {
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

std::vector<std::byte> logical_wad_bytes(const SyntheticWad& wad) {
    std::vector<std::byte> logical(wad.total_bytes);
    const auto source_begin = wad.image.begin() +
        static_cast<std::ptrdiff_t>(kTestExtentOffset);
    std::transform(
        source_begin,
        source_begin + static_cast<std::ptrdiff_t>(wad.total_bytes),
        logical.begin(),
        [](const std::uint8_t value) { return static_cast<std::byte>(value); });
    return logical;
}

void expect_decode_bytes_rejected(
    const std::span<const std::byte> logical_wad,
    const std::uint64_t cap,
    const std::string& message) {
    bool rejected = false;
    try {
        (void)openrc::decode_wad_bytes(logical_wad, cap);
    } catch (const openrc::WadError&) {
        rejected = true;
    }
    expect(rejected, message);
}

void test_valid_inspection(const std::filesystem::path& directory) {
    constexpr std::array<std::uint8_t, openrc::kWadV1AuxiliarySize> auxiliary{
        0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const auto wad = make_wad({0xdeU, 0xadU, 0xbeU, 0xefU, 0x42U}, auxiliary);
    const auto image_path = directory / "valid.img";
    write_image(image_path, wad.image);

    const auto report = openrc::inspect_wad(image_path, kTestLogicalBlock, wad.sector_count);
    expect(report.image_path == image_path, "source path was not preserved");
    expect(report.logical_block == kTestLogicalBlock, "logical block was not reported");
    expect(report.sector_count == 1, "minimal sector count was not reported");
    expect(report.extent_bytes == openrc::kWadSectorSize, "extent size is incorrect");
    expect(report.total_bytes == 21, "logical size is incorrect");
    expect(report.compressed_payload_bytes == 5, "compressed payload size is incorrect");
    expect(report.auxiliary_bytes == auxiliary, "opaque auxiliary bytes were not preserved");
    expect(report.padding_bytes == openrc::kWadSectorSize - 21U, "padding size is incorrect");
    expect(
        report.sha256 == "b6093b1f36df0f44ad2e2f946f79931af5d55946145763a60e1388569f302788",
        "logical WadV1 SHA-256 is incorrect");
}

void verify_decode(
    const std::filesystem::path& directory,
    const std::string& name,
    const std::vector<std::uint8_t>& stream,
    const std::string& expected,
    const std::string& expected_sha256) {
    const auto wad = make_wad(stream);
    const auto image_path = directory / name;
    write_image(image_path, wad.image);
    const auto decoded = openrc::decode_wad(
        image_path,
        kTestLogicalBlock,
        wad.sector_count,
        expected.size());
    expect(decoded_text(decoded.bytes) == expected, name + " decoded bytes are incorrect");
    expect(decoded.sha256 == expected_sha256, name + " decoded SHA-256 is incorrect");
    expect(decoded.source.total_bytes == wad.total_bytes, name + " source report is incorrect");
}

void test_decoder_examples(const std::filesystem::path& directory) {
    verify_decode(
        directory,
        "literal-a.img",
        {0x01U, 0x41U, 0x42U, 0x43U, 0x44U},
        "ABCD",
        "e12e115acf4552b2568b55e93cbd39394c4ef81c82447fafc997882a02d23677");
    verify_decode(
        directory,
        "medium-b.img",
        {0x01U, 0x41U, 0x42U, 0x43U, 0x44U, 0x22U, 0x0cU, 0x00U},
        "ABCDABCD",
        "eb9651ab32840938610c6f2da4d2be34f3f70c9ebbd40e63ba49349124d1f301");
    verify_decode(
        directory,
        "short-c.img",
        {0x01U, 0x41U, 0x42U, 0x43U, 0x41U, 0x88U, 0x00U},
        "ABCABCABC",
        "12f54f42ce246d5311d04dddbd3cb72bdf2447765aa5439e69033601dfe020bd");

    const auto empty = make_wad({});
    const auto empty_path = directory / "empty.img";
    write_image(empty_path, empty.image);
    const auto decoded = openrc::decode_wad(
        empty_path,
        kTestLogicalBlock,
        empty.sector_count,
        0);
    expect(decoded.bytes.empty(), "zero output cap did not allow empty output");
    expect(
        decoded.sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "empty decoded SHA-256 is incorrect");
}

void test_decoder_branch_examples(const std::filesystem::path& directory) {
    verify_decode(
        directory,
        "extended-literal.img",
        {
            0x00U, 0x02U,
            0x41U, 0x42U, 0x43U, 0x44U, 0x45U,
            0x46U, 0x47U, 0x48U, 0x49U, 0x4aU,
            0x4bU, 0x4cU, 0x4dU, 0x4eU, 0x4fU,
            0x50U, 0x51U, 0x52U, 0x53U, 0x54U,
        },
        "ABCDEFGHIJKLMNOPQRST",
        "40800c4dc7925aa3ce2bd450f0b46efe056dbf5f4a83844555a43564b680a8ae");

    verify_decode(
        directory,
        "dummy-literals.img",
        {0x11U, 0x03U, 0x00U, 0x58U, 0x59U, 0x5aU},
        "XYZ",
        "ade099751d2ea9f3393f0f32d20c6b980dd5d3b0989dea599b966ae0d3cd5a1e");

    std::vector<std::uint8_t> aligned_stream(0x1000U, 0xeeU);
    aligned_stream[0] = 0x12U;
    aligned_stream[1] = 0x00U;
    aligned_stream[2] = 0x00U;
    aligned_stream.insert(
        aligned_stream.end(),
        {0x01U, 0x41U, 0x42U, 0x43U, 0x44U});
    verify_decode(
        directory,
        "aligned-literal.img",
        aligned_stream,
        "ABCD",
        "e12e115acf4552b2568b55e93cbd39394c4ef81c82447fafc997882a02d23677");

    verify_decode(
        directory,
        "extended-medium.img",
        {
            0x01U, 0x41U, 0x42U, 0x43U, 0x44U,
            0x20U, 0x01U, 0x0cU, 0x00U,
        },
        "ABCDABCDABCDABCDABCDABCDABCDABCDABCDAB",
        "b322b0d053c43adc4791ebc45dcfbdfa8fae5771232a2d3ba731345999d2ad65");

    std::vector<std::uint8_t> far_stream{
        0x01U, 0x41U, 0x42U, 0x43U, 0x44U,
    };
    for (std::size_t index = 0; index < 57U; ++index) {
        far_stream.insert(
            far_stream.end(),
            {0x20U, 0xffU, 0x0cU, 0x00U});
    }
    far_stream.insert(far_stream.end(), {0x11U, 0x04U, 0x00U});

    std::string far_expected;
    far_expected.reserve(16'423U);
    for (std::size_t index = 0; index < 4'105U; ++index) {
        far_expected += "ABCD";
    }
    far_expected += "DAB";
    verify_decode(
        directory,
        "far-match.img",
        far_stream,
        far_expected,
        "4b8631c10e09a210956121554901f18fcd6f89795ca000a1920844842191e31c");
}

void test_nested_logical_segment_decoder() {
    auto logical = logical_wad_bytes(make_wad({
        0x01U, 0x41U, 0x42U, 0x43U, 0x44U}));
    const auto decoded = openrc::decode_wad_bytes(logical, 4);
    expect(decoded_text(decoded.bytes) == "ABCD", "nested WAD decoded bytes are incorrect");
    expect(
        decoded.sha256 == "e12e115acf4552b2568b55e93cbd39394c4ef81c82447fafc997882a02d23677",
        "nested WAD decoded SHA-256 is incorrect");

    expect_decode_bytes_rejected(
        logical,
        3,
        "nested WAD output cap was ignored");

    logical.push_back(std::byte{0});
    expect_decode_bytes_rejected(
        logical,
        4,
        "nested WAD with trailing bytes was accepted");
    logical.pop_back();
    logical.pop_back();
    expect_decode_bytes_rejected(
        logical,
        4,
        "truncated nested WAD was accepted");
}

void test_header_and_extent_rejections(const std::filesystem::path& directory) {
    auto wad = make_wad({0x01U, 0x41U, 0x42U, 0x43U, 0x44U});
    auto image = wad.image;
    image[static_cast<std::size_t>(kTestExtentOffset)] = 'B';
    expect_inspect_rejected(
        directory / "bad-magic.img", image, kTestLogicalBlock, wad.sector_count,
        "bad WadV1 magic was accepted");

    image = wad.image;
    write_le32(image.data() + kTestExtentOffset + 3U, 15);
    expect_inspect_rejected(
        directory / "too-small.img", image, kTestLogicalBlock, wad.sector_count,
        "logical size smaller than the header was accepted");

    image = wad.image;
    write_le32(image.data() + kTestExtentOffset + 3U, 2049);
    expect_inspect_rejected(
        directory / "too-large.img", image, kTestLogicalBlock, wad.sector_count,
        "logical size larger than the extent was accepted");

    expect_inspect_rejected(
        directory / "zero-extent.img", wad.image, kTestLogicalBlock, 0,
        "extent smaller than the header was accepted");
    expect_inspect_rejected(
        directory / "out-of-bounds.img", wad.image, 4, 1,
        "out-of-bounds WAD extent was accepted");

    image = wad.image;
    image.resize(static_cast<std::size_t>(kTestExtentOffset + openrc::kWadSectorSize - 1U));
    expect_inspect_rejected(
        directory / "truncated.img", image, kTestLogicalBlock, 1,
        "truncated WAD extent was accepted");

    image = wad.image;
    image.resize(static_cast<std::size_t>(kTestExtentOffset + 2U * openrc::kWadSectorSize), 0);
    expect_inspect_rejected(
        directory / "extra-sector.img", image, kTestLogicalBlock, 2,
        "an extra full zero sector was accepted");

    image = wad.image;
    image[static_cast<std::size_t>(kTestExtentOffset + wad.total_bytes)] = 1;
    expect_inspect_rejected(
        directory / "nonzero-padding.img", image, kTestLogicalBlock, wad.sector_count,
        "non-zero sector padding was accepted");

    const auto overflow =
        std::numeric_limits<std::uint64_t>::max() / openrc::kWadSectorSize + 1U;
    expect_inspect_rejected(
        directory / "offset-overflow.img", wad.image, overflow, 1,
        "overflowing WAD extent offset was accepted");
    expect_inspect_rejected(
        directory / "size-overflow.img", wad.image, 0, overflow,
        "overflowing WAD extent size was accepted");
}

void test_decoder_rejections(const std::filesystem::path& directory) {
    expect_decode_rejected(
        directory / "stream-truncated.img",
        make_wad({0x01U, 0x41U}),
        32,
        "truncated literal run was accepted");
    expect_decode_rejected(
        directory / "bad-distance.img",
        make_wad({0x40U, 0x00U}),
        32,
        "match outside decoded output was accepted");
    expect_decode_rejected(
        directory / "output-cap.img",
        make_wad({0x01U, 0x41U, 0x42U, 0x43U, 0x44U}),
        3,
        "decoded output cap was ignored");
    expect_decode_rejected(
        directory / "malformed-dummy.img",
        make_wad({0x11U, 0x00U}),
        32,
        "truncated dummy command was accepted");
    expect_decode_rejected(
        directory / "malformed-pad.img",
        make_wad({0x12U, 0x01U, 0x00U}),
        32,
        "malformed alignment command was accepted");
    expect_decode_rejected(
        directory / "double-literal.img",
        make_wad({
            0x01U, 0x41U, 0x42U, 0x43U, 0x44U,
            0x01U, 0x45U, 0x46U, 0x47U, 0x48U}),
        32,
        "consecutive literal runs were accepted");

    std::vector<std::uint8_t> alignment_stream(0x1000U, 0xeeU);
    alignment_stream[0] = 0x12U;
    alignment_stream[1] = 0x00U;
    alignment_stream[2] = 0x00U;
    alignment_stream[123] = 0xedU;
    expect_decode_rejected(
        directory / "bad-alignment-fill.img",
        make_wad(alignment_stream),
        0,
        "non-0xee alignment padding was accepted");
}

} // namespace

int main() {
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_directory = std::filesystem::temp_directory_path() /
        ("OpenRC-wad-tests-" + std::to_string(unique_suffix));
    try {
        std::filesystem::create_directories(test_directory);
        test_valid_inspection(test_directory);
        test_decoder_examples(test_directory);
        test_decoder_branch_examples(test_directory);
        test_nested_logical_segment_decoder();
        test_header_and_extent_rejections(test_directory);
        test_decoder_rejections(test_directory);
        std::filesystem::remove_all(test_directory);
        std::cout << "OpenRC WadV1 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove_all(test_directory, ignored);
        std::cerr << "OpenRC WadV1 tests failed: " << error.what() << '\n';
        return 1;
    }
}
