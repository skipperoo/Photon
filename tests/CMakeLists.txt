find_package(Qt6 REQUIRED COMPONENTS Test Quick Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED libraw)

add_executable(tst_RawEngine
    auto/tst_RawEngine.cpp
    ../src/engine/RawEngine.cpp
    ../src/engine/RawEngine.h
)

target_link_libraries(tst_RawEngine
    PRIVATE Qt6::Test Qt6::Gui Qt6::Concurrent
    PRIVATE ${LIBRAW_LIBRARIES}
)

target_include_directories(tst_RawEngine PRIVATE
    ../src/engine
    ${LIBRAW_INCLUDE_DIRS}
)

add_test(NAME tst_RawEngine COMMAND tst_RawEngine)
