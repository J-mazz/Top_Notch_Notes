// ============================================================================
// TopNotchNotes Harness - Core Module Interface
// Modern C++23 module providing the foundational types and abstractions
// ============================================================================

module;

#include <cstdint>
#include <string>
#include <string_view>
#include <expected>
#include <span>
#include <optional>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <print>

export module harness;

// Re-export submodules
export import :audio;
export import :transcribe;
export import :io;
export import :ringbuffer;
export import :telemetry;

export namespace harness
{

    // ============================================================================
    // Core Type Aliases for Modern Safety
    // ============================================================================

    /// A non-owning view of an audio frame's PCM samples
    using AudioFrame = std::span<const float>;

    /// A mutable view for writing audio data
    using AudioFrameMut = std::span<float>;

    /// Result type for hardware operations - replaces exceptions
    template <typename T>
    using HardwareResult = std::expected<T, std::string>;

    /// Result type for void operations
    using VoidResult = std::expected<void, std::string>;

    // ============================================================================
    // Recording State Machine
    // ============================================================================

    enum class RecordingState : std::uint8_t
    {
        Idle,
        Recording,
        Paused,
        Error
    };

    constexpr std::string_view to_string(RecordingState state) noexcept
    {
        using enum RecordingState;
        switch (state)
        {
        case Idle:
            return "idle";
        case Recording:
            return "recording";
        case Paused:
            return "paused";
        case Error:
            return "error";
        }
        std::unreachable();
    }

    // ============================================================================
    // Command Protocol
    // ============================================================================

    enum class Command : std::uint8_t
    {
        Start,
        Stop,
        Pause,
        Resume,
        Kill,
        Status,
        Unknown
    };

    constexpr Command parse_command(std::string_view cmd) noexcept
    {
        using enum Command;
        if (cmd == "START")
            return Start;
        if (cmd == "STOP")
            return Stop;
        if (cmd == "PAUSE")
            return Pause;
        if (cmd == "RESUME")
            return Resume;
        if (cmd == "KILL")
            return Kill;
        if (cmd == "STATUS")
            return Status;
        return Unknown;
    }

    // ============================================================================
    // Audio Configuration
    // ============================================================================

    struct AudioConfig
    {
        std::uint32_t sample_rate = 48000;
        std::uint32_t channels = 1;         // Mono for voice
        std::uint32_t buffer_frames = 1024; // Frames per callback
        std::uint32_t bit_depth = 32;       // 32-bit float

        [[nodiscard]] constexpr std::size_t buffer_size_bytes() const noexcept
        {
            return buffer_frames * channels * sizeof(float);
        }

        [[nodiscard]] constexpr double buffer_duration_ms() const noexcept
        {
            return (static_cast<double>(buffer_frames) / sample_rate) * 1000.0;
        }
    };

    // ============================================================================
    // Session Information
    // ============================================================================

    struct SessionInfo
    {
        std::string session_id;
        std::filesystem::path output_dir;
        std::chrono::system_clock::time_point start_time;
        AudioConfig audio_config;

        [[nodiscard]] std::filesystem::path audio_file_path() const
        {
            return output_dir / (session_id + ".raw");
        }

        [[nodiscard]] std::filesystem::path transcript_file_path() const
        {
            return output_dir / (session_id + ".txt");
        }
    };

    // ============================================================================
    // Utility: Generate Session ID
    // ============================================================================

    [[nodiscard]] inline std::string generate_session_id()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
        localtime_r(&time_t, &tm_buf);

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
        return oss.str();
    }

    // ============================================================================
    // Version Information
    // ============================================================================

    struct Version
    {
        static constexpr int major = 1;
        static constexpr int minor = 0;
        static constexpr int patch = 0;

        static constexpr std::string_view string() noexcept
        {
            return "1.0.0";
        }
    };

    /// Print greeting/version info
    inline void print_banner()
    {
        std::print("TopNotchNotes Harness v{}\n", Version::string());
        std::print("C++23 Audio/Video Hardware Orchestration Daemon\n");
        std::print("Ready for commands on stdin\n");
    }

} // namespace harness
