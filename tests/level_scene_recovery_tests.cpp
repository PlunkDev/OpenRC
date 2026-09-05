#include "level_scene_recovery.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callback>
void expect_runtime_error(Callback&& callback, const std::string& message) {
    try {
        callback();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] openrc::runtime::LevelSceneRecoveryRequestV1
make_synthetic_request() {
    return openrc::runtime::LevelSceneRecoveryRequestV1{
        std::filesystem::path("synthetic-disc.iso"),
        std::filesystem::path("synthetic-boot.elf"),
        0U,
        openrc::runtime::LevelSceneRecordSelectionV1::single_record,
        0U,
        16U,
    };
}

void test_default_limits_and_profile_are_explicitly_bounded() {
    const auto limits =
        openrc::runtime::make_level_scene_recovery_limits_v1();
    const auto profile =
        openrc::runtime::make_level_scene_recovery_profile_v1();

    expect(limits.max_aggregate_records == 4096U,
           "the aggregate record limit changed");
    expect(limits.aggregate_geometry.max_vertices == 3'000'000U,
           "the aggregate vertex limit changed");
    expect(limits.aggregate_geometry.max_triangle_indices == 9'000'000U,
           "the aggregate index limit changed");
    expect(limits.moby_geometry.max_output_vertices == 3'000'000U &&
               limits.tie_geometry.max_output_vertices == 3'000'000U,
           "static environment output is not bounded");
    expect(profile.moby_coordinate_domain ==
                   openrc::runtime::MobySceneCoordinateDomainV1::
                       scene_block_itof0_units &&
               profile.tie_coordinate_domain ==
                   openrc::runtime::TieSceneCoordinateDomainV1::
                       scene_block_itof0_units,
           "the viewer coordinate profile changed");

    for (std::size_t row = 0U;
         row < profile.frame_input.transform_qwords.size();
         ++row) {
        for (std::size_t lane = 0U;
             lane < profile.frame_input.transform_qwords[row].lanes.size();
             ++lane) {
            const auto word =
                profile.frame_input.transform_qwords[row].lanes[lane];
            expect(word.bits == (row == lane ? 0x3f800000U : 0U),
                   "the SceneBlock frame is not identity");
            expect(word.known_mask ==
                       std::numeric_limits<std::uint32_t>::max(),
                   "the SceneBlock frame contains unknown transform bits");
        }
    }
}

void test_validation_accepts_the_default_synthetic_job_without_io() {
    const auto request = make_synthetic_request();
    const auto limits =
        openrc::runtime::make_level_scene_recovery_limits_v1();
    const auto profile =
        openrc::runtime::make_level_scene_recovery_profile_v1();
    openrc::runtime::validate_level_scene_recovery_request_v1(
        request, limits, profile);
}

void test_validation_rejects_missing_sources() {
    auto request = make_synthetic_request();
    const auto limits =
        openrc::runtime::make_level_scene_recovery_limits_v1();
    const auto profile =
        openrc::runtime::make_level_scene_recovery_profile_v1();

    request.disc_image.clear();
    expect_runtime_error(
        [&] {
            openrc::runtime::validate_level_scene_recovery_request_v1(
                request, limits, profile);
        },
        "an empty disc path was accepted");
    request = make_synthetic_request();
    request.boot_executable.clear();
    expect_runtime_error(
        [&] {
            openrc::runtime::validate_level_scene_recovery_request_v1(
                request, limits, profile);
        },
        "an empty boot-executable path was accepted");
}

void test_validation_rejects_unbounded_or_unknown_policy() {
    const auto request = make_synthetic_request();
    const auto default_limits =
        openrc::runtime::make_level_scene_recovery_limits_v1();
    const auto default_profile =
        openrc::runtime::make_level_scene_recovery_profile_v1();

    auto limits = default_limits;
    limits.aggregate_geometry.max_vertices = 0U;
    expect_runtime_error(
        [&] {
            openrc::runtime::validate_level_scene_recovery_request_v1(
                request, limits, default_profile);
        },
        "an unbounded aggregate profile was accepted");

    auto invalid_request = request;
    invalid_request.record_selection =
        static_cast<openrc::runtime::LevelSceneRecordSelectionV1>(255U);
    expect_runtime_error(
        [&] {
            openrc::runtime::validate_level_scene_recovery_request_v1(
                invalid_request, default_limits, default_profile);
        },
        "an unknown record-selection policy was accepted");

    auto profile = default_profile;
    profile.moby_coordinate_domain =
        static_cast<openrc::runtime::MobySceneCoordinateDomainV1>(255U);
    expect_runtime_error(
        [&] {
            openrc::runtime::validate_level_scene_recovery_request_v1(
                request, default_limits, profile);
        },
        "an unknown coordinate-domain profile was accepted");
}

} // namespace

int main() {
    try {
        test_default_limits_and_profile_are_explicitly_bounded();
        test_validation_accepts_the_default_synthetic_job_without_io();
        test_validation_rejects_missing_sources();
        test_validation_rejects_unbounded_or_unknown_policy();
    } catch (const std::exception& error) {
        return error.what()[0] == '\0' ? 2 : 1;
    }
    return 0;
}
