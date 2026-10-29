# Enables ASIO for a JUCE target only when a local Steinberg ASIO SDK is found.
# The SDK is never committed. Missing SDK is not an error: WASAPI/DirectSound stay available.
function(jackal_configure_asio target)
    if(JACKAL_ASIO_SDK_DIR AND EXISTS "${JACKAL_ASIO_SDK_DIR}/common/iasiodrv.h")
        target_compile_definitions(${target} PRIVATE JUCE_ASIO=1)
        target_include_directories(${target} PRIVATE "${JACKAL_ASIO_SDK_DIR}/common")
        message(STATUS "ASIO: enabled (${JACKAL_ASIO_SDK_DIR})")
    else()
        target_compile_definitions(${target} PRIVATE JUCE_ASIO=0)
        message(STATUS "ASIO: disabled, WASAPI/DirectSound only")
    endif()
endfunction()
