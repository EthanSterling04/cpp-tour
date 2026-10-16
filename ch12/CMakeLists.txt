# Header-only, so an INTERFACE library: nothing to compile, it only carries
# the include path (and lob_core's) to whatever links it.
add_library(lob_levels INTERFACE)
target_include_directories(lob_levels INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lob_levels INTERFACE lob_core)

add_tour_test(ch12_test test.cpp)
target_link_libraries(ch12_test PRIVATE lob_levels)

add_tour_bench(ch12_bench bench.cpp)
target_link_libraries(ch12_bench PRIVATE lob_levels)