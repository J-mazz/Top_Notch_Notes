# Tests for TopNotchNotes Harness

enable_testing()

# Test executable
add_executable(harness_tests
    test_ringbuffer.cpp
    test_telemetry.cpp
)

target_link_libraries(harness_tests
    PRIVATE
        harness_modules
        Threads::Threads
)

# Register tests
add_test(NAME RingBufferTests COMMAND harness_tests --ringbuffer)
add_test(NAME TelemetryTests COMMAND harness_tests --telemetry)
