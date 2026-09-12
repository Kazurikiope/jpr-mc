cmake_minimum_required(VERSION 3.16)
project(jpr CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(JPR_BUILD_TESTS "Build the host side unit tests" OFF)

set(JPR_CORE_SOURCES
    src/api.cpp
    src/config.cpp
    src/inline_hook.cpp
    src/inline_hook_aarch64.cpp
    src/inline_hook_x86_64.cpp
    src/inline_hook_unsupported.cpp
    src/json.cpp
    src/keyboard.cpp
    src/keycodes.cpp
    src/log.cpp
    src/module.cpp
    src/random.cpp
    src/signatures.cpp
    src/sigscan.cpp
    src/tick.cpp
    src/game/game_hooks.cpp
    src/modules/auto_jump_reset.cpp
)

add_library(jpr_core OBJECT ${JPR_CORE_SOURCES})
target_include_directories(jpr_core PUBLIC include PRIVATE src)
target_compile_options(jpr_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

# The mod itself: an Android shared object the launcher dlopen()s.
add_library(jpr SHARED src/mod_main.cpp $<TARGET_OBJECTS:jpr_core>)
target_include_directories(jpr PRIVATE include src)
target_compile_options(jpr PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(jpr PROPERTIES PREFIX "lib" OUTPUT_NAME "jpr")

# mod_preinit/mod_init are looked up with dlsym, so they must stay exported
# even though everything else is hidden.
target_link_options(jpr PRIVATE -Wl,--no-undefined-version)

if(ANDROID)
    # The launcher resolves the mcpelauncher_* API at load time through
    # ANDROID_DLEXT_MCPELAUNCHER_HOOKS, so those references are undefined here
    # on purpose.
    target_link_options(jpr PRIVATE -Wl,--unresolved-symbols=ignore-all)
    target_link_libraries(jpr PRIVATE log dl)
endif()

if(JPR_BUILD_TESTS)
    enable_testing()
    add_executable(jpr_tests tests/tests.cpp $<TARGET_OBJECTS:jpr_core>)
    target_include_directories(jpr_tests PRIVATE include src)
    target_compile_options(jpr_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(jpr_tests PRIVATE dl pthread)
    add_test(NAME jpr_tests COMMAND jpr_tests)
endif()
