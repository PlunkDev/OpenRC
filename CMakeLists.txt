cmake_minimum_required(VERSION 3.24)

project(
    OpenRC
    VERSION 0.1.0
    DESCRIPTION "A native reimplementation project for Ratchet & Clank (2002)"
    LANGUAGES CXX
)

option(OPENRC_BUILD_TESTS "Build OpenRC tests" ON)
option(OPENRC_BUILD_LAUNCHER "Build the graphical launcher" ON)

add_library(openrc_core STATIC
    src/core/boundary_table.cpp
    src/core/companion_wad_index.cpp
    src/core/disc.cpp
    src/core/disc_toc.cpp
    src/core/dvp_vu.cpp
    src/core/dvp_vu_execute.cpp
    src/core/elf.cpp
    src/core/hash.cpp
    src/core/iso9660.cpp
    src/core/map_art.cpp
    src/core/paths.cpp
    src/core/preparation.cpp
    src/core/ps_adpcm.cpp
    src/core/ps2_save_bundle.cpp
    src/core/scene_block_directory.cpp
    src/core/scene_block_vif.cpp
    src/core/scene_block_vu.cpp
    src/core/scene_block_vu_phase.cpp
    src/core/sblk.cpp
    src/core/sblk_audio.cpp
    src/core/settings.cpp
    src/core/two_fip.cpp
    src/core/vagp.cpp
    src/core/wad.cpp
    src/core/wad_bundle.cpp
)

