include(FetchContent)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.14
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(juce)
