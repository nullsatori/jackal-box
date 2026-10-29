add_executable(jackal_tests
    TestHelpers.h
    RigHelpers.h
    TransportTests.cpp
    MetronomeTests.cpp
    CommandQueueTests.cpp
    InputMonitorTests.cpp
    LevelMeterTests.cpp
    EngineCoreTests.cpp
    SequencerTests.cpp
    VoiceTests.cpp
    SampleBankTests.cpp
    GlitchTests.cpp
    ProbabilityTests.cpp
    MutationTests.cpp
    RecordingTests.cpp
    ProjectJsonTests.cpp
    DemoKitTests.cpp
    SlicerTests.cpp
    PatternOpsTests.cpp
    Stage2CoreTests.cpp
    FxTestHelpers.h
    ShakalEffectTests.cpp
    ShakalChainTests.cpp
    MacroCurveTests.cpp
    FakeInsert.h
    LoopTestHelpers.h
    InsertChainTests.cpp
    LoopRecorderTests.cpp
    CalibrationTests.cpp
    GuitarStripTests.cpp
    SongTests.cpp
    CaptureTests.cpp
    MidiMapTests.cpp)

target_link_libraries(jackal_tests PRIVATE jackal_engine jackal_model Catch2::Catch2WithMain)

# the CPU budget test (K6) skips itself under sanitizers
if(CMAKE_CXX_FLAGS MATCHES "sanitize")
    target_compile_definitions(jackal_tests PRIVATE JACKAL_UNDER_SANITIZER=1)
endif()

if(MSVC)
    target_compile_options(jackal_tests PRIVATE /W4 /bigobj)
else()
    target_compile_options(jackal_tests PRIVATE -Wall -Wextra)
    find_package(Threads REQUIRED)
    target_link_libraries(jackal_tests PRIVATE Threads::Threads)
endif()

include(Catch)
catch_discover_tests(jackal_tests)
