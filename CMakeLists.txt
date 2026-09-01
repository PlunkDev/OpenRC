cmake_minimum_required(VERSION 3.24)

project(
    OpenRC
    VERSION 0.1.0
    DESCRIPTION "A native reimplementation project for Ratchet & Clank (2002)"
    LANGUAGES CXX
)

option(OPENRC_BUILD_TESTS "Build OpenRC tests" ON)
option(OPENRC_BUILD_LAUNCHER "Build the graphical launcher" ON)
option(OPENRC_BUILD_RUNTIME "Build the native Windows runtime" ON)
option(
    OPENRC_STATIC_MINGW_RUNTIME
    "Statically link the MinGW C++ and unwind runtimes into executables"
    ON
)

add_library(openrc_core STATIC
    src/core/boundary_table.cpp
    src/core/companion_wad_index.cpp
    src/core/disc.cpp
    src/core/disc_toc.cpp
    src/core/dvp_vu.cpp
    src/core/dvp_vu_execute.cpp
    src/core/ee_r5900_boundary.cpp
    src/core/elf.cpp
    src/core/gif_gs.cpp
    src/core/hash.cpp
    src/core/iso9660.cpp
    src/core/localized_subtitle_bank.cpp
    src/core/map_art.cpp
    src/core/pcm_wav.cpp
    src/core/paths.cpp
    src/core/preparation.cpp
    src/core/ps_adpcm.cpp
    src/core/ps2_save_bundle.cpp
    src/core/rac_gameplay_bank.cpp
    src/core/rac_level_core.cpp
    src/core/rac_level_moby_assets.cpp
    src/core/rac_level_moby_texture.cpp
    src/core/rac_moby_class.cpp
    src/core/rac_moby_model_geometry.cpp
    src/core/rac_moby_packet_geometry.cpp
    src/core/scene_animation_bank.cpp
    src/core/scene_block_directory.cpp
    src/core/scene_block_geometry.cpp
    src/core/scene_block_runtime.cpp
    src/core/scene_block_task.cpp
    src/core/scene_block_task_execute.cpp
    src/core/scene_block_vif.cpp
    src/core/scene_block_vu.cpp
    src/core/scene_block_vu_phase.cpp
    src/core/sblk.cpp
    src/core/sblk_audio.cpp
    src/core/sblk_wav.cpp
    src/core/settings.cpp
    src/core/two_fip.cpp
    src/core/vagp.cpp
    src/core/wad.cpp
    src/core/wad_bundle.cpp
    src/core/wad_payload_family.cpp
    src/core/wad_payload_family_tsv.cpp
    src/core/wad_payload_inventory.cpp
    src/core/wad_payload_probes.cpp
    src/core/wad_payload_structure.cpp
    src/core/wad_payload_tsv.cpp
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

add_executable(openrc-cli
    src/cli/main.cpp
    src/runtime/moby_scene_geometry.cpp
)
target_include_directories(openrc-cli
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
)
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

if(OPENRC_BUILD_RUNTIME AND WIN32)
    add_executable(openrc-runtime WIN32
        src/runtime/windows_main.cpp
        src/runtime/d3d11_renderer.cpp
        src/runtime/moby_scene_geometry.cpp
        src/runtime/scene_geometry.cpp
        src/runtime/openrc-runtime.manifest
    )
    target_compile_definitions(openrc-runtime PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(openrc-runtime PRIVATE
        openrc_core
        d3d11
        dxgi
        shell32
        user32
        gdi32
    )
    target_compile_features(openrc-runtime PRIVATE cxx_std_20)
    if(MINGW)
        target_link_options(openrc-runtime PRIVATE -municode)
    endif()
    if(MSVC)
        target_compile_options(openrc-runtime PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(openrc-runtime PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(OPENRC_BUILD_TESTS)
    include(CTest)
    add_executable(
        openrc-localized-subtitle-bank-tests
        tests/localized_subtitle_bank_tests.cpp
    )
    target_link_libraries(
        openrc-localized-subtitle-bank-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-localized-subtitle-bank-tests
        PRIVATE
        cxx_std_20
    )
    add_executable(openrc-disc-tests tests/disc_tests.cpp)
    target_link_libraries(openrc-disc-tests PRIVATE openrc_core)
    target_compile_features(openrc-disc-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-rac-gameplay-bank-tests
        tests/rac_gameplay_bank_tests.cpp
    )
    target_link_libraries(
        openrc-rac-gameplay-bank-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-gameplay-bank-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-level-core-tests
        tests/rac_level_core_tests.cpp
    )
    target_link_libraries(
        openrc-rac-level-core-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-level-core-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-level-moby-texture-tests
        tests/rac_level_moby_texture_tests.cpp
    )
    target_link_libraries(
        openrc-rac-level-moby-texture-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-level-moby-texture-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-moby-class-tests
        tests/rac_moby_class_tests.cpp
    )
    target_link_libraries(
        openrc-rac-moby-class-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-moby-class-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-moby-packet-geometry-tests
        tests/rac_moby_packet_geometry_tests.cpp
    )
    target_link_libraries(
        openrc-rac-moby-packet-geometry-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-moby-packet-geometry-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-moby-model-geometry-tests
        tests/rac_moby_model_geometry_tests.cpp
    )
    target_link_libraries(
        openrc-rac-moby-model-geometry-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-moby-model-geometry-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-scene-animation-bank-tests
        tests/scene_animation_bank_tests.cpp
    )
    target_link_libraries(
        openrc-scene-animation-bank-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-scene-animation-bank-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(openrc-disc-toc-tests tests/disc_toc_tests.cpp)
    target_link_libraries(openrc-disc-toc-tests PRIVATE openrc_core)
    target_compile_features(openrc-disc-toc-tests PRIVATE cxx_std_20)

    add_executable(openrc-wad-tests tests/wad_tests.cpp)
    target_link_libraries(openrc-wad-tests PRIVATE openrc_core)
    target_compile_features(openrc-wad-tests PRIVATE cxx_std_20)

    add_executable(openrc-wad-bundle-tests tests/wad_bundle_tests.cpp)
    target_link_libraries(openrc-wad-bundle-tests PRIVATE openrc_core)
    target_compile_features(openrc-wad-bundle-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-wad-payload-inventory-tests
        tests/wad_payload_inventory_tests.cpp
    )
    target_link_libraries(
        openrc-wad-payload-inventory-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-wad-payload-inventory-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-wad-payload-probes-tests
        tests/wad_payload_probes_tests.cpp
    )
    target_link_libraries(
        openrc-wad-payload-probes-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-wad-payload-probes-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-wad-payload-tsv-tests
        tests/wad_payload_tsv_tests.cpp
    )
    target_link_libraries(
        openrc-wad-payload-tsv-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-wad-payload-tsv-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-wad-payload-structure-tests
        tests/wad_payload_structure_tests.cpp
    )
    target_link_libraries(
        openrc-wad-payload-structure-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-wad-payload-structure-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-wad-payload-family-tests
        tests/wad_payload_family_tests.cpp
    )
    target_link_libraries(
        openrc-wad-payload-family-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-wad-payload-family-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-wad-payload-family-tsv-tests
        tests/wad_payload_family_tsv_tests.cpp
    )
    target_link_libraries(
        openrc-wad-payload-family-tsv-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-wad-payload-family-tsv-tests
        PRIVATE
        cxx_std_20
    )

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

    add_executable(openrc-sblk-wav-tests tests/sblk_wav_tests.cpp)
    target_link_libraries(openrc-sblk-wav-tests PRIVATE openrc_core)
    target_compile_features(openrc-sblk-wav-tests PRIVATE cxx_std_20)

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
        openrc-scene-block-task-tests
        tests/scene_block_task_tests.cpp
    )
    target_link_libraries(openrc-scene-block-task-tests PRIVATE openrc_core)
    target_compile_features(openrc-scene-block-task-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-scene-block-task-execute-tests
        tests/scene_block_task_execute_tests.cpp
    )
    target_link_libraries(openrc-scene-block-task-execute-tests PRIVATE openrc_core)
    target_compile_features(openrc-scene-block-task-execute-tests PRIVATE cxx_std_20)

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
        target_compile_options(openrc-localized-subtitle-bank-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-gameplay-bank-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-level-core-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-level-moby-texture-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-moby-class-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-moby-model-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-moby-packet-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-animation-bank-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-disc-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-disc-toc-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-bundle-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-payload-inventory-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-payload-probes-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-payload-tsv-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-payload-structure-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-payload-family-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-wad-payload-family-tsv-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-two-fip-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-boundary-table-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-map-art-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-ps2-save-bundle-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-ps-adpcm-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-sblk-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-sblk-audio-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-sblk-wav-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-vagp-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-directory-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-task-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-task-execute-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-vif-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-vu-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-vu-phase-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-companion-wad-index-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(openrc-localized-subtitle-bank-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-gameplay-bank-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-level-core-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-level-moby-texture-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-moby-class-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-moby-model-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-moby-packet-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-animation-bank-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-disc-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-disc-toc-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-bundle-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-payload-inventory-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-payload-probes-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-payload-tsv-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-payload-structure-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-payload-family-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-wad-payload-family-tsv-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-two-fip-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-boundary-table-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-map-art-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-ps2-save-bundle-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-ps-adpcm-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-sblk-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-sblk-audio-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-sblk-wav-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-vagp-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-directory-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-task-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-task-execute-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-vif-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-vu-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-vu-phase-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-companion-wad-index-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME localized-subtitle-bank COMMAND openrc-localized-subtitle-bank-tests)
    add_test(NAME rac-gameplay-bank COMMAND openrc-rac-gameplay-bank-tests)
    add_test(NAME rac-level-core COMMAND openrc-rac-level-core-tests)
    add_test(NAME rac-level-moby-texture COMMAND openrc-rac-level-moby-texture-tests)
    add_test(NAME rac-moby-class COMMAND openrc-rac-moby-class-tests)
    add_test(NAME rac-moby-model-geometry COMMAND openrc-rac-moby-model-geometry-tests)
    add_test(NAME rac-moby-packet-geometry COMMAND openrc-rac-moby-packet-geometry-tests)
    add_test(NAME scene-animation-bank COMMAND openrc-scene-animation-bank-tests)
    add_test(NAME disc-inspector COMMAND openrc-disc-tests)
    add_test(NAME disc-toc COMMAND openrc-disc-toc-tests)
    add_test(NAME wad COMMAND openrc-wad-tests)
    add_test(NAME wad-bundle COMMAND openrc-wad-bundle-tests)
    add_test(NAME wad-payload-inventory COMMAND openrc-wad-payload-inventory-tests)
    add_test(NAME wad-payload-probes COMMAND openrc-wad-payload-probes-tests)
    add_test(NAME wad-payload-tsv COMMAND openrc-wad-payload-tsv-tests)
    add_test(NAME wad-payload-structure COMMAND openrc-wad-payload-structure-tests)
    add_test(NAME wad-payload-family COMMAND openrc-wad-payload-family-tests)
    add_test(NAME wad-payload-family-tsv COMMAND openrc-wad-payload-family-tsv-tests)
    add_test(NAME two-fip COMMAND openrc-two-fip-tests)
    add_test(NAME boundary-table COMMAND openrc-boundary-table-tests)
    add_test(NAME map-art COMMAND openrc-map-art-tests)
    add_test(NAME ps2-save-bundle COMMAND openrc-ps2-save-bundle-tests)
    add_test(NAME ps-adpcm COMMAND openrc-ps-adpcm-tests)
    add_test(NAME sblk COMMAND openrc-sblk-tests)
    add_test(NAME sblk-audio COMMAND openrc-sblk-audio-tests)
    add_test(NAME sblk-wav COMMAND openrc-sblk-wav-tests)
    add_test(NAME vagp COMMAND openrc-vagp-tests)
    add_test(NAME scene-block-directory COMMAND openrc-scene-block-directory-tests)
    add_test(NAME scene-block-task COMMAND openrc-scene-block-task-tests)
    add_test(NAME scene-block-task-execute COMMAND openrc-scene-block-task-execute-tests)
    add_test(NAME scene-block-vif COMMAND openrc-scene-block-vif-tests)
    add_test(NAME scene-block-vu COMMAND openrc-scene-block-vu-tests)
    add_test(NAME scene-block-vu-phase COMMAND openrc-scene-block-vu-phase-tests)
    add_test(NAME companion-wad-index COMMAND openrc-companion-wad-index-tests)

    add_executable(openrc-elf-tests tests/elf_tests.cpp)
    target_link_libraries(openrc-elf-tests PRIVATE openrc_core)
    target_compile_features(openrc-elf-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-ee-r5900-boundary-tests
        tests/ee_r5900_boundary_tests.cpp
    )
    target_link_libraries(
        openrc-ee-r5900-boundary-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-ee-r5900-boundary-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(openrc-dvp-vu-tests tests/dvp_vu_tests.cpp)
    target_link_libraries(openrc-dvp-vu-tests PRIVATE openrc_core)
    target_compile_features(openrc-dvp-vu-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-dvp-vu-execute-tests
        tests/dvp_vu_execute_tests.cpp
    )
    target_link_libraries(openrc-dvp-vu-execute-tests PRIVATE openrc_core)
    target_compile_features(openrc-dvp-vu-execute-tests PRIVATE cxx_std_20)

    add_executable(openrc-gif-gs-tests tests/gif_gs_tests.cpp)
    target_link_libraries(openrc-gif-gs-tests PRIVATE openrc_core)
    target_compile_features(openrc-gif-gs-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-scene-block-geometry-tests
        tests/scene_block_geometry_tests.cpp
    )
    target_link_libraries(openrc-scene-block-geometry-tests PRIVATE openrc_core)
    target_compile_features(
        openrc-scene-block-geometry-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-scene-geometry-tests
        tests/scene_geometry_tests.cpp
        src/runtime/scene_geometry.cpp
    )
    target_include_directories(
        openrc-scene-geometry-tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
    )
    target_link_libraries(openrc-scene-geometry-tests PRIVATE openrc_core)
    target_compile_features(openrc-scene-geometry-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-moby-scene-geometry-tests
        tests/moby_scene_geometry_tests.cpp
        src/runtime/moby_scene_geometry.cpp
    )
    target_include_directories(
        openrc-moby-scene-geometry-tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
    )
    target_link_libraries(
        openrc-moby-scene-geometry-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-moby-scene-geometry-tests
        PRIVATE
        cxx_std_20
    )

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
        target_compile_options(openrc-ee-r5900-boundary-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-dvp-vu-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-dvp-vu-execute-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-gif-gs-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-block-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-scene-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-moby-scene-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-hash-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-iso9660-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-preparation-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-settings-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(openrc-elf-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-ee-r5900-boundary-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-dvp-vu-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-dvp-vu-execute-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-gif-gs-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-moby-scene-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-hash-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-iso9660-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-preparation-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-settings-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME elf-inspector COMMAND openrc-elf-tests)
    add_test(NAME ee-r5900-boundary COMMAND openrc-ee-r5900-boundary-tests)
    add_test(NAME dvp-vu COMMAND openrc-dvp-vu-tests)
    add_test(NAME dvp-vu-execute COMMAND openrc-dvp-vu-execute-tests)
    add_test(NAME gif-gs COMMAND openrc-gif-gs-tests)
    add_test(NAME scene-block-geometry COMMAND openrc-scene-block-geometry-tests)
    add_test(NAME scene-geometry COMMAND openrc-scene-geometry-tests)
    add_test(NAME moby-scene-geometry COMMAND openrc-moby-scene-geometry-tests)
    add_test(NAME sha256 COMMAND openrc-hash-tests)
    add_test(NAME iso9660-reader COMMAND openrc-iso9660-tests)
    add_test(NAME preparation COMMAND openrc-preparation-tests)
    add_test(NAME settings-migration COMMAND openrc-settings-tests)
endif()

# LLVM-MinGW otherwise leaves every executable dependent on libc++.dll and
# libunwind.dll from the compiler directory. Keeping the runtime static makes
# local builds and the launcher directly runnable without modifying PATH or
# distributing architecture-specific compiler DLLs beside each executable.
if(MINGW AND OPENRC_STATIC_MINGW_RUNTIME)
    get_property(
        openrc_directory_targets
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        PROPERTY BUILDSYSTEM_TARGETS
    )
    foreach(openrc_target IN LISTS openrc_directory_targets)
        get_target_property(openrc_target_type "${openrc_target}" TYPE)
        if(openrc_target_type STREQUAL "EXECUTABLE")
            target_link_options("${openrc_target}" PRIVATE -static)
        endif()
    endforeach()
endif()
