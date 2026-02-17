find_package(Qt6 REQUIRED COMPONENTS Test Quick Concurrent Qml)
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

add_executable(tst_AppStateManager
    auto/tst_AppStateManager.cpp
    ../src/managers/AppStateManager.cpp
    ../src/managers/AppStateManager.h
)

target_link_libraries(tst_AppStateManager
    PRIVATE Qt6::Test Qt6::Qml
)

target_include_directories(tst_AppStateManager PRIVATE
    ../src
    ../src/managers
)

add_test(NAME tst_RawEngine COMMAND tst_RawEngine)
add_test(NAME tst_AppStateManager COMMAND tst_AppStateManager)
