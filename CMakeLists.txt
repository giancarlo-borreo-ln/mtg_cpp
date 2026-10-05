cmake_minimum_required(VERSION 3.24)

project(mtg_cpp
  VERSION 0.1.0
  DESCRIPTION "MTG VTT sandbox — C++ desktop rewrite (Shandalar-style)"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language & warnings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always emit compile_commands.json so clang-tidy can be pointed at any build tree.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MTG_CPP_BUILD_TESTS       "Build the unit test suite" ON)
option(MTG_CPP_ENABLE_SANITIZERS "Build with AddressSanitizer + UBSan (Linux, GCC/Clang)" OFF)
option(MTG_CPP_ENABLE_TSAN       "Build with ThreadSanitizer (Linux, GCC/Clang; exclusive of ASan/UBSan)" OFF)
option(MTG_CPP_HIDDEN_VISIBILITY "Build mtg_cpp_engine with hidden symbol visibility + explicit MTG_CPP_EXPORT" OFF)

set(MTG_CPP_SFML_DIR "" CACHE PATH
  "Path to a prebuilt SFML (the directory containing lib/cmake/SFML). \
Optional; CMake falls back to the system SFML, then builds SFML from source.")

if(MTG_CPP_ENABLE_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR "MTG_CPP_ENABLE_SANITIZERS is not supported on MSVC; use the Debug configuration instead.")
  endif()
  add_compile_options(-g -fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(MTG_CPP_ENABLE_TSAN)
  if(MSVC)
    message(FATAL_ERROR "MTG_CPP_ENABLE_TSAN is not supported on MSVC.")
  endif()
  if(MTG_CPP_ENABLE_SANITIZERS)
    message(FATAL_ERROR "MTG_CPP_ENABLE_TSAN cannot be combined with MTG_CPP_ENABLE_SANITIZERS.")
  endif()
  add_compile_options(-g -fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(UNIX AND NOT APPLE)
    # Prebuilt shared libs in non-system locations (e.g. a downloaded SFML)
    # often expose only one module as a direct dependency. DT_RUNPATH does not
    # resolve those transitive libraries, so use a recursive DT_RPATH instead.
    add_link_options(-Wl,--disable-new-dtags)
  endif()
endif()

include(FetchContent)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# nlohmann/json — header-only
# ---------------------------------------------------------------------------
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# Asio — header-only standalone networking
# ---------------------------------------------------------------------------
FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-30-2
  GIT_SHALLOW TRUE)
set(ASIO_STANDALONE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(asio)
if(NOT TARGET asio)
  # Asio ships no installable CMake target; define a thin interface target.
  add_library(asio INTERFACE)
  target_include_directories(asio INTERFACE "${asio_SOURCE_DIR}/asio/include")
endif()
# Drop the deprecated error_code-returning sync overloads: asio's
# close(ec)/shutdown(ec) then return void, which keeps clang-tidy's
# bugprone-unused-return-value quiet.
target_compile_definitions(asio INTERFACE ASIO_NO_DEPRECATED)

# ---------------------------------------------------------------------------
# SFML — prebuilt dir, then system, then source build
# ---------------------------------------------------------------------------
if(MTG_CPP_SFML_DIR)
  list(PREPEND CMAKE_PREFIX_PATH "${MTG_CPP_SFML_DIR}")
endif()
find_package(SFML 2.6 COMPONENTS graphics window system QUIET)
if(NOT SFML_FOUND)
  message(STATUS
    "SFML 2.6 not found on the system — building from source via FetchContent "
    "(on Linux this requires X11/GL/freetype dev headers).")
  FetchContent_Declare(sfml
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 2.6.1
    GIT_SHALLOW TRUE)
  set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(sfml)
endif()

# ---------------------------------------------------------------------------
# cpr — HTTP client for the runtime art cache (Sprint 10)
# ---------------------------------------------------------------------------
# cpr wraps libcurl; it pulls a from-source curl when no system curl dev package
# is present (the OpenSSL backend is used, so HTTPS art downloads work on a
# clean machine). cpr's CMake does the curl FetchContent itself.
FetchContent_Declare(cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.5
  GIT_SHALLOW TRUE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CPR_FORCE_USE_SYSTEM_CURL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpr)

# curl's bundled zlib-ng forces BUILD_TESTING OFF in the cache; restore the
# project's own test-suite switch so our gtest target is still built.
if(MTG_CPP_BUILD_TESTS AND NOT BUILD_TESTING)
  set(BUILD_TESTING ON CACHE BOOL "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Engine libraries (core + net + state), built as OBJECT libraries so the same
# translation units feed the single shared engine library without being
# compiled twice. The layered names survive as compile/link groupings; the
# public artifact is the merged `mtg_cpp_engine` shared library below.
# ---------------------------------------------------------------------------
add_library(mtg_cpp_core OBJECT
  src/core/card.cpp
  src/core/board.cpp
  src/core/deck_parser.cpp
  src/core/arena.cpp
  src/core/check_cards.cpp
  src/core/card_record.cpp
  src/core/card_database.cpp
  src/core/importer.cpp
  src/core/deck_editor.cpp
  src/core/import_preview.cpp
  src/core/card_serialization.cpp
  src/core/data_paths.cpp
  src/store/deck_repository.cpp
  src/store/profile_store.cpp)
target_include_directories(mtg_cpp_core PUBLIC src)
target_link_libraries(mtg_cpp_core PUBLIC nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# Networking library (envelope, framing, relay server, client, transports)
# ---------------------------------------------------------------------------
add_library(mtg_cpp_net OBJECT
  src/net/envelope.cpp
  src/net/address.cpp
  src/net/connection_manager.cpp
  src/net/server.cpp
  src/net/transport.cpp
  src/net/client.cpp)
target_include_directories(mtg_cpp_net PUBLIC src)
target_link_libraries(mtg_cpp_net PUBLIC
  nlohmann_json::nlohmann_json
  asio
  Threads::Threads)

# ---------------------------------------------------------------------------
# State library (board state reducer, sync payloads, room session machine)
# ---------------------------------------------------------------------------
add_library(mtg_cpp_state OBJECT
  src/state/board_state.cpp
  src/state/sync.cpp
  src/state/session.cpp)
target_include_directories(mtg_cpp_state PUBLIC src)
# Object libraries propagate only usage requirements (no object files), so
# linking core/net here gives state their include dirs (asio, nlohmann, ...)
# exactly as before the object-library split.
target_link_libraries(mtg_cpp_state PUBLIC mtg_cpp_core mtg_cpp_net nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# Engine shared library: the single reusable artifact the Godot GDExtension
# (Phase 2) links against. It merges the three object libraries (no TU is
# compiled twice) and links the engine's third-party deps into one .so/.dll.
# ---------------------------------------------------------------------------
add_library(mtg_cpp_engine SHARED
  $<TARGET_OBJECTS:mtg_cpp_core>
  $<TARGET_OBJECTS:mtg_cpp_net>
  $<TARGET_OBJECTS:mtg_cpp_state>)
target_include_directories(mtg_cpp_engine PUBLIC src include)
target_link_libraries(mtg_cpp_engine PUBLIC
  nlohmann_json::nlohmann_json
  asio
  Threads::Threads)
set_target_properties(mtg_cpp_core mtg_cpp_net mtg_cpp_state PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
if(MTG_CPP_HIDDEN_VISIBILITY)
  set_target_properties(mtg_cpp_engine PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  target_compile_definitions(mtg_cpp_engine PRIVATE MTG_CPP_BUILDING_ENGINE)
endif()

# ---------------------------------------------------------------------------
# Bundled assets (fonts, later procedural art placeholders)
# ---------------------------------------------------------------------------
# Copy assets/ into the build tree at configure time, so the app and the tests
# find fonts by the relative path "assets/..." no matter where they were built.
# Packaging (M11.2) will install them next to the executable instead.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# ---------------------------------------------------------------------------
# UI library (SFML theme + screen router; screens/widgets in later sprints)
# ---------------------------------------------------------------------------
add_library(mtg_cpp_ui
  src/ui/theme.cpp
  src/ui/layout.cpp
  src/ui/app.cpp
  src/ui/art_cache.cpp
  src/ui/widgets/button.cpp
  src/ui/widgets/widget.cpp
  src/ui/widgets/text_input.cpp
  src/ui/widgets/list_view.cpp
  src/ui/widgets/deck_list_view.cpp
  src/ui/widgets/card_list.cpp
  src/ui/widgets/deck_builder_list.cpp
  src/ui/widgets/text_area.cpp
  src/ui/widgets/preview_list.cpp
  src/ui/widgets/card_view.cpp
  src/ui/widgets/zone_view.cpp
  src/ui/widgets/context_menu.cpp
  src/ui/screens/home_screen.cpp
  src/ui/screens/deck_editor_screen.cpp
  src/ui/screens/lobby_screen.cpp
  src/ui/screens/table_screen.cpp)
target_include_directories(mtg_cpp_ui PUBLIC src)
target_link_libraries(mtg_cpp_ui PUBLIC mtg_cpp_engine sfml-graphics sfml-window sfml-system)
target_link_libraries(mtg_cpp_ui PRIVATE cpr::cpr)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(mtg_cpp src/main.cpp)
target_link_libraries(mtg_cpp PRIVATE mtg_cpp_ui)
target_compile_definitions(mtg_cpp PRIVATE MTG_CPP_VERSION="${PROJECT_VERSION}")

# Integration driver (Sprint 12): simulates mouse presses through the App's
# scripting seam to exercise the whole app headlessly. Not part of ctest — it
# is run by /tmp/todocpp/integration.sh as an end-to-end gate.
add_executable(mtg_cpp_integration src/integration.cpp)
target_link_libraries(mtg_cpp_integration PRIVATE mtg_cpp_ui)
target_compile_definitions(mtg_cpp_integration PRIVATE MTG_CPP_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Fuzz targets (M11.1): structure-aware fuzz of the deck parser + the network
# envelope/framing. With clang the targets link libFuzzer (`-fsanitize=fuzzer`,
# which provides main); otherwise the shared driver main runs a fixed-budget
# mutation loop over the seed corpus. Only meaningful under the ASan/UBSan
# build (the sanitizers catch the crash/UB a target would hide otherwise).
# ---------------------------------------------------------------------------
option(MTG_CPP_BUILD_FUZZERS "Build the structure-aware fuzz targets" ${MTG_CPP_ENABLE_SANITIZERS})

if(MTG_CPP_BUILD_FUZZERS)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-fsanitize=fuzzer" MTG_CPP_HAVE_LIBFUZZER)

  function(mtg_cpp_add_fuzz_target target source)
    add_executable(${target} ${source} src/fuzz/fuzz_target.cpp)
    target_include_directories(${target} PRIVATE src)
    target_link_libraries(${target} PRIVATE mtg_cpp_engine Threads::Threads)
    if(MTG_CPP_HAVE_LIBFUZZER)
      target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined -g -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    else()
      target_compile_definitions(${target} PRIVATE MTG_CPP_FUZZ_DRIVER)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -g -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endfunction()
  mtg_cpp_add_fuzz_target(mtg_cpp_fuzz_parser src/fuzz/fuzz_deck_parser.cpp)
  mtg_cpp_add_fuzz_target(mtg_cpp_fuzz_envelope src/fuzz/fuzz_envelope.cpp)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MTG_CPP_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
      URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(mtg_cpp_tests
      tests/smoke_test.cpp
      tests/core/models_test.cpp
      tests/core/board_test.cpp
      tests/core/deck_parser_test.cpp
      tests/core/arena_test.cpp
      tests/core/check_cards_test.cpp
      tests/core/card_db_test.cpp
      tests/core/importer_test.cpp
      tests/core/deck_editor_test.cpp
      tests/core/import_preview_test.cpp
      tests/core/data_paths_test.cpp
      tests/store/deck_repository_test.cpp
      tests/store/profile_store_test.cpp
      tests/net/envelope_test.cpp
      tests/net/message_queue_test.cpp
      tests/net/connection_manager_test.cpp
      tests/net/server_test.cpp
      tests/net/routing_test.cpp
      tests/net/two_peer_integration_test.cpp
      tests/net/wire_hardening_test.cpp
      tests/state/board_state_test.cpp
      tests/state/sync_test.cpp
      tests/state/session_test.cpp
      tests/state/soak_test.cpp
      tests/fuzz/fuzz_corpus_test.cpp
      tests/ui/layout_test.cpp
      tests/ui/app_test.cpp
      tests/ui/widgets_test.cpp
      tests/ui/deck_list_view_test.cpp
      tests/ui/card_list_test.cpp
      tests/ui/deck_builder_list_test.cpp
      tests/ui/text_area_test.cpp
      tests/ui/preview_list_test.cpp
      tests/ui/home_screen_test.cpp
      tests/ui/deck_editor_screen_test.cpp
      tests/ui/lobby_screen_test.cpp
      tests/ui/theme_test.cpp
      tests/ui/art_cache_test.cpp
      tests/ui/cursor_test.cpp
      tests/ui/card_view_test.cpp
      tests/ui/zone_view_test.cpp
      tests/ui/context_menu_test.cpp
      tests/ui/table_screen_test.cpp)
    target_compile_definitions(mtg_cpp_tests PRIVATE MTG_CPP_VERSION="${PROJECT_VERSION}"
      MTG_CPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(mtg_cpp_tests PRIVATE
      mtg_cpp_engine
      mtg_cpp_ui
      nlohmann_json::nlohmann_json
      asio
      GTest::gtest_main
      sfml-graphics sfml-window sfml-system)

    include(GoogleTest)
    # Skip known third-party exit leaks (SFML's FreeType library, GPU driver)
    # on Linux; see lsan.supp. Anything leaking in our own code still fails.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      gtest_discover_tests(mtg_cpp_tests PROPERTIES ENVIRONMENT
        "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp")
    else()
      gtest_discover_tests(mtg_cpp_tests)
    endif()

    # Engine linkage smoke test (Phase 1): links ONLY mtg_cpp_engine — no SFML,
    # no UI — to prove the engine is a self-contained reusable library.
    add_executable(mtg_cpp_engine_tests tests/engine/engine_smoke_test.cpp)
    target_include_directories(mtg_cpp_engine_tests PRIVATE src include)
    target_link_libraries(mtg_cpp_engine_tests PRIVATE mtg_cpp_engine GTest::gtest_main)
    gtest_discover_tests(mtg_cpp_engine_tests)
  endif()
endif()
