cmake_minimum_required(VERSION 3.16)
project(dreal4_cmake)
include(CMakePrintHelpers)

find_package(Git QUIET)
if(NOT GIT_FOUND)
  set(GIT_EXECUTABLE git)
endif()
set(GIT_VERSION_HEADER "${CMAKE_BINARY_DIR}/git_version.h")
# Always-run target: git queries are fast; copy_if_different in the script
# means git_version.h timestamp (and thus dreal_main.cc recompilation) only
# updates when the hash or dirty status actually changes.
add_custom_target(git_version_h
    COMMAND "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DOUTPUT_FILE=${GIT_VERSION_HEADER}"
        -P "${PROJECT_SOURCE_DIR}/cmake/GenerateGitVersion.cmake"
    COMMENT "Capturing git version info"
    VERBATIM
)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

# Emit gcc_build/compile_commands.json so clangd/editors resolve the project's
# full -I list (ibex-install, capd-install, generated parsers, the drake
# third_party tree). Symlink it to the repo root for clangd to find it:
#   ln -sf gcc_build/compile_commands.json compile_commands.json
# Survives a fresh FULL_BUILD.sh (which reconfigures without -D flags).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# NOTE: CMAKE_OSX_ARCHITECTURES is no longer forced to x86_64.
# CAPD (the original x86-only ODE library) has been replaced with Codac, which
# supports ARM64 natively. If you need to override (e.g. for cross-compilation),
# pass -DCMAKE_OSX_ARCHITECTURES=x86_64 on the cmake command line.

if (CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin")
    function(find_brew_package package_name variable_name)
        # Try ARM homebrew first, then x86 homebrew
        foreach(BREW_EXEC /opt/homebrew/bin/brew /usr/local/bin/brew)
            if (EXISTS "${BREW_EXEC}")
                execute_process(
                        COMMAND "${BREW_EXEC}" --prefix ${package_name}
                        RESULT_VARIABLE BREW_RESULT
                        OUTPUT_VARIABLE BREW_PREFIX
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                )
                if (BREW_RESULT EQUAL 0 AND EXISTS "${BREW_PREFIX}")
                    message(STATUS "Found ${package_name} via Homebrew at ${BREW_PREFIX}")
                    if (NOT DEFINED ${variable_name})
                        set(${variable_name} "${BREW_PREFIX}" PARENT_SCOPE)
                    endif()
                    return()
                endif()
            endif()
        endforeach()
    endfunction()

    find_brew_package(bison BISON_ROOT)
    find_brew_package(flex FLEX_ROOT)
    find_brew_package(gmp GMP_ROOT)
    find_brew_package(cadical CADICAL_ROOT)
endif ()


# parsers
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(PARSERS_HOME "${CMAKE_CURRENT_BINARY_DIR}/parsers/")
file(MAKE_DIRECTORY "${PARSERS_HOME}/dreal/smt2/")
FLEX_TARGET(smt2_scanner "${PROJECT_SOURCE_DIR}/src/dreal/smt2/scanner.ll" "${PARSERS_HOME}/dreal/smt2/scanner.ll.cc")
BISON_TARGET(smt2_parser "${PROJECT_SOURCE_DIR}/src/dreal/smt2/parser.yy" "${PARSERS_HOME}/dreal/smt2/parser.yy.cc")
ADD_FLEX_BISON_DEPENDENCY(smt2_scanner smt2_parser)

file(MAKE_DIRECTORY "${PARSERS_HOME}/dreal/dr/")
FLEX_TARGET(dr_scanner "${PROJECT_SOURCE_DIR}/src/dreal/dr/scanner.ll" "${PARSERS_HOME}/dreal/dr/scanner.ll.cc")
BISON_TARGET(dr_parser "${PROJECT_SOURCE_DIR}/src/dreal/dr/parser.yy" "${PARSERS_HOME}/dreal/dr/parser.yy.cc")
ADD_FLEX_BISON_DEPENDENCY(dr_scanner dr_parser)


# manually imported dependencies
set(DRAKE_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_robotlocomotion_drake/")
set(DYNBITSET_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_pinam45_dynamic_bitset/")
set(THREADPOOL_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_progschj_threadpool/")
set(EZPARSER_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_dreal-deps_ezoptionparser/")
set(LIBCDS_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_khizmax_libcds/")
set(JSON_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_nlohmann_json/")
set(PICOSAT_HOME "${PROJECT_SOURCE_DIR}/src/third_party/com_github_dreal-deps_picosat/")

unset(DREAL_SOURCES)
unset(DREAL_SOURCES CACHE)
file(GLOB_RECURSE DREAL_SOURCES
        "${PROJECT_SOURCE_DIR}/src/dreal/*/**.cc"
        "${PROJECT_SOURCE_DIR}/src/dreal/*/**.yy"
        "${PROJECT_SOURCE_DIR}/src/dreal/*/**.ll"
        "${DRAKE_HOME}/**.cc"
        "${THREADPOOL_HOME}/**.cc"
        "${LIBCDS_HOME}/src/**.cpp"
        "${PICOSAT_HOME}/src/**.c"
)
add_library(dreal4_cmake STATIC
        ${DREAL_SOURCES}
        ${BISON_smt2_parser_OUTPUTS} ${FLEX_smt2_scanner_OUTPUTS}
        ${BISON_dr_parser_OUTPUTS} ${FLEX_dr_scanner_OUTPUTS}
)
# -frounding-math tells the compiler not to assume a fixed FPU rounding mode, so it
# cannot constant-fold or reorder FP arithmetic across fesetround calls. dReal runs
# gaol/ibex under FE_UPWARD and formatting/CAPD under FE_TONEAREST; without this flag
# Clang's #pragma STDC FENV_ACCESS ON support is incomplete and the directed-rounding
# ops in rounded_interval.h (sub_up/sub_down/add_up/add_down) could be silently
# mis-compiled. gaol itself is already compiled with -frounding-math by IBEX's
# CMakeLists; this covers dReal4's own source. PUBLIC propagates to dreal4 and tests.
target_compile_options(dreal4_cmake PUBLIC -frounding-math)
target_include_directories(dreal4_cmake PUBLIC
        "${PARSERS_HOME}"
        "${PROJECT_SOURCE_DIR}/src/"
        "${DRAKE_HOME}"
        "${DYNBITSET_HOME}"
        "${THREADPOOL_HOME}"
        "${EZPARSER_HOME}"
        "${LIBCDS_HOME}"
        "${JSON_HOME}"
        "${PICOSAT_HOME}/src/"
)

# system dependencies
find_library(   FLEX_LIB NAMES libfl.a    HINTS "${FLEX_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${FLEX_INCLUDE_DIRS}")
target_link_libraries(dreal4_cmake PUBLIC "${FLEX_LIB}")

find_path(       GMP_INC NAMES gmpxx.h    HINTS "${GMP_ROOT}" PATH_SUFFIXES include includes     REQUIRED NO_CACHE)
find_library(  GMP_C_LIB NAMES libgmp.a   HINTS "${GMP_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
find_library(GMP_CXX_LIB NAMES libgmpxx.a HINTS "${GMP_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${GMP_INC}")
target_link_libraries(dreal4_cmake PUBLIC "${GMP_C_LIB}" "${GMP_CXX_LIB}")

find_path(   CADICAL_INC NAMES cadical.hpp  HINTS "${CADICAL_ROOT}" PATH_SUFFIXES include includes     REQUIRED NO_CACHE)
find_library(CADICAL_LIB NAMES libcadical.a HINTS "${CADICAL_ROOT}" PATH_SUFFIXES lib libs lib32 lib64 REQUIRED NO_CACHE)
target_include_directories(dreal4_cmake PUBLIC "${CADICAL_INC}")
target_link_libraries(dreal4_cmake PUBLIC "${CADICAL_LIB}")

# ---------------------------------------------------------------------------
# IBEX via ExternalProject — source-built from the dReal team's fork
#
# The fork carries a series of surgical soundness/perf/portability patches on
# top of ibex-team/ibex-lib@master. The canonical patch catalog (what each one
# does, and its upstream-PR status) is ../ibex-fork/MIGRATION.md — this file
# deliberately doesn't duplicate it. Every patch is a candidate upstream PR;
# once they all land, drop the fork and point at ibex-team/ibex-lib directly.
#
# For local development against a working tree, override IBEX_GIT_REPOSITORY
# with a file:// URL pointing at the local fork checkout.
#
# Build options:
#   INTERVAL_LIB=gaol  — arm64-native works since mainline 971f8eb0
#   LP_LIB=soplex      — vendored SoPlex 4.0.2 (hermetic, ZLIB/GMP off); enables
#                        the polytope/X-Newton linear-relaxation contractors that
#                        --forall-polytope exercises for ∃∀ queries
#   BUILD_TESTING=0    — skip cppunit (not needed at consume time)
# ---------------------------------------------------------------------------
include(ExternalProject)

set(IBEX_INSTALL_DIR     "${CMAKE_BINARY_DIR}/ibex-install")
set(IBEX_GIT_REPOSITORY  "https://github.com/ncsys-lab/ibex-lib.git"
    CACHE STRING "Git repository for the ibex fork (override with file://path for local dev)")
# Pinned to a fork sha (reproducible) rather than the moving branch ref.
# Bump when new fork patches land. (Local-dev iteration still works via
# -DIBEX_GIT_TAG=<branch-or-sha> and -DIBEX_GIT_REPOSITORY=file:///path/to/ibex-fork.)
set(IBEX_GIT_TAG         "d99309094850efb84e2ebc11a62ee655d690639f"
    CACHE STRING "Git ref for the ibex fork (branch, tag, or sha)")

set(EP_ARCH_ARGS "")
if (DEFINED CMAKE_OSX_ARCHITECTURES)
    list(APPEND EP_ARCH_ARGS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
endif()

ExternalProject_Add(ibex_external
    PREFIX            "${CMAKE_BINARY_DIR}/ibex_ep"
    GIT_REPOSITORY    "${IBEX_GIT_REPOSITORY}"
    GIT_TAG           "${IBEX_GIT_TAG}"
    GIT_SHALLOW       OFF
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=${IBEX_INSTALL_DIR}"
        "-DINTERVAL_LIB=gaol"
        "-DLP_LIB=soplex"
        "-DBUILD_TESTING=0"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        ${EP_ARCH_ARGS}
    BUILD_BYPRODUCTS
        "${IBEX_INSTALL_DIR}/lib/libibex.a"
        "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libgaol.a"
        "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libultim.a"
        "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libsoplex.a"
    INSTALL_DIR       "${IBEX_INSTALL_DIR}"
)
# Pre-create the include dir so the IMPORTED-target validation doesn't fail
# at configure time (ExternalProject populates it at build time).
file(MAKE_DIRECTORY "${IBEX_INSTALL_DIR}/include")

# ---- IMPORTED targets for IBEX ----
# gaol and ultim are IBEX's interval arithmetic backends, installed under lib/ibex/3rd/.
# All three depend on ibex_external to ensure the source-build runs before linking.
add_library(ibex_imported STATIC IMPORTED GLOBAL)
add_dependencies(ibex_imported ibex_external)
set_target_properties(ibex_imported PROPERTIES
    IMPORTED_LOCATION "${IBEX_INSTALL_DIR}/lib/libibex.a"
    INTERFACE_INCLUDE_DIRECTORIES "${IBEX_INSTALL_DIR}/include"
)
add_library(ibex_gaol  STATIC IMPORTED GLOBAL)
add_library(ibex_ultim STATIC IMPORTED GLOBAL)
add_dependencies(ibex_gaol  ibex_external)
add_dependencies(ibex_ultim ibex_external)
set_target_properties(ibex_gaol  PROPERTIES IMPORTED_LOCATION "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libgaol.a")
set_target_properties(ibex_ultim PROPERTIES IMPORTED_LOCATION "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libultim.a")
# soplex is IBEX's LP backend (LP_LIB=soplex), needed by the polytope/X-Newton
# linear-relaxation contractors that --forall-polytope exercises.
add_library(ibex_soplex STATIC IMPORTED GLOBAL)
add_dependencies(ibex_soplex ibex_external)
set_target_properties(ibex_soplex PROPERTIES IMPORTED_LOCATION "${IBEX_INSTALL_DIR}/lib/ibex/3rd/libsoplex.a")

# ---------------------------------------------------------------------------
# CAPD via ExternalProject — second ODE backend (order-20 Taylor) running
# alongside Codac's order-2 CtcLohner. See CODAC_MIGRATION.md for the
# motivation and gating policy.
#
# The historical FILIB-on-ARM64 blocker that abandoned the prior CAPD attempt
# is gone in CAPD master: CAPD_INTERVAL_TYPE=NATIVE makes capdExt skip
# add_subdirectory(filibsrc) entirely and CAPD's own DoubleRounding (ARM64
# `msr fpcr` assembly, x86 SSE) is used instead. The two-file FILIB-bypass
# patch documented in CODAC_MIGRATION.md is no longer required.
#
# Pin: b353e170 (2026-05-18). The CAPD_INTERVAL_TYPE switch landed in the
# v6.1.0 development cycle (Jan 26, 2025 — see CAPD PR #22 / commit b7506bf9
# bumping the version, alongside the CMAKE_HOST_SYSTEM_PROCESSOR fix). No
# v6.1.0 release tag exists yet, so we pin a master SHA. Bump only after
# verifying that the CAPD_INTERVAL_TYPE plumbing is still intact.
# ---------------------------------------------------------------------------
set(CAPD_INSTALL_DIR "${CMAKE_BINARY_DIR}/capd-install")
set(CAPD_GIT_TAG     "b353e170")

ExternalProject_Add(capd_external
    PREFIX            "${CMAKE_BINARY_DIR}/capd_ep"
    GIT_REPOSITORY    "https://github.com/CAPDGroup/CAPD.git"
    GIT_TAG           "${CAPD_GIT_TAG}"
    GIT_SHALLOW       OFF
    UPDATE_DISCONNECTED 1
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=${CAPD_INSTALL_DIR}"
        "-DCAPD_INTERVAL_TYPE=NATIVE"
        "-DCAPD_ENABLE_MULTIPRECISION=OFF"
        "-DCAPD_BUILD_ALL=OFF"
        "-DCAPD_BUILD_TESTS=OFF"
        "-DCMAKE_CXX_STANDARD=17"
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        "-DCMAKE_BUILD_TYPE=Release"
        ${EP_ARCH_ARGS}
    BUILD_BYPRODUCTS  "${CAPD_INSTALL_DIR}/lib/libcapd.a"
    INSTALL_DIR       "${CAPD_INSTALL_DIR}"
)

# IMPORTED target — the .a file does not exist until build time, but CMake
# only checks existence at link time, so add_dependencies wires the order.
file(MAKE_DIRECTORY "${CAPD_INSTALL_DIR}/include")
add_library(capd_imported STATIC IMPORTED GLOBAL)
add_dependencies(capd_imported capd_external)
set_target_properties(capd_imported PROPERTIES
    IMPORTED_LOCATION "${CAPD_INSTALL_DIR}/lib/libcapd.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CAPD_INSTALL_DIR}/include"
    # CAPD's interval/lib.h fails with "No interval type selected!" unless
    # exactly one of __USE_NATIVE__ / __USE_FILIB__ / __USE_CXSC__ is
    # defined on the consumer's compile line. We built CAPD with
    # CAPD_INTERVAL_TYPE=NATIVE, so consumers must define __USE_NATIVE__
    # to pick the matching template instantiations.
    INTERFACE_COMPILE_DEFINITIONS "__USE_NATIVE__"
)

# IBEX and CAPD are built at build time via ExternalProject; their IMPORTED
# targets carry add_dependencies(...) (see above) so the build order is wired
# correctly without needing add_dependencies on dreal4_cmake itself.

# Wire dreal4_cmake against IBEX and CAPD (static linking requires explicit list).
target_include_directories(dreal4_cmake PUBLIC
    "${IBEX_INSTALL_DIR}/include"
    "${IBEX_INSTALL_DIR}/include/ibex"
    "${IBEX_INSTALL_DIR}/include/ibex/3rd"
    "${CAPD_INSTALL_DIR}/include"
)

target_link_libraries(dreal4_cmake PUBLIC
    ibex_imported
    ibex_gaol
    ibex_ultim
    ibex_soplex
    capd_imported
)
# CAPD builds at build time (ExternalProject), so dreal4_cmake must wait for it.
add_dependencies(dreal4_cmake capd_external ibex_external)

# ---------------------------------------------------------------------------
# FetchContent dependencies (unchanged from before)
# ---------------------------------------------------------------------------
include(FetchContent)

set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt
        GIT_TAG 11.0.2) # version must be kept in sync with what SPDLOG expects
FetchContent_MakeAvailable(fmt)

set(FMT_ROOT "${fmt_BINARY_DIR}")
set(SPDLOG_FMT_EXTERNAL    ON  CACHE BOOL "Use external linked fmt library" FORCE)
set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "Use external header fmt library" FORCE)
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog/
        GIT_TAG v1.15.0)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
        nlopt
        GIT_REPOSITORY https://github.com/stevengj/nlopt
        GIT_TAG v2.9.1)
FetchContent_MakeAvailable(nlopt)

unset(BUILD_SHARED_LIBS)

target_link_libraries(dreal4_cmake PUBLIC nlopt spdlog::spdlog fmt::fmt)

# On some platforms, linking is purely sequential; repeat GMP so it resolves on Ubuntu.
target_link_libraries(dreal4_cmake PUBLIC "${GMP_C_LIB}" "${GMP_CXX_LIB}")

# executable
add_executable(dreal4 "${PROJECT_SOURCE_DIR}/src/dreal/dreal_main.cc")
target_link_libraries(dreal4 PRIVATE dreal4_cmake)
add_dependencies(dreal4 git_version_h)
target_include_directories(dreal4 PRIVATE "${CMAKE_BINARY_DIR}")
target_compile_definitions(dreal4 PRIVATE
    DREAL_BUILD_OS="${CMAKE_SYSTEM_NAME}"
    DREAL_BUILD_OS_VERSION="${CMAKE_SYSTEM_VERSION}"
    DREAL_BUILD_ARCH="${CMAKE_SYSTEM_PROCESSOR}"
)


# tests
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

set(DRAKE_TEST_HOME "${PROJECT_SOURCE_DIR}/test/third_party/com_github_robotlocomotion_drake/")
file(GLOB_RECURSE DREAL_TESTS
        "${PROJECT_SOURCE_DIR}/test/dreal/**.cc"
        "${DRAKE_TEST_HOME}/**.cc"
)
list(REMOVE_ITEM DREAL_TESTS "${PROJECT_SOURCE_DIR}/test/dreal/api/test/cav19_benchmark.cc")
add_executable(dreal4_cmake_test ${DREAL_TESTS})
target_include_directories(dreal4_cmake_test PRIVATE
        "${PROJECT_SOURCE_DIR}/test/"
        "${DRAKE_TEST_HOME}"
)
target_link_libraries(
        dreal4_cmake_test
        PRIVATE
        dreal4_cmake
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(dreal4_cmake_test)