target_include_directories(openrc_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_features(openrc_core PUBLIC cxx_std_20)
target_compile_definitions(openrc_core
    PUBLIC
        OPENRC_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(openrc_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(openrc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(openrc-cli src/cli/main.cpp)
target_link_libraries(openrc-cli PRIVATE openrc_core)
target_compile_features(openrc-cli PRIVATE cxx_std_20)

if(MINGW)
    target_link_options(openrc-cli PRIVATE -municode)
endif()

if(MSVC)
    target_compile_options(openrc-cli PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(openrc-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OPENRC_BUILD_LAUNCHER)
    if(WIN32)
        add_executable(openrc-launcher WIN32
            src/launcher/windows_main.cpp
            src/launcher/openrc-launcher.manifest
        )
        target_compile_definitions(openrc-launcher PRIVATE UNICODE _UNICODE NOMINMAX)
        target_link_libraries(openrc-launcher PRIVATE openrc_core comdlg32 shell32)
        if(MINGW)
            target_link_options(openrc-launcher PRIVATE -municode)
        endif()
    else()
        add_executable(openrc-launcher src/launcher/stub_main.cpp)
        target_link_libraries(openrc-launcher PRIVATE openrc_core)
    endif()

    target_compile_features(openrc-launcher PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(openrc-launcher PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(openrc-launcher PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(OPENRC_BUILD_TESTS)
    include(CTest)
    add_executable(openrc-disc-tests tests/disc_tests.cpp)
    target_link_libraries(openrc-disc-tests PRIVATE openrc_core)
    target_compile_features(openrc-disc-tests PRIVATE cxx_std_20)

    add_executable(openrc-disc-toc-tests tests/disc_toc_tests.cpp)
    target_link_libraries(openrc-disc-toc-tests PRIVATE openrc_core)
    target_compile_features(openrc-disc-toc-tests PRIVATE cxx_std_20)

    add_executable(openrc-wad-tests tests/wad_tests.cpp)
    target_link_libraries(openrc-wad-tests PRIVATE openrc_core)
    target_compile_features(openrc-wad-tests PRIVATE cxx_std_20)

    add_executable(openrc-wad-bundle-tests tests/wad_bundle_tests.cpp)
    target_link_libraries(openrc-wad-bundle-tests PRIVATE openrc_core)
    target_compile_features(openrc-wad-bundle-tests PRIVATE cxx_std_20)

    add_executable(openrc-two-fip-tests tests/two_fip_tests.cpp)
    target_link_libraries(openrc-two-fip-tests PRIVATE openrc_core)
    target_compile_features(openrc-two-fip-tests PRIVATE cxx_std_20)

    add_executable(openrc-boundary-table-tests tests/boundary_table_tests.cpp)
    target_link_libraries(openrc-boundary-table-tests PRIVATE openrc_core)
    target_compile_features(openrc-boundary-table-tests PRIVATE cxx_std_20)

    add_executable(openrc-map-art-tests tests/map_art_tests.cpp)
    target_link_libraries(openrc-map-art-tests PRIVATE openrc_core)
    target_compile_features(openrc-map-art-tests PRIVATE cxx_std_20)

    add_executable(openrc-ps2-save-bundle-tests tests/ps2_save_bundle_tests.cpp)
    target_link_libraries(openrc-ps2-save-bundle-tests PRIVATE openrc_core)
    target_compile_features(openrc-ps2-save-bundle-tests PRIVATE cxx_std_20)

    add_executable(openrc-ps-adpcm-tests tests/ps_adpcm_tests.cpp)
    target_link_libraries(openrc-ps-adpcm-tests PRIVATE openrc_core)
    target_compile_features(openrc-ps-adpcm-tests PRIVATE cxx_std_20)

    add_executable(openrc-sblk-tests tests/sblk_tests.cpp)
    target_link_libraries(openrc-sblk-tests PRIVATE openrc_core)
    target_compile_features(openrc-sblk-tests PRIVATE cxx_std_20)

    add_executable(openrc-sblk-audio-tests tests/sblk_audio_tests.cpp)
    target_link_libraries(openrc-sblk-audio-tests PRIVATE openrc_core)
    target_compile_features(openrc-sblk-audio-tests PRIVATE cxx_std_20)

    add_executable(openrc-vagp-tests tests/vagp_tests.cpp)
    target_link_libraries(openrc-vagp-tests PRIVATE openrc_core)
    target_compile_features(openrc-vagp-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-scene-block-directory-tests
        tests/scene_block_directory_tests.cpp
    )
    target_link_libraries(openrc-scene-block-directory-tests PRIVATE openrc_core)
    target_compile_features(openrc-scene-block-directory-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-scene-block-vif-tests
        tests/scene_block_vif_tests.cpp
    )
    target_link_libraries(openrc-scene-block-vif-tests PRIVATE openrc_core)
    target_compile_features(openrc-scene-block-vif-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-scene-block-vu-tests
        tests/scene_block_vu_tests.cpp
    )
    target_link_libraries(openrc-scene-block-vu-tests PRIVATE openrc_core)
    target_compile_features(openrc-scene-block-vu-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-scene-block-vu-phase-tests
        tests/scene_block_vu_phase_tests.cpp
    )
    target_link_libraries(openrc-scene-block-vu-phase-tests PRIVATE openrc_core)
    target_compile_features(openrc-scene-block-vu-phase-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-companion-wad-index-tests
        tests/companion_wad_index_tests.cpp
    )
    target_link_libraries(openrc-companion-wad-index-tests PRIVATE openrc_core)
    target_compile_features(openrc-companion-wad-index-tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(openrc-disc-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-disc-toc-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-bundle-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-two-fip-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-boundary-table-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-map-art-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-ps2-save-bundle-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-ps-adpcm-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-sblk-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-sblk-audio-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-vagp-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-directory-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-vif-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-vu-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-vu-phase-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-companion-wad-index-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(openrc-disc-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-disc-toc-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-bundle-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-two-fip-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-boundary-table-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-map-art-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-ps2-save-bundle-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-ps-adpcm-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-sblk-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-sblk-audio-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-vagp-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-directory-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-vif-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-vu-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-vu-phase-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-companion-wad-index-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME disc-inspector COMMAND openrc-disc-tests)
    add_test(NAME disc-toc COMMAND openrc-disc-toc-tests)
    add_test(NAME wad COMMAND openrc-wad-tests)
    add_test(NAME wad-bundle COMMAND openrc-wad-bundle-tests)
    add_test(NAME two-fip COMMAND openrc-two-fip-tests)
    add_test(NAME boundary-table COMMAND openrc-boundary-table-tests)
    add_test(NAME map-art COMMAND openrc-map-art-tests)
    add_test(NAME ps2-save-bundle COMMAND openrc-ps2-save-bundle-tests)
    add_test(NAME ps-adpcm COMMAND openrc-ps-adpcm-tests)
    add_test(NAME sblk COMMAND openrc-sblk-tests)
    add_test(NAME sblk-audio COMMAND openrc-sblk-audio-tests)
    add_test(NAME vagp COMMAND openrc-vagp-tests)
    add_test(NAME scene-block-directory COMMAND openrc-scene-block-directory-tests)
    add_test(NAME scene-block-vif COMMAND openrc-scene-block-vif-tests)
    add_test(NAME scene-block-vu COMMAND openrc-scene-block-vu-tests)
    add_test(NAME scene-block-vu-phase COMMAND openrc-scene-block-vu-phase-tests)
    add_test(NAME companion-wad-index COMMAND openrc-companion-wad-index-tests)

    add_executable(openrc-elf-tests tests/elf_tests.cpp)
    target_link_libraries(openrc-elf-tests PRIVATE openrc_core)
    target_compile_features(openrc-elf-tests PRIVATE cxx_std_20)

    add_executable(openrc-dvp-vu-tests tests/dvp_vu_tests.cpp)
    target_link_libraries(openrc-dvp-vu-tests PRIVATE openrc_core)
    target_compile_features(openrc-dvp-vu-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-dvp-vu-execute-tests
        tests/dvp_vu_execute_tests.cpp
    )
    target_link_libraries(openrc-dvp-vu-execute-tests PRIVATE openrc_core)
    target_compile_features(openrc-dvp-vu-execute-tests PRIVATE cxx_std_20)

    add_executable(openrc-hash-tests tests/hash_tests.cpp)
    target_link_libraries(openrc-hash-tests PRIVATE openrc_core)
    target_compile_features(openrc-hash-tests PRIVATE cxx_std_20)

    add_executable(openrc-iso9660-tests tests/iso9660_tests.cpp)
    target_link_libraries(openrc-iso9660-tests PRIVATE openrc_core)
    target_compile_features(openrc-iso9660-tests PRIVATE cxx_std_20)

    add_executable(openrc-preparation-tests tests/preparation_tests.cpp)
    target_link_libraries(openrc-preparation-tests PRIVATE openrc_core)
    target_compile_features(openrc-preparation-tests PRIVATE cxx_std_20)

    add_executable(openrc-settings-tests tests/settings_tests.cpp)
    target_link_libraries(openrc-settings-tests PRIVATE openrc_core)
    target_compile_features(openrc-settings-tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(openrc-elf-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-dvp-vu-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-dvp-vu-execute-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-hash-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-iso9660-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-preparation-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-settings-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(openrc-elf-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-dvp-vu-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-dvp-vu-execute-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-hash-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-iso9660-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-preparation-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-settings-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME elf-inspector COMMAND openrc-elf-tests)
    add_test(NAME dvp-vu COMMAND openrc-dvp-vu-tests)
    add_test(NAME dvp-vu-execute COMMAND openrc-dvp-vu-execute-tests)
    add_test(NAME sha256 COMMAND openrc-hash-tests)
    add_test(NAME iso9660-reader COMMAND openrc-iso9660-tests)
    add_test(NAME preparation COMMAND openrc-preparation-tests)
    add_test(NAME settings-migration COMMAND openrc-settings-tests)
endif()
