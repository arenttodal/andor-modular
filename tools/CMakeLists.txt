add_executable(modular-render modular-render.cpp)
target_link_libraries(modular-render PRIVATE andor_modular_dsp)
modular_set_warnings(modular-render)

add_executable(modular-bench modular-bench.cpp)
target_link_libraries(modular-bench PRIVATE andor_modular_dsp)
modular_set_warnings(modular-bench)

add_executable(make-samples make-samples.cpp)
target_link_libraries(make-samples PRIVATE andor_modular_dsp)
modular_set_warnings(make-samples)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(modular-bench PRIVATE -Wno-mismatched-new-delete)   # it counts heap calls
endif()
