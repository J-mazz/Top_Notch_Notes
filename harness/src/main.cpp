// ============================================================================
// TopNotchNotes Harness - Main Entry Point
// C++23 Audio/Video Hardware Orchestration Daemon
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import harness;

// ============================================================================
// Global State (Atomic for lock-free access)
// ============================================================================

std::atomic<harness::RecordingState> g_state{harness::RecordingState::Idle};
std::atomic<bool> g_should_exit{false};

// Current session information
struct Session
{
    std::string id;
    std::filesystem::path output_dir;
    std::chrono::steady_clock::time_point start_time;
    std::unique_ptr<harness::io::WavWriter> audio_writer;
    std::unique_ptr<harness::transcribe::ITranscribeEngine> transcriber;
    std::ofstream transcript_file;
    std::size_t frame_count = 0;
};

std::optional<Session> g_session;
std::mutex g_session_mutex;

// ============================================================================
// Command Handlers (Split for reduced complexity)
// ============================================================================

namespace cmd
{

    void start_recording(std::string_view output_dir)
    {
        using namespace harness;
        std::lock_guard lock(g_session_mutex);

        if (g_state == RecordingState::Recording)
        {
            telemetry::emit_error("Already recording");
            return;
        }

        auto session_id = generate_session_id();
        auto session_path = output_dir.empty()
                                ? std::filesystem::current_path() / "recordings" / session_id
                                : std::filesystem::path(output_dir) / session_id;

        std::filesystem::create_directories(session_path);

        Session session;
        session.id = session_id;
        session.output_dir = session_path;
        session.start_time = std::chrono::steady_clock::now();

        // Create audio writer
        auto audio_path = session_path / (session_id + ".wav");
        auto writer_result = io::WavWriter::create(audio_path, 48000, 1);
        if (!writer_result)
        {
            telemetry::emit_error("Failed to create audio file: " + writer_result.error());
            return;
        }
        session.audio_writer = std::make_unique<io::WavWriter>(std::move(*writer_result));

        // Create transcriber
        transcribe::TranscribeConfig tc_config;
        session.transcriber = transcribe::create_engine(tc_config);

        // Open transcript file
        auto transcript_path = session_path / (session_id + ".md");
        session.transcript_file.open(transcript_path);
        session.transcript_file << "# Recording Session: " << session_id << "\n\n";
        session.transcript_file << "---\n\n";

        g_session = std::move(session);
        g_state = RecordingState::Recording;

        telemetry::global().session_start(g_session->id, session_path.string());
        telemetry::emit_status("recording");
    }

    void stop_recording()
    {
        using namespace harness;
        std::lock_guard lock(g_session_mutex);

        if (g_state == RecordingState::Idle)
        {
            telemetry::emit_error("Not recording");
            return;
        }

        if (g_session)
        {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - g_session->start_time);

            g_session->audio_writer->close();
            g_session->transcript_file.close();

            telemetry::global().session_end(
                g_session->id,
                g_session->audio_writer->samples_written() * sizeof(float),
                duration);

            g_session.reset();
        }

        g_state = RecordingState::Idle;
        telemetry::emit_status("idle");
    }

    void pause_recording()
    {
        using namespace harness;
        if (g_state == RecordingState::Recording)
        {
            g_state = RecordingState::Paused;
            telemetry::emit_status("paused");
        }
        else
        {
            telemetry::emit_error("Not recording");
        }
    }

    void resume_recording()
    {
        using namespace harness;
        if (g_state == RecordingState::Paused)
        {
            g_state = RecordingState::Recording;
            telemetry::emit_status("recording");
        }
        else
        {
            telemetry::emit_error("Not paused");
        }
    }

} // namespace cmd

