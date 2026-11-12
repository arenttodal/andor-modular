cmake_minimum_required(VERSION 3.25)

# ANDOR MODULAR: invent an instrument.
#
# Built on ANDOR OSP (external/osp: core DSP, analysis, shared design) and ANDOR ANALOG
# (external/analog: oscillator models, filters, live morph, FX library), both pinned
# submodules that this project never modifies.
#
#   dsp/        the modular kernel, pure C++20 (no JUCE): andor_modular_dsp
#   tools/      modular-render (offline WAV), modular-validate, modular-bench
#   tests/      Catch2: modular_tests
#   product/    the plugin + standalone (JUCE), -DMODULAR_BUILD_PLUGIN=ON
#
#   cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
#   cmake -B build-plugin -G Ninja -DMODULAR_BUILD_PLUGIN=ON && cmake --build build-plugin

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/product/product.json" MODULAR_PRODUCT_JSON)
string(JSON MODULAR_VERSION GET "${MODULAR_PRODUCT_JSON}" version)
string(JSON MODULAR_NAME GET "${MODULAR_PRODUCT_JSON}" name)
string(JSON MODULAR_SLUG GET "${MODULAR_PRODUCT_JSON}" slug)
string(JSON MODULAR_CODE GET "${MODULAR_PRODUCT_JSON}" pluginCode)
string(JSON MODULAR_BUNDLE GET "${MODULAR_PRODUCT_JSON}" bundleId)
if(NOT MODULAR_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "product/product.json: version '${MODULAR_VERSION}' is not MAJOR.MINOR.PATCH")
endif()

project(ANDOR_MODULAR
    VERSION ${MODULAR_VERSION}
    DESCRIPTION "ANDOR MODULAR: invent an instrument"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MODULAR_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" OFF)
option(MODULAR_BUILD_TESTS "Build the tests" ON)
option(MODULAR_SANITIZE "Address + undefined-behaviour sanitizers (tests, tools)" OFF)

set(MODULAR_OSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/osp" CACHE PATH "ANDOR OSP checkout (submodule)")
set(MODULAR_ANALOG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/analog" CACHE PATH "ANDOR ANALOG checkout (submodule)")
foreach(dep OSP ANALOG)
    if(NOT EXISTS "${MODULAR_${dep}_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "${dep} not found at ${MODULAR_${dep}_DIR}: run `git submodule update --init` (not --recursive)")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Identity: ANDOR Modular's identifiers must be its own. Checked against every ANDOR plugin
# the pinned sources know (ANDOR OSP's registry and ANDOR ANALOG's product.json), without
# editing either repository.
# ---------------------------------------------------------------------------
file(READ "${MODULAR_OSP_DIR}/andor-design-system/product-registry.json" registry)
string(JSON registryCount LENGTH "${registry}" products)
math(EXPR registryLast "${registryCount} - 1")
foreach(i RANGE ${registryLast})
    string(JSON otherSlug GET "${registry}" products ${i} slug)
    string(JSON otherCode GET "${registry}" products ${i} pluginCode)
    string(JSON otherBundle GET "${registry}" products ${i} bundleId)
    string(JSON otherName GET "${registry}" products ${i} name)
    if(otherSlug STREQUAL MODULAR_SLUG OR otherCode STREQUAL MODULAR_CODE OR otherBundle STREQUAL MODULAR_BUNDLE OR otherName STREQUAL MODULAR_NAME)
        message(FATAL_ERROR "product/product.json: an identifier is ${otherName}'s (ANDOR OSP's product registry)")
    endif()
endforeach()
file(READ "${MODULAR_ANALOG_DIR}/product/product.json" analogJson)
foreach(key slug pluginCode bundleId name)
    string(JSON analogValue GET "${analogJson}" ${key})
    string(JSON ownValue GET "${MODULAR_PRODUCT_JSON}" ${key})
    if(analogValue STREQUAL ownValue)
        message(FATAL_ERROR "product/product.json: ${key} '${ownValue}' is ANDOR Analog's")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# ANDOR OSP: its libraries (osp_dsp, JUCE, signalsmith) as a subproject. Nothing of it is built
# unless Modular uses it (EXCLUDE_FROM_ALL); its own tests run in its own CI, not here.
# ---------------------------------------------------------------------------
set(OSP_BUILD_TESTS OFF)
set(OSP_BUILD_PLUGIN ${MODULAR_BUILD_PLUGIN})
set(ANDOR_BUILD_PRODUCTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${MODULAR_OSP_DIR} osp EXCLUDE_FROM_ALL)

if(MODULAR_BUILD_TESTS)
    include(FetchContent)
    set(MODULAR_CATCH2_DIR "" CACHE PATH "Local Catch2 checkout (skips download)")
    if(MODULAR_CATCH2_DIR)
        add_subdirectory(${MODULAR_CATCH2_DIR} ${CMAKE_BINARY_DIR}/_deps/catch2-build EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.9.1 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(Catch2)
    endif()
    enable_testing()
endif()

function(modular_set_warnings target)
    osp_set_warnings(${target})
    if(MODULAR_SANITIZE AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# ANDOR ANALOG's reusable DSP, compiled from the pinned checkout without changes: the
# oscillator models (header-only), filters, envelope, live morph, live granulator, chorus
# and its FX library. Not its synth (dsp/Synth.cpp) and not its plugin.
# ---------------------------------------------------------------------------
add_library(modular_analog_dsp STATIC
    ${MODULAR_ANALOG_DIR}/dsp/HalfBand.cpp
    ${MODULAR_ANALOG_DIR}/dsp/JunoChorus.cpp
    ${MODULAR_ANALOG_DIR}/dsp/SubOsc.cpp
    ${MODULAR_ANALOG_DIR}/dsp/LiveGranulator.cpp
    ${MODULAR_ANALOG_DIR}/dsp/morph/LiveMorph.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxGraph.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxEngine.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxProcessors.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxTone.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxDynamics.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxModulation.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxSpectral.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxRetro.cpp
    ${MODULAR_ANALOG_DIR}/dsp/fx/FxParameterTable.cpp)
target_include_directories(modular_analog_dsp SYSTEM PUBLIC ${MODULAR_ANALOG_DIR}/dsp)
target_link_libraries(modular_analog_dsp PUBLIC osp_dsp)
set_target_properties(modular_analog_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# The kernel.
# ---------------------------------------------------------------------------
add_library(andor_modular_dsp STATIC
    dsp/core/Json.cpp
    dsp/graph/Document.cpp
    dsp/graph/Registry.cpp
    dsp/graph/Validator.cpp
    dsp/graph/Compiler.cpp
    dsp/graph/Engine.cpp
    dsp/nodes/EventNodes.cpp
    dsp/nodes/SourceNodes.cpp
    dsp/nodes/ProcessorNodes.cpp
    dsp/nodes/ControlNodes.cpp
    dsp/nodes/SpatialNodes.cpp
    dsp/nodes/UtilityNodes.cpp
    dsp/nodes/FxNodes.cpp
    dsp/edit/Edits.cpp
    dsp/edit/Evolve.cpp
    dsp/edit/Groups.cpp
    dsp/edit/Factory.cpp
    dsp/io/Wav.cpp
    dsp/io/AssetLoader.cpp
    dsp/io/OfflineRender.cpp)
target_include_directories(andor_modular_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
target_link_libraries(andor_modular_dsp PUBLIC modular_analog_dsp osp_dsp)
target_compile_definitions(andor_modular_dsp PUBLIC MODULAR_VERSION_STRING="${MODULAR_VERSION}")
set_target_properties(andor_modular_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
modular_set_warnings(andor_modular_dsp)

add_subdirectory(tools)
if(MODULAR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(MODULAR_BUILD_PLUGIN)
    add_subdirectory(product)
endif()
