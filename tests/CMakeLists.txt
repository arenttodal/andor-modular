add_executable(modular_tests
    JsonTests.cpp
    GraphTests.cpp
    EngineTests.cpp
    NodeTests.cpp
    EditTests.cpp
    FactoryTests.cpp
    RealtimeTests.cpp
    ReliabilityTests.cpp
    ExternalTests.cpp
    FxTableTests.cpp)
target_include_directories(modular_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(modular_tests PRIVATE andor_modular_dsp Catch2::Catch2WithMain)
target_compile_definitions(modular_tests PRIVATE MODULAR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
modular_set_warnings(modular_tests)
include(Catch)
add_test(NAME modular_tests COMMAND modular_tests)
# RealtimeTests replaces the global operator new to count heap calls inside process().
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(modular_tests PRIVATE -Wno-mismatched-new-delete)
endif()