// Dispatch command to appropriate handler
void handle_command(harness::Command command, std::string_view output_dir = "")
{
    using namespace harness;

    switch (command)
    {
    case Command::Start:
        cmd::start_recording(output_dir);
        break;
    case Command::Stop:
        cmd::stop_recording();
        break;
    case Command::Pause:
        cmd::pause_recording();
        break;
    case Command::Resume:
        cmd::resume_recording();
        break;
    case Command::Status:
        telemetry::emit_status(std::string(to_string(g_state.load())));
        break;
    case Command::Kill:
        if (g_state == RecordingState::Recording)
            cmd::stop_recording();
        g_should_exit = true;
        telemetry::emit_info("Shutting down");
        break;
    case Command::Unknown:
        telemetry::emit_error("Unknown command");
        break;
    }
}

// ============================================================================
// Command Listener Thread
// ============================================================================

void command_listener()
{
    std::string line;
    while (!g_should_exit && std::getline(std::cin, line))
    {
        // Trim whitespace
        auto start = line.find_first_not_of(" \t\r\n");
        auto end = line.find_last_not_of(" \t\r\n");

        if (start == std::string::npos)
            continue;

        std::string_view cmd_str(line.data() + start, end - start + 1);

        // Check for command with argument (e.g., "START /path/to/output")
        auto space_pos = cmd_str.find(' ');
        std::string_view cmd_part = cmd_str.substr(0, space_pos);
        std::string_view arg_part = (space_pos != std::string_view::npos)
                                        ? cmd_str.substr(space_pos + 1)
                                        : "";

        auto cmd = harness::parse_command(cmd_part);
        handle_command(cmd, arg_part);
    }
}

// ============================================================================
// Audio Processing Loop
// ============================================================================

void process_audio_frame(harness::audio::AudioFrame frame)
{
    using namespace harness;

    std::lock_guard lock(g_session_mutex);

    if (!g_session || g_state != RecordingState::Recording)
    {
        return;
    }

    // 1. Write audio to disk
    g_session->audio_writer->write(frame);

    // 2. Calculate and emit level
    float db = audio::calculate_db_level(frame);
    if (g_session->frame_count % 5 == 0)
    { // Emit every 5 frames (~100ms)
        telemetry::emit_level(db);
    }

    // 3. Attempt transcription
    if (g_session->transcriber && audio::detect_voice_activity(frame))
    {
        if (auto text = transcribe::transcribe(*g_session->transcriber, frame))
        {
            telemetry::emit_text(*text);

            // Also write to transcript file
            g_session->transcript_file << *text << " ";
            g_session->transcript_file.flush();
        }
    }

    ++g_session->frame_count;
}

// ============================================================================
// Initialization Helpers
// ============================================================================

struct AppConfig
{
    bool verbose = false;
};

[[nodiscard]] AppConfig parse_args(int argc, char *argv[])
{
    AppConfig config;
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "-v" || arg == "--verbose")
        {
            config.verbose = true;
        }
    }
    return config;
}

[[nodiscard]] std::expected<harness::audio::AudioDevice, std::string> init_audio()
{
    harness::audio::DeviceConfig config{
        .sample_rate = 48000,
        .channels = 1,
        .buffer_frames = 1024};
    return harness::audio::AudioDevice::create(config);
}

void run_audio_loop(harness::audio::AudioDevice &device)
{
    for (const auto &frame : harness::audio::create_audio_stream(device))
    {
        if (g_should_exit)
            break;
        process_audio_frame(frame);
    }
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char *argv[])
{
    using namespace harness;

    auto config = parse_args(argc, argv);
    if (config.verbose)
        print_banner();

    telemetry::emit_status("ready");

    auto device_result = init_audio();
    if (!device_result)
    {
        telemetry::emit_error("Failed to initialize audio device: " + device_result.error());
        return 1;
    }
    auto &device = *device_result;

    std::jthread commander(command_listener);

    if (auto result = device.start(); !result)
    {
        telemetry::emit_error("Failed to start audio device: " + result.error());
        return 1;
    }
    telemetry::emit_info("Audio device started");

    run_audio_loop(device);

    (void)device.stop();
    if (g_state == RecordingState::Recording)
        cmd::stop_recording();
    telemetry::emit_status("stopped");

    return 0;
}
