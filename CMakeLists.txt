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
    "Also statically link MinGW compiler runtimes into test executables"
    ON
)

set(OPENRC_PUBLIC_EXECUTABLE_TARGETS)

add_library(openrc_core STATIC
    src/core/actor_animation.cpp
    src/core/actor_animation_io.cpp
    src/core/actor_animation_player.cpp
    src/core/actor_library.cpp
    src/core/actor_library_io.cpp
    src/core/actor_pose.cpp
    src/core/actor_render_bake.cpp
    src/core/boundary_table.cpp
    src/core/character_controller.cpp
    src/core/collision_world.cpp
    src/core/collision_world_io.cpp
    src/core/companion_wad_index.cpp
    src/core/disc.cpp
    src/core/disc_toc.cpp
    src/core/destructible_scene.cpp
    src/core/destructible_scene_io.cpp
    src/core/dvp_vu.cpp
    src/core/dvp_vu_execute.cpp
    src/core/ee_r5900_boundary.cpp
    src/core/elf.cpp
    src/core/entity_scene.cpp
    src/core/entity_scene_io.cpp
    src/core/fixed_step.cpp
    src/core/game_input.cpp
    src/core/gameplay_scene.cpp
    src/core/gameplay_scene_io.cpp
    src/core/game_world.cpp
    src/core/gif_gs.cpp
    src/core/hash.cpp
    src/core/iso9660.cpp
    src/core/launcher_plan.cpp
    src/core/level_actor_animation_compile.cpp
    src/core/level_actor_library_compile.cpp
    src/core/level_destructible_scene_compile.cpp
    src/core/level_entity_scene_compile.cpp
    src/core/level_gameplay_scene_compile.cpp
    src/core/level_render_scene_compile.cpp
    src/core/level_bootstrap.cpp
    src/core/localized_subtitle_bank.cpp
    src/core/map_art.cpp
    src/core/pcm_wav.cpp
    src/core/paths.cpp
    src/core/player_combat.cpp
    src/core/player_simulation.cpp
    src/core/prepared_game_v2.cpp
    src/core/prepared_game_v2_fs.cpp
    src/core/prepared_game_v2_publish.cpp
    src/core/preparation.cpp
    src/core/ps_adpcm.cpp
    src/core/ps2_save_bundle.cpp
    src/core/rac_gameplay_bank.cpp
    src/core/rac_actor_library_compile.cpp
    src/core/rac_collectible_scene_compile.cpp
    src/core/rac_destructible_scene_compile.cpp
    src/core/rac_level_bootstrap_compile.cpp
    src/core/rac_level_collision.cpp
    src/core/rac_level_collision_compile.cpp
    src/core/rac_level_foundation_compile.cpp
    src/core/rac_level_core.cpp
    src/core/rac_level_moby_assets.cpp
    src/core/rac_level_moby_texture.cpp
    src/core/rac_moby_class.cpp
    src/core/rac_moby_bind_pose.cpp
    src/core/rac_moby_model_geometry.cpp
    src/core/rac_moby_packet_geometry.cpp
    src/core/rac_pad_input.cpp
    src/core/rac_ratchet_animation_compile.cpp
    src/core/rac_ratchet_pose.cpp
    src/core/rac_ratchet_sequence.cpp
    src/core/rac_tie_class.cpp
    src/core/render_scene.cpp
    src/core/render_scene_io.cpp
    src/core/runtime_level_content.cpp
    src/core/runtime_level_foundation.cpp
    src/core/runtime_gameplay.cpp
    src/core/runtime_gameplay_scene.cpp
    src/core/runtime_gameplay_scene_resource.cpp
    src/core/runtime_destructible_scene_resource.cpp
    src/core/runtime_actor_animation.cpp
    src/core/runtime_actor_library.cpp
    src/core/runtime_entity_scene.cpp
    src/core/runtime_player_actor.cpp
    src/core/runtime_player_animation.cpp
    src/core/runtime_render_scene.cpp
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
    src/core/third_person_camera.cpp
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

add_library(openrc_level_scene_recovery STATIC
    src/runtime/level_scene_recovery.cpp
    src/runtime/level_scene_render_compile.cpp
    src/runtime/moby_scene_geometry.cpp
    src/runtime/scene_geometry.cpp
    src/runtime/tie_scene_geometry.cpp
)
target_include_directories(openrc_level_scene_recovery
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
)
target_link_libraries(openrc_level_scene_recovery PUBLIC openrc_core)
target_compile_features(openrc_level_scene_recovery PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        openrc_level_scene_recovery
        PRIVATE
        /W4 /permissive- /utf-8
    )
else()
    target_compile_options(
        openrc_level_scene_recovery
        PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

add_library(openrc_native_game_prepare STATIC
    src/compiler/native_game_prepare.cpp
)
target_include_directories(openrc_native_game_prepare
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
)
target_link_libraries(
    openrc_native_game_prepare
    PUBLIC
        openrc_level_scene_recovery
)
target_compile_features(openrc_native_game_prepare PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        openrc_native_game_prepare
        PRIVATE
        /W4 /permissive- /utf-8
    )
else()
    target_compile_options(
        openrc_native_game_prepare
        PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

add_library(openrc_render_scene_d3d_data STATIC
    src/runtime/render_scene_d3d_data.cpp
)
target_include_directories(openrc_render_scene_d3d_data
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
)
target_link_libraries(openrc_render_scene_d3d_data PUBLIC openrc_core)
target_compile_features(openrc_render_scene_d3d_data PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        openrc_render_scene_d3d_data
        PRIVATE
        /W4 /permissive- /utf-8
    )
else()
    target_compile_options(
        openrc_render_scene_d3d_data
        PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

add_executable(openrc-cli
    src/cli/main.cpp
)
list(APPEND OPENRC_PUBLIC_EXECUTABLE_TARGETS openrc-cli)
target_include_directories(openrc-cli
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
)
target_link_libraries(
    openrc-cli
    PRIVATE
        openrc_level_scene_recovery
        openrc_native_game_prepare
)
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
            src/launcher/portable_executable.cpp
            src/launcher/openrc-launcher.manifest
        )
        target_compile_definitions(openrc-launcher PRIVATE UNICODE _UNICODE NOMINMAX)
        target_link_libraries(
            openrc-launcher
            PRIVATE
                openrc_native_game_prepare
                comdlg32
                shell32
        )
        if(MINGW)
            target_link_options(openrc-launcher PRIVATE -municode)
        endif()
    else()
        add_executable(openrc-launcher src/launcher/stub_main.cpp)
        target_link_libraries(openrc-launcher PRIVATE openrc_core)
    endif()

    list(APPEND OPENRC_PUBLIC_EXECUTABLE_TARGETS openrc-launcher)

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
        src/runtime/windows_gamepad.cpp
        src/runtime/d3d11_renderer.cpp
        src/runtime/openrc-runtime.manifest
    )
    list(APPEND OPENRC_PUBLIC_EXECUTABLE_TARGETS openrc-runtime)
    target_compile_definitions(openrc-runtime PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(openrc-runtime PRIVATE
        openrc_core
        openrc_render_scene_d3d_data
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
    if(WIN32)
        add_executable(
            openrc-portable-executable-tests
            tests/portable_executable_tests.cpp
            src/launcher/portable_executable.cpp
        )
        target_include_directories(
            openrc-portable-executable-tests
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher"
        )
        target_compile_features(
            openrc-portable-executable-tests
            PRIVATE
            cxx_std_20
        )
        if(MSVC)
            target_compile_options(
                openrc-portable-executable-tests
                PRIVATE
                /W4 /permissive- /utf-8
            )
        else()
            target_compile_options(
                openrc-portable-executable-tests
                PRIVATE
                -Wall -Wextra -Wpedantic
            )
        endif()
        add_test(
            NAME portable-executable
            COMMAND openrc-portable-executable-tests
        )
    endif()

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
    add_executable(
        openrc-launcher-plan-tests
        tests/launcher_plan_tests.cpp
    )
    target_link_libraries(
        openrc-launcher-plan-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-launcher-plan-tests
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
        openrc-rac-level-collision-tests
        tests/rac_level_collision_tests.cpp
    )
    target_link_libraries(
        openrc-rac-level-collision-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-level-collision-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-collision-world-tests
        tests/collision_world_tests.cpp
    )
    target_link_libraries(openrc-collision-world-tests PRIVATE openrc_core)
    target_compile_features(openrc-collision-world-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-collision-world-io-tests
        tests/collision_world_io_tests.cpp
    )
    target_link_libraries(openrc-collision-world-io-tests PRIVATE openrc_core)
    target_compile_features(openrc-collision-world-io-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-character-controller-tests
        tests/character_controller_tests.cpp
    )
    target_link_libraries(openrc-character-controller-tests PRIVATE openrc_core)
    target_compile_features(openrc-character-controller-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-player-simulation-tests
        tests/player_simulation_tests.cpp
    )
    target_link_libraries(openrc-player-simulation-tests PRIVATE openrc_core)
    target_compile_features(openrc-player-simulation-tests PRIVATE cxx_std_20)

    add_executable(openrc-game-input-tests tests/game_input_tests.cpp)
    target_link_libraries(openrc-game-input-tests PRIVATE openrc_core)
    target_compile_features(openrc-game-input-tests PRIVATE cxx_std_20)

    add_executable(openrc-fixed-step-tests tests/fixed_step_tests.cpp)
    target_link_libraries(openrc-fixed-step-tests PRIVATE openrc_core)
    target_compile_features(openrc-fixed-step-tests PRIVATE cxx_std_20)

    add_executable(openrc-game-world-tests tests/game_world_tests.cpp)
    target_link_libraries(openrc-game-world-tests PRIVATE openrc_core)
    target_compile_features(openrc-game-world-tests PRIVATE cxx_std_20)

    set(OPENRC_DESTRUCTIBLE_TEST_TARGETS
        openrc-player-combat-tests
        openrc-destructible-scene-tests
        openrc-level-destructible-scene-compile-tests
        openrc-rac-destructible-scene-compile-tests
        openrc-runtime-destructible-scene-resource-tests
    )
    add_executable(openrc-player-combat-tests tests/player_combat_tests.cpp)
    add_executable(
        openrc-destructible-scene-tests
        tests/destructible_scene_tests.cpp
    )
    add_executable(
        openrc-level-destructible-scene-compile-tests
        tests/level_destructible_scene_compile_tests.cpp
    )
    add_executable(
        openrc-rac-destructible-scene-compile-tests
        tests/rac_destructible_scene_compile_tests.cpp
    )
    add_executable(
        openrc-runtime-destructible-scene-resource-tests
        tests/runtime_destructible_scene_resource_tests.cpp
    )
    foreach(target IN LISTS OPENRC_DESTRUCTIBLE_TEST_TARGETS)
        target_link_libraries(${target} PRIVATE openrc_core)
        target_compile_features(${target} PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()

    add_executable(
        openrc-runtime-gameplay-tests
        tests/runtime_gameplay_tests.cpp
    )

    add_executable(
        openrc-runtime-gameplay-scene-tests
        tests/runtime_gameplay_scene_tests.cpp
    )

    add_executable(
        openrc-runtime-gameplay-scene-resource-tests
        tests/runtime_gameplay_scene_resource_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-gameplay-scene-resource-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-gameplay-scene-resource-tests
        PRIVATE
        cxx_std_20
    )
    target_link_libraries(
        openrc-runtime-gameplay-scene-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-gameplay-scene-tests
        PRIVATE
        cxx_std_20
    )
    target_link_libraries(
        openrc-runtime-gameplay-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-gameplay-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-third-person-camera-tests
        tests/third_person_camera_tests.cpp
    )
    target_link_libraries(
        openrc-third-person-camera-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-third-person-camera-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-prepared-game-v2-tests
        tests/prepared_game_v2_tests.cpp
    )
    target_link_libraries(openrc-prepared-game-v2-tests PRIVATE openrc_core)
    target_compile_features(openrc-prepared-game-v2-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-prepared-game-v2-fs-tests
        tests/prepared_game_v2_fs_tests.cpp
    )
    target_link_libraries(openrc-prepared-game-v2-fs-tests PRIVATE openrc_core)
    target_compile_features(openrc-prepared-game-v2-fs-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-prepared-game-v2-publish-tests
        tests/prepared_game_v2_publish_tests.cpp
    )
    target_link_libraries(
        openrc-prepared-game-v2-publish-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-prepared-game-v2-publish-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-native-game-prepare-tests
        tests/native_game_prepare_tests.cpp
    )
    target_link_libraries(
        openrc-native-game-prepare-tests
        PRIVATE
        openrc_native_game_prepare
    )
    target_compile_features(
        openrc-native-game-prepare-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-native-game-prepare-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-native-game-prepare-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-level-bootstrap-tests
        tests/level_bootstrap_tests.cpp
    )
    target_link_libraries(openrc-level-bootstrap-tests PRIVATE openrc_core)
    target_compile_features(openrc-level-bootstrap-tests PRIVATE cxx_std_20)

    add_executable(
        openrc-level-render-scene-compile-tests
        tests/level_render_scene_compile_tests.cpp
    )
    target_link_libraries(
        openrc-level-render-scene-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-level-render-scene-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-level-render-scene-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-level-render-scene-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-level-actor-library-compile-tests
        tests/level_actor_library_compile_tests.cpp
    )
    target_link_libraries(
        openrc-level-actor-library-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-level-actor-library-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-level-actor-library-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-level-actor-library-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-level-entity-scene-compile-tests
        tests/level_entity_scene_compile_tests.cpp
    )
    target_link_libraries(
        openrc-level-entity-scene-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-level-entity-scene-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-level-entity-scene-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-level-entity-scene-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-level-gameplay-scene-compile-tests
        tests/level_gameplay_scene_compile_tests.cpp
    )
    target_link_libraries(
        openrc-level-gameplay-scene-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-level-gameplay-scene-compile-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-level-foundation-compile-tests
        tests/rac_level_foundation_compile_tests.cpp
    )
    target_link_libraries(
        openrc-rac-level-foundation-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-level-foundation-compile-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-collectible-scene-compile-tests
        tests/rac_collectible_scene_compile_tests.cpp
    )
    target_link_libraries(
        openrc-rac-collectible-scene-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-collectible-scene-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-rac-collectible-scene-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-rac-collectible-scene-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-runtime-level-foundation-tests
        tests/runtime_level_foundation_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-level-foundation-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-level-foundation-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-render-scene-io-tests
        tests/render_scene_io_tests.cpp
    )
    target_link_libraries(
        openrc-render-scene-io-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-render-scene-io-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-entity-scene-tests
        tests/entity_scene_tests.cpp
    )
    target_link_libraries(
        openrc-entity-scene-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-entity-scene-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-entity-scene-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-entity-scene-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-gameplay-scene-tests
        tests/gameplay_scene_tests.cpp
    )
    target_link_libraries(
        openrc-gameplay-scene-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-gameplay-scene-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-gameplay-scene-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-gameplay-scene-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-actor-library-tests
        tests/actor_library_tests.cpp
    )
    target_link_libraries(
        openrc-actor-library-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-actor-library-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-actor-library-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-actor-library-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-actor-animation-tests
        tests/actor_animation_tests.cpp
    )
    target_link_libraries(
        openrc-actor-animation-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-actor-animation-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-actor-animation-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-actor-animation-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-actor-animation-player-tests
        tests/actor_animation_player_tests.cpp
    )
    target_link_libraries(
        openrc-actor-animation-player-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-actor-animation-player-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-actor-animation-player-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-actor-animation-player-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-level-actor-animation-compile-tests
        tests/level_actor_animation_compile_tests.cpp
    )
    target_link_libraries(
        openrc-level-actor-animation-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-level-actor-animation-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-level-actor-animation-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-level-actor-animation-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-runtime-actor-animation-tests
        tests/runtime_actor_animation_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-actor-animation-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-actor-animation-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-runtime-actor-animation-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-runtime-actor-animation-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-rac-ratchet-animation-compile-tests
        tests/rac_ratchet_animation_compile_tests.cpp
    )
    target_link_libraries(
        openrc-rac-ratchet-animation-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-ratchet-animation-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-rac-ratchet-animation-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-rac-ratchet-animation-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-runtime-player-animation-tests
        tests/runtime_player_animation_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-player-animation-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-player-animation-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-runtime-player-animation-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-runtime-player-animation-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-actor-pose-tests
        tests/actor_pose_tests.cpp
    )
    target_link_libraries(
        openrc-actor-pose-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-actor-pose-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-actor-pose-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-actor-pose-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-actor-render-bake-tests
        tests/actor_render_bake_tests.cpp
    )
    target_link_libraries(
        openrc-actor-render-bake-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-actor-render-bake-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-actor-library-compile-tests
        tests/rac_actor_library_compile_tests.cpp
    )
    target_link_libraries(
        openrc-rac-actor-library-compile-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-actor-library-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-rac-actor-library-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-rac-actor-library-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-runtime-actor-library-tests
        tests/runtime_actor_library_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-actor-library-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-actor-library-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-runtime-actor-library-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-runtime-actor-library-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-runtime-entity-scene-tests
        tests/runtime_entity_scene_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-entity-scene-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-entity-scene-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-runtime-entity-scene-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-runtime-entity-scene-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-runtime-render-scene-tests
        tests/runtime_render_scene_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-render-scene-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-render-scene-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-runtime-level-content-tests
        tests/runtime_level_content_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-level-content-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-level-content-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-runtime-player-actor-tests
        tests/runtime_player_actor_tests.cpp
    )
    target_link_libraries(
        openrc-runtime-player-actor-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-runtime-player-actor-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-runtime-player-actor-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-runtime-player-actor-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-rac-moby-bind-pose-tests
        tests/rac_moby_bind_pose_tests.cpp
    )
    target_link_libraries(
        openrc-rac-moby-bind-pose-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-moby-bind-pose-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-render-scene-d3d-data-tests
        tests/render_scene_d3d_data_tests.cpp
    )
    target_link_libraries(
        openrc-render-scene-d3d-data-tests
        PRIVATE
        openrc_render_scene_d3d_data
    )
    target_compile_features(
        openrc-render-scene-d3d-data-tests
        PRIVATE
        cxx_std_20
    )

    if(MSVC)
        target_compile_options(
            openrc-render-scene-io-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
        target_compile_options(
            openrc-runtime-render-scene-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
        target_compile_options(
            openrc-runtime-level-content-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
        target_compile_options(
            openrc-rac-moby-bind-pose-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
        target_compile_options(
            openrc-render-scene-d3d-data-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-render-scene-io-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(
            openrc-runtime-render-scene-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(
            openrc-runtime-level-content-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(
            openrc-rac-moby-bind-pose-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(
            openrc-render-scene-d3d-data-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_executable(
        openrc-level-scene-recovery-tests
        tests/level_scene_recovery_tests.cpp
    )
    target_link_libraries(
        openrc-level-scene-recovery-tests
        PRIVATE
        openrc_level_scene_recovery
    )
    target_compile_features(
        openrc-level-scene-recovery-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-level-scene-render-compile-tests
        tests/level_scene_render_compile_tests.cpp
    )
    target_link_libraries(
        openrc-level-scene-render-compile-tests
        PRIVATE
        openrc_level_scene_recovery
    )
    target_compile_features(
        openrc-level-scene-render-compile-tests
        PRIVATE
        cxx_std_20
    )
    if(MSVC)
        target_compile_options(
            openrc-level-scene-recovery-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
        target_compile_options(
            openrc-level-scene-render-compile-tests
            PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(
            openrc-level-scene-recovery-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(
            openrc-level-scene-render-compile-tests
            PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

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
        openrc-rac-ratchet-sequence-tests
        tests/rac_ratchet_sequence_tests.cpp
    )
    target_link_libraries(
        openrc-rac-ratchet-sequence-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-ratchet-sequence-tests
        PRIVATE
        cxx_std_20
    )

    add_executable(
        openrc-rac-ratchet-pose-tests
        tests/rac_ratchet_pose_tests.cpp
    )
    target_link_libraries(
        openrc-rac-ratchet-pose-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-ratchet-pose-tests
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
        openrc-rac-tie-class-tests
        tests/rac_tie_class_tests.cpp
    )
    target_link_libraries(
        openrc-rac-tie-class-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-rac-tie-class-tests
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
        target_compile_options(openrc-rac-level-collision-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-collision-world-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-collision-world-io-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-character-controller-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-player-simulation-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-game-input-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-fixed-step-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-game-world-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-runtime-gameplay-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-third-person-camera-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-prepared-game-v2-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-prepared-game-v2-fs-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-prepared-game-v2-publish-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-level-bootstrap-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-level-foundation-compile-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-runtime-level-foundation-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-level-core-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-level-moby-texture-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-moby-class-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-ratchet-pose-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-ratchet-sequence-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-moby-model-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-moby-packet-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-rac-tie-class-tests PRIVATE /W4 /permissive- /utf-8)
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
        target_compile_options(openrc-rac-level-collision-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-collision-world-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-collision-world-io-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-character-controller-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-player-simulation-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-game-input-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-fixed-step-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-game-world-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-runtime-gameplay-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-third-person-camera-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-prepared-game-v2-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-prepared-game-v2-fs-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-prepared-game-v2-publish-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-level-bootstrap-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-level-foundation-compile-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-runtime-level-foundation-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-level-core-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-level-moby-texture-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-moby-class-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-ratchet-pose-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-ratchet-sequence-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-moby-model-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-moby-packet-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-rac-tie-class-tests PRIVATE -Wall -Wextra -Wpedantic)
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
    add_test(NAME rac-level-collision COMMAND openrc-rac-level-collision-tests)
    add_test(NAME collision-world COMMAND openrc-collision-world-tests)
    add_test(NAME collision-world-io COMMAND openrc-collision-world-io-tests)
    add_test(NAME character-controller COMMAND openrc-character-controller-tests)
    add_test(NAME player-simulation COMMAND openrc-player-simulation-tests)
    add_test(NAME game-input COMMAND openrc-game-input-tests)
    add_test(NAME fixed-step COMMAND openrc-fixed-step-tests)
    add_test(NAME game-world COMMAND openrc-game-world-tests)
    add_test(NAME player-combat COMMAND openrc-player-combat-tests)
    add_test(NAME destructible-scene COMMAND openrc-destructible-scene-tests)
    add_test(
        NAME level-destructible-scene-compile
        COMMAND openrc-level-destructible-scene-compile-tests
    )
    add_test(
        NAME rac-destructible-scene-compile
        COMMAND openrc-rac-destructible-scene-compile-tests
    )
    add_test(
        NAME runtime-destructible-scene-resource
        COMMAND openrc-runtime-destructible-scene-resource-tests
    )
    add_test(NAME runtime-gameplay COMMAND openrc-runtime-gameplay-tests)
    add_test(NAME runtime-gameplay-scene COMMAND openrc-runtime-gameplay-scene-tests)
    add_test(NAME runtime-gameplay-scene-resource COMMAND openrc-runtime-gameplay-scene-resource-tests)
    add_test(NAME third-person-camera COMMAND openrc-third-person-camera-tests)
    add_test(NAME prepared-game-v2 COMMAND openrc-prepared-game-v2-tests)
    add_test(NAME prepared-game-v2-fs COMMAND openrc-prepared-game-v2-fs-tests)
    add_test(NAME prepared-game-v2-publish COMMAND openrc-prepared-game-v2-publish-tests)
    add_test(NAME native-game-prepare COMMAND openrc-native-game-prepare-tests)
    add_test(NAME level-bootstrap COMMAND openrc-level-bootstrap-tests)
    add_test(NAME level-render-scene-compile COMMAND openrc-level-render-scene-compile-tests)
    add_test(NAME level-actor-library-compile COMMAND openrc-level-actor-library-compile-tests)
    add_test(NAME level-entity-scene-compile COMMAND openrc-level-entity-scene-compile-tests)
    add_test(NAME level-gameplay-scene-compile COMMAND openrc-level-gameplay-scene-compile-tests)
    add_test(NAME rac-collectible-scene-compile COMMAND openrc-rac-collectible-scene-compile-tests)
    add_test(NAME rac-level-foundation-compile COMMAND openrc-rac-level-foundation-compile-tests)
    add_test(NAME runtime-level-foundation COMMAND openrc-runtime-level-foundation-tests)
    add_test(NAME render-scene-io COMMAND openrc-render-scene-io-tests)
    add_test(NAME entity-scene COMMAND openrc-entity-scene-tests)
    add_test(NAME gameplay-scene COMMAND openrc-gameplay-scene-tests)
    add_test(NAME actor-library COMMAND openrc-actor-library-tests)
    add_test(NAME actor-animation COMMAND openrc-actor-animation-tests)
    add_test(NAME actor-animation-player COMMAND openrc-actor-animation-player-tests)
    add_test(NAME level-actor-animation-compile COMMAND openrc-level-actor-animation-compile-tests)
    add_test(NAME runtime-actor-animation COMMAND openrc-runtime-actor-animation-tests)
    add_test(NAME rac-ratchet-animation-compile COMMAND openrc-rac-ratchet-animation-compile-tests)
    add_test(NAME runtime-player-animation COMMAND openrc-runtime-player-animation-tests)
    add_test(NAME actor-pose COMMAND openrc-actor-pose-tests)
    add_test(NAME actor-render-bake COMMAND openrc-actor-render-bake-tests)
    add_test(NAME rac-actor-library-compile COMMAND openrc-rac-actor-library-compile-tests)
    add_test(NAME runtime-actor-library COMMAND openrc-runtime-actor-library-tests)
    add_test(NAME runtime-entity-scene COMMAND openrc-runtime-entity-scene-tests)
    add_test(NAME runtime-render-scene COMMAND openrc-runtime-render-scene-tests)
    add_test(NAME runtime-level-content COMMAND openrc-runtime-level-content-tests)
    add_test(NAME runtime-player-actor COMMAND openrc-runtime-player-actor-tests)
    add_test(NAME rac-moby-bind-pose COMMAND openrc-rac-moby-bind-pose-tests)
    add_test(NAME render-scene-d3d-data COMMAND openrc-render-scene-d3d-data-tests)
    add_test(NAME level-scene-recovery COMMAND openrc-level-scene-recovery-tests)
    add_test(NAME level-scene-render-compile COMMAND openrc-level-scene-render-compile-tests)
    add_test(NAME rac-level-core COMMAND openrc-rac-level-core-tests)
    add_test(NAME rac-level-moby-texture COMMAND openrc-rac-level-moby-texture-tests)
    add_test(NAME rac-moby-class COMMAND openrc-rac-moby-class-tests)
    add_test(NAME rac-ratchet-pose COMMAND openrc-rac-ratchet-pose-tests)
    add_test(NAME rac-ratchet-sequence COMMAND openrc-rac-ratchet-sequence-tests)
    add_test(NAME rac-moby-model-geometry COMMAND openrc-rac-moby-model-geometry-tests)
    add_test(NAME rac-moby-packet-geometry COMMAND openrc-rac-moby-packet-geometry-tests)
    add_test(NAME rac-tie-class COMMAND openrc-rac-tie-class-tests)
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

    add_executable(
        openrc-tie-scene-geometry-tests
        tests/tie_scene_geometry_tests.cpp
        src/runtime/tie_scene_geometry.cpp
    )
    target_include_directories(
        openrc-tie-scene-geometry-tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
    )
    target_link_libraries(
        openrc-tie-scene-geometry-tests
        PRIVATE
        openrc_core
    )
    target_compile_features(
        openrc-tie-scene-geometry-tests
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
        target_compile_options(openrc-tie-scene-geometry-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-hash-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-iso9660-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-preparation-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-settings-tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(openrc-launcher-plan-tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(openrc-elf-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-ee-r5900-boundary-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-dvp-vu-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-dvp-vu-execute-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-gif-gs-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-block-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-scene-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-moby-scene-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-tie-scene-geometry-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-hash-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-iso9660-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-preparation-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-settings-tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(openrc-launcher-plan-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME elf-inspector COMMAND openrc-elf-tests)
    add_test(NAME ee-r5900-boundary COMMAND openrc-ee-r5900-boundary-tests)
    add_test(NAME dvp-vu COMMAND openrc-dvp-vu-tests)
    add_test(NAME dvp-vu-execute COMMAND openrc-dvp-vu-execute-tests)
    add_test(NAME gif-gs COMMAND openrc-gif-gs-tests)
    add_test(NAME scene-block-geometry COMMAND openrc-scene-block-geometry-tests)
    add_test(NAME scene-geometry COMMAND openrc-scene-geometry-tests)
    add_test(NAME moby-scene-geometry COMMAND openrc-moby-scene-geometry-tests)
    add_test(NAME tie-scene-geometry COMMAND openrc-tie-scene-geometry-tests)
    add_test(NAME sha256 COMMAND openrc-hash-tests)
    add_test(NAME iso9660-reader COMMAND openrc-iso9660-tests)
    add_test(NAME preparation COMMAND openrc-preparation-tests)
    add_test(NAME settings-migration COMMAND openrc-settings-tests)
    add_test(NAME launcher-plan COMMAND openrc-launcher-plan-tests)
endif()

# LLVM-MinGW otherwise leaves executables dependent on libc++.dll and
# libunwind.dll from the compiler directory. Public executables are always
# static and audited after every link. OPENRC_STATIC_MINGW_RUNTIME extends the
# same static and post-link audit policy to test/developer executables.
if(MINGW)
    # This verifier must not depend on the very compiler-support DLLs it is
    # checking for. It is a console-only build tool, deliberately isolated
    # from the application libraries and statically linked on MinGW.
    add_executable(openrc-portable-executable-audit EXCLUDE_FROM_ALL
        src/launcher/portable_executable_audit_main.cpp
        src/launcher/portable_executable.cpp
    )
    target_compile_features(
        openrc-portable-executable-audit
        PRIVATE
        cxx_std_20
    )
    target_compile_options(
        openrc-portable-executable-audit
        PRIVATE
        -Wall -Wextra -Wpedantic
    )
    target_link_options(openrc-portable-executable-audit PRIVATE -static)
    add_custom_command(
        TARGET openrc-portable-executable-audit
        POST_BUILD
        COMMAND "$<TARGET_FILE:openrc-portable-executable-audit>"
            "$<TARGET_FILE:openrc-portable-executable-audit>"
        COMMENT "Verifying portable imports for the portable auditor"
        VERBATIM
    )

    get_property(
        openrc_directory_targets
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        PROPERTY BUILDSYSTEM_TARGETS
    )
    set(openrc_audited_executable_targets)
    foreach(openrc_target IN LISTS openrc_directory_targets)
        if(openrc_target STREQUAL "openrc-portable-executable-audit")
            continue()
        endif()
        get_target_property(openrc_target_type "${openrc_target}" TYPE)
        if(openrc_target_type STREQUAL "EXECUTABLE")
            if(OPENRC_STATIC_MINGW_RUNTIME OR
               openrc_target IN_LIST OPENRC_PUBLIC_EXECUTABLE_TARGETS)
                target_link_options("${openrc_target}" PRIVATE -static)
                list(APPEND openrc_audited_executable_targets "${openrc_target}")
            endif()
        endif()
    endforeach()

    list(REMOVE_DUPLICATES openrc_audited_executable_targets)
    foreach(openrc_target IN LISTS openrc_audited_executable_targets)
        # TARGET_FILE establishes a target-level dependency as well; the
        # explicit edge documents the bootstrap relationship and keeps it
        # unambiguous across generators.
        add_dependencies(
            "${openrc_target}"
            openrc-portable-executable-audit
        )
        add_custom_command(
            TARGET "${openrc_target}"
            POST_BUILD
            COMMAND "$<TARGET_FILE:openrc-portable-executable-audit>"
                "$<TARGET_FILE:${openrc_target}>"
            COMMENT "Verifying portable imports for ${openrc_target}"
            VERBATIM
        )
    endforeach()
endif()
