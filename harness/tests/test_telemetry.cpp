// ============================================================================
// TopNotchNotes Harness - Telemetry Tests
// ============================================================================

#include <print>
#include <string>
#include <string_view>

import harness;

namespace
{

    bool test_json_escape()
    {
        using harness::telemetry::json_escape;

        // Test basic string
        if (json_escape("hello") != "hello")
            return false;

        // Test special characters
        if (json_escape("hello\"world") != "hello\\\"world")
            return false;
        if (json_escape("hello\\world") != "hello\\\\world")
            return false;
        if (json_escape("hello\nworld") != "hello\\nworld")
            return false;
        if (json_escape("hello\tworld") != "hello\\tworld")
            return false;

        // Test control characters
        std::string ctrl;
        ctrl += '\x01';
        if (json_escape(ctrl) != "\\u0001")
            return false;

        return true;
    }

    bool test_command_parsing()
    {
        using harness::Command;
        using harness::parse_command;

        if (parse_command("START") != Command::Start)
            return false;
        if (parse_command("STOP") != Command::Stop)
            return false;
        if (parse_command("PAUSE") != Command::Pause)
            return false;
        if (parse_command("RESUME") != Command::Resume)
            return false;
        if (parse_command("KILL") != Command::Kill)
            return false;
        if (parse_command("STATUS") != Command::Status)
            return false;
        if (parse_command("INVALID") != Command::Unknown)
            return false;
        if (parse_command("start") != Command::Unknown)
            return false; // Case sensitive

        return true;
    }

    bool test_state_to_string()
    {
        using harness::RecordingState;
        using harness::to_string;

        if (to_string(RecordingState::Idle) != "idle")
            return false;
        if (to_string(RecordingState::Recording) != "recording")
            return false;
        if (to_string(RecordingState::Paused) != "paused")
            return false;
        if (to_string(RecordingState::Error) != "error")
            return false;

        return true;
    }

    bool test_session_id_generation()
    {
        auto id1 = harness::generate_session_id();

        // Should be in format YYYYMMDD_HHMMSS
        if (id1.length() != 15)
            return false;
        if (id1[8] != '_')
            return false;

        // All characters should be digits or underscore
        for (char c : id1)
        {
            if (c != '_' && (c < '0' || c > '9'))
                return false;
        }

        return true;
    }

    bool test_audio_config()
    {
        harness::AudioConfig config{
            .sample_rate = 48000,
            .channels = 1,
            .buffer_frames = 1024,
            .bit_depth = 32};

        // Buffer size should be 1024 * 1 * 4 = 4096 bytes
        if (config.buffer_size_bytes() != 4096)
            return false;

        // Duration should be ~21.33ms
        double duration = config.buffer_duration_ms();
        if (duration < 21.0 || duration > 22.0)
            return false;

        return true;
    }

} // anonymous namespace

int run_telemetry_tests()
{
    int passed = 0;
    int failed = 0;

    auto run = [&](const char *name, bool (*test)())
    {
        if (test())
        {
            std::print("[PASS] {}\n", name);
            ++passed;
        }
        else
        {
            std::print("[FAIL] {}\n", name);
            ++failed;
        }
    };

    run("json_escape", test_json_escape);
    run("command_parsing", test_command_parsing);
    run("state_to_string", test_state_to_string);
    run("session_id_generation", test_session_id_generation);
    run("audio_config", test_audio_config);

    std::print("\nTelemetry Tests: {} passed, {} failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

// Declare external test functions
extern int run_ringbuffer_tests();

int main(int argc, char *argv[])
{
    bool run_ringbuffer = false;
    bool run_telemetry = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--ringbuffer")
            run_ringbuffer = true;
        if (arg == "--telemetry")
            run_telemetry = true;
        if (arg == "--all")
        {
            run_ringbuffer = true;
            run_telemetry = true;
        }
    }

    // If no specific tests requested, run all
    if (!run_ringbuffer && !run_telemetry)
    {
        run_ringbuffer = true;
        run_telemetry = true;
    }

    int result = 0;

    if (run_ringbuffer)
    {
        result |= run_ringbuffer_tests();
    }

    if (run_telemetry)
    {
        result |= run_telemetry_tests();
    }

    return result;
}
