cmake_minimum_required(VERSION 3.28)
project(BetterAtKrondor LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(BAK_VERSION "102" CACHE STRING "Game release the build targets: 100 (floppy) or 102 (CD)")
set_property(CACHE BAK_VERSION PROPERTY STRINGS 100 102)
if(NOT BAK_VERSION MATCHES "^(100|102)$")
    message(FATAL_ERROR "BAK_VERSION must be 100 or 102, got '${BAK_VERSION}'")
endif()

option(BAK_BUILD_PORT "Build the SDL3 executable (OFF: only compile the game sources)" ON)
option(BAK_FETCH_SDL "Download and build SDL3 instead of using an installed package" ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(WARNING "64-bit targets are not supported yet: the game still stores pointers in "
                    "32-bit fields. Configure an x86 (32-bit) build.")
endif()

# --- SDL3 ---------------------------------------------------------------------------

if(NOT BAK_BUILD_PORT)
elseif(BAK_FETCH_SDL)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3)
else()
    find_package(SDL3 REQUIRED CONFIG)
endif()

# --- Compiler settings shared by game and port code -----------------------------------

if(MSVC)
    # clang-cl or cl. /utf-8 keeps the (Latin-1) sources from being read as the ANSI code page.
    set(BAK_C_WARNINGS /W3)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(BAK_C_SEMANTICS /clang:-fwrapv /clang:-fno-strict-aliasing)
    else()
        set(BAK_C_SEMANTICS)
    endif()
    set(BAK_FORCE_INCLUDE /FI)
else()
    set(BAK_C_WARNINGS -Wall)
    set(BAK_C_SEMANTICS -fwrapv -fno-strict-aliasing -fsigned-char)
    set(BAK_FORCE_INCLUDE -include)
endif()

# --- The reconstructed game ------------------------------------------------------------
#
# Every C module of the original link (bak/KRONDOR.RSP / KRN102.RSP). The TASM modules
# of that link are replaced by C under port/asm. The 1.00 and 1.02 builds share one set
# of sources; 1.02 differences are #ifdef V102CD blocks.

file(GLOB_RECURSE BAK_GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/bak/SRC/*.C")
list(FILTER BAK_GAME_SOURCES EXCLUDE REGEX "/TOOLS/")
set_source_files_properties(${BAK_GAME_SOURCES} PROPERTIES LANGUAGE C)

add_library(bakgame STATIC ${BAK_GAME_SOURCES})
target_include_directories(bakgame PRIVATE
    "${CMAKE_SOURCE_DIR}/port/compat/borland"
    "${CMAKE_SOURCE_DIR}/port/compat/include"
    "${CMAKE_SOURCE_DIR}/port/include"
    "${CMAKE_SOURCE_DIR}/bak/INCLUDE"
    "${CMAKE_SOURCE_DIR}/bak")
target_compile_definitions(bakgame PRIVATE $<$<STREQUAL:${BAK_VERSION},102>:V102CD=1>)
target_compile_options(bakgame PRIVATE
    ${BAK_C_SEMANTICS}
    ${BAK_FORCE_INCLUDE} "${CMAKE_SOURCE_DIR}/port/include/bak_native.h")

if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # The reconstruction is Borland-era C. These are the classes of diagnostics it
    # triggers by the thousand; they are surveyed with -Weverything separately and
    # fixed by category, not silenced for good.
    set(BAK_GAME_QUIET
        -Wno-unknown-pragmas
        -Wno-address-of-packed-member
        -Wno-pragma-pack
        -Wno-parentheses
        -Wno-dangling-else
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-unused-function)
    if(MSVC)
        list(TRANSFORM BAK_GAME_QUIET PREPEND "/clang:")
    endif()
    target_compile_options(bakgame PRIVATE ${BAK_GAME_QUIET})
endif()

# --- The native executable ---------------------------------------------------------------

if(NOT BAK_BUILD_PORT)
    return()
endif()

file(GLOB BAK_PORT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/port/platform/*.c"
    "${CMAKE_SOURCE_DIR}/port/compat/*.c"
    "${CMAKE_SOURCE_DIR}/port/drivers/*.c"
    "${CMAKE_SOURCE_DIR}/port/asm/*.c")

add_executable(betterkrondor ${BAK_PORT_SOURCES})
target_include_directories(betterkrondor PRIVATE
    "${CMAKE_SOURCE_DIR}/port/compat/include"
    "${CMAKE_SOURCE_DIR}/port/include"
    "${CMAKE_SOURCE_DIR}/bak/INCLUDE"
    "${CMAKE_SOURCE_DIR}/bak")
target_compile_definitions(betterkrondor PRIVATE
    BAK_VERSION=${BAK_VERSION}
    $<$<STREQUAL:${BAK_VERSION},102>:V102CD=1>)
target_compile_options(betterkrondor PRIVATE ${BAK_C_WARNINGS} ${BAK_C_SEMANTICS})
target_link_libraries(betterkrondor PRIVATE bakgame SDL3::SDL3)

# --- Tests -------------------------------------------------------------------------------

enable_testing()
