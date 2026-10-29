add_executable(jackal_tests
    TestHelpers.h
    TransportTests.cpp
    MetronomeTests.cpp
    CommandQueueTests.cpp
    InputMonitorTests.cpp
    LevelMeterTests.cpp
    EngineCoreTests.cpp)

target_link_libraries(jackal_tests PRIVATE jackal_engine Catch2::Catch2WithMain)

if(MSVC)
    target_compile_options(jackal_tests PRIVATE /W4)
else()
    target_compile_options(jackal_tests PRIVATE -Wall -Wextra)
    find_package(Threads REQUIRED)
    target_link_libraries(jackal_tests PRIVATE Threads::Threads)
endif()

include(Catch)
catch_discover_tests(jackal_tests)
