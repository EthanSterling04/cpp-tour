# The library: one translation unit today, the start of the order book's core.
add_library(lob_core src/order.cpp)
target_include_directories(lob_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lob_core PRIVATE tour_warnings tour_sanitize)

# The test: a separate executable that links the library.
add_tour_test(ch03_test test.cpp)
target_link_libraries(ch03_test PRIVATE lob_core)