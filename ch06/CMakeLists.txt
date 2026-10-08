add_tour_test(ch06_test test.cpp)

# lob::Price for the _ticks literal comes from chapter 3's library. If your
# chapter-3 library target has a different name, use that name here.
target_link_libraries(ch06_test PRIVATE lob_core)