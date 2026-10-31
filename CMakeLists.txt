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
# Finds what the compiler cannot: buffers sized for 16-bit pointers and other out-of-bounds
# accesses the original got away with in DOS. Port code that emulates DOS memory keeps its
# overruns inside its own allocations, so reports point at the game code.
option(BAK_ASAN "Build with AddressSanitizer" OFF)

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

if(BAK_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        # CMake links with lld-link/link directly, which does not add the runtime itself.
        get_filename_component(BAK_LLVM_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
        file(GLOB BAK_ASAN_DLL "${BAK_LLVM_BIN}/../lib/clang/*/lib/windows/clang_rt.asan_dynamic-i386.dll")
        if(NOT BAK_ASAN_DLL)
            message(FATAL_ERROR "BAK_ASAN: clang_rt.asan_dynamic-i386.dll not found next to ${CMAKE_C_COMPILER}")
        endif()
        list(GET BAK_ASAN_DLL 0 BAK_ASAN_DLL)
        get_filename_component(BAK_ASAN_DIR "${BAK_ASAN_DLL}" DIRECTORY)
        link_directories("${BAK_ASAN_DIR}")
        link_libraries(clang_rt.asan_dynamic-i386 clang_rt.asan_dynamic_runtime_thunk-i386)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
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
# The original writes into string literals (DOS had no read-only memory) and its build
# never merges identical ones (Borland's -d is off): game literals are writable, one copy
# per occurrence.
if(MSVC)
    set(BAK_WRITABLE_STRINGS /GF-)
else()
    set(BAK_WRITABLE_STRINGS -fwritable-strings)
endif()
target_compile_options(bakgame PRIVATE
    ${BAK_C_SEMANTICS}
    ${BAK_WRITABLE_STRINGS}
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
list(REMOVE_ITEM BAK_PORT_SOURCES "${CMAKE_SOURCE_DIR}/port/platform/main.c")

# Everything native except the process entry point: shared by the game and the test runner.
add_library(bakport OBJECT ${BAK_PORT_SOURCES})
target_include_directories(bakport PUBLIC
    "${CMAKE_SOURCE_DIR}/port/compat/include"
    "${CMAKE_SOURCE_DIR}/port/include"
    "${CMAKE_SOURCE_DIR}/bak/INCLUDE"
    "${CMAKE_SOURCE_DIR}/bak")
target_compile_definitions(bakport PUBLIC
    BAK_VERSION=${BAK_VERSION}
    $<$<STREQUAL:${BAK_VERSION},102>:V102CD=1>)
target_compile_options(bakport PUBLIC ${BAK_C_WARNINGS} ${BAK_C_SEMANTICS})
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # bak_game_begin.h pushes the packing that bak_game_end.h pops: across headers on purpose.
    if(MSVC)
        target_compile_options(bakport PUBLIC /clang:-Wno-pragma-pack)
    else()
        target_compile_options(bakport PUBLIC -Wno-pragma-pack)
    endif()
endif()
target_link_libraries(bakport PUBLIC SDL3::SDL3)

add_executable(betterkrondor "${CMAKE_SOURCE_DIR}/port/platform/main.c")
target_link_libraries(betterkrondor PRIVATE bakport bakgame SDL3::SDL3)
if(BAK_ASAN AND MSVC)
    add_custom_command(TARGET betterkrondor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BAK_ASAN_DLL}"
                "$<TARGET_FILE_DIR:betterkrondor>")
endif()

# --- Tests -------------------------------------------------------------------------------
#
# Differential tests (port/tests): pytest runs original routines from the reference build
# in Unicorn and their C translations in bak_difftest, then compares. They need uv and the
# reference artifacts in work/ref (see port/tests/README.md); without them they skip.

file(GLOB BAK_DIFFTEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/tests/difftest/*.c")
add_executable(bak_difftest ${BAK_DIFFTEST_SOURCES})
target_link_libraries(bak_difftest PRIVATE bakport bakgame SDL3::SDL3)

enable_testing()
find_program(BAK_UV uv)
if(BAK_UV)
    add_test(NAME difftest
             COMMAND "${BAK_UV}" run --group difftest pytest -q port/tests
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(difftest PROPERTIES
        ENVIRONMENT "BAK_DIFFTEST_EXE=$<TARGET_FILE:bak_difftest>")
else()
    message(STATUS "uv not found: the differential tests are not registered with ctest")
endif()
