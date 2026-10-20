# CoreMark (EEMBC, Apache 2.0): the algorithms of the "CoreMark" workload of the CPU stress test, built as plain C with -O2
# like the official run rules ask for, regardless of the build type.
add_library(coremark_kernel STATIC
        core_list_join.c
        core_matrix.c
        core_state.c
        core_util.c
        coremark_kernel.c)
target_include_directories(coremark_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(coremark_kernel PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(coremark_kernel PRIVATE -O2)
endif ()
