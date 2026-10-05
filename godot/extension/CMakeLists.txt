# mtg_cpp Godot GDExtension (Phase 2).
#
# A self-contained CMake project for the extension shared library: it fetches
# godot-cpp (pinned to the Godot version below), links the mtg_cpp engine
# library, and emits the .so into godot/bin/ for the Godot project to load.
#
# Build:
#   cmake -S godot/extension -B <build> -G Ninja \
#     -DMTG_CPP_ENGINE_LIBRARY=<path>/libmtg_cpp_engine.so \
#     -DMTG_CPP_ENGINE_INCLUDE_DIR=<repo-root>
#   cmake --build <build>

cmake_minimum_required(VERSION 3.24)
project(mtg_cpp_godot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# mtg_cpp engine (built by the main project; consumed as a prebuilt here)
# ---------------------------------------------------------------------------
set(MTG_CPP_ENGINE_LIBRARY "" CACHE FILEPATH "Path to libmtg_cpp_engine.so")
set(MTG_CPP_ENGINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH
  "Repo root (contains include/ and src/)")

add_library(mtg_cpp_engine SHARED IMPORTED GLOBAL)
set_target_properties(mtg_cpp_engine PROPERTIES
  IMPORTED_LOCATION "${MTG_CPP_ENGINE_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES
    "${MTG_CPP_ENGINE_INCLUDE_DIR}/include;${MTG_CPP_ENGINE_INCLUDE_DIR}/src")

# ---------------------------------------------------------------------------
# Header-only deps the engine's internal headers (net/server.h, net/transport.h)
# expose. The engine links them into libmtg_cpp_engine already; the extension
# only needs their headers to compile its own translation units.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-30-2
  GIT_SHALLOW TRUE)
set(ASIO_STANDALONE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(asio)
if(NOT TARGET asio)
  add_library(asio INTERFACE)
  target_include_directories(asio INTERFACE "${asio_SOURCE_DIR}/asio/include")
endif()
target_compile_definitions(asio INTERFACE ASIO_NO_DEPRECATED)

# ---------------------------------------------------------------------------
# godot-cpp (static bindings library, pinned to the Godot ABI)
# ---------------------------------------------------------------------------
FetchContent_Declare(godot_cpp
  GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
  GIT_TAG godot-4.5-stable
  GIT_SHALLOW TRUE)

# The GDExtension interface header + API JSON live in the repo under
# godot/gdextension/ (pinned to Godot 4.5), decoupled from the godot-cpp
# checkout so configure never depends on its layout.
set(GODOTCPP_GDEXTENSION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gdextension" CACHE PATH "" FORCE)
set(GODOTCPP_TARGET "template_debug" CACHE STRING "" FORCE)
set(GODOTCPP_SYSTEM_HEADERS OFF CACHE BOOL "" FORCE)
# The mtg_cpp engine throws (std::runtime_error from the stores, nlohmann parse
# errors, ...). The binding catches everything at the boundary, so exceptions
# never cross the C ABI into Godot — but the boundary code still needs them
# enabled to compile.
set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(godot_cpp)

# ---------------------------------------------------------------------------
# The extension shared library
# ---------------------------------------------------------------------------
add_library(mtg_cpp_godot SHARED
  src/register_types.cpp
  src/mtg_cpp_bindings.cpp)
target_link_libraries(mtg_cpp_godot PRIVATE godot-cpp mtg_cpp_engine nlohmann_json::nlohmann_json asio)
set_target_properties(mtg_cpp_godot PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME "mtg_cpp_godot"
  POSITION_INDEPENDENT_CODE ON
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
