add_library(lob_book src/book.cpp)
target_include_directories(lob_book PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lob_book
  PUBLIC  lob_core                       # lob/types.h: Price, Qty, OrderId
  PRIVATE tour_warnings tour_sanitize)

add_tour_test(ch15_test test.cpp)
target_link_libraries(ch15_test PRIVATE lob_book)

add_tour_bench(ch15_bench bench.cpp)