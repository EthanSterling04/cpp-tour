# The policies and the level they act on: a library, like Ch 3's lob_core.
add_library(lob_policy src/level.cpp src/policy.cpp)
target_include_directories(lob_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lob_policy
  PUBLIC  lob_core                       # lob/types.h: Price, Qty, OrderId
  PRIVATE tour_warnings tour_sanitize)

add_tour_test(ch05_test test.cpp)
target_link_libraries(ch05_test PRIVATE lob_policy)

# Run from build-bench only: sanitizers distort every timing.
add_tour_bench(ch05_bench bench.cpp)
target_link_libraries(ch05_bench PRIVATE lob_policy)