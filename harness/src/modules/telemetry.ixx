// ============================================================================
// TopNotchNotes Harness - Telemetry Module
// JSON-based communication protocol for IPC
// ============================================================================

module;

#include <cstdint>
#include <string>
#include <string_view>
#include <mutex>
#include <chrono>
#include <format>
#include <print>
#include <utility>

export module harness:telemetry;

export namespace harness::telemetry
{

    // ============================================================================
    // Event Types
    // ============================================================================

    enum class EventType : std::uint8_t
    {
        Status,   // State change notification
        Text,     // Transcribed text
        Level,    // Audio level meter
        Error,    // Error notification
        Info,     // Informational message
        Heartbeat // Keep-alive
    };

    constexpr std::string_view to_string(EventType type) noexcept
    {
        using enum EventType;
        switch (type)
        {
        case Status:
            return "status";
        case Text:
            return "txt";
        case Level:
            return "level";
        case Error:
            return "err";
        case Info:
            return "info";
        case Heartbeat:
            return "heartbeat";
        }
        std::unreachable();
    }

    // ============================================================================
    // JSON Escape Utility
    // ============================================================================

    /// Escape a string for JSON output
    [[nodiscard]] inline std::string json_escape(std::string_view input)
    {
        std::string result;
        result.reserve(input.size() + 8);

        for (char c : input)
        {
            switch (c)
            {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    std::format_to(std::back_inserter(result), "\\u{:04x}",
                                   static_cast<unsigned>(c));
                }
                else
                {
                    result += c;
                }
            }
        }

        return result;
    }

    // ============================================================================
    // Telemetry Emitter
    // ============================================================================

    /// Thread-safe telemetry emitter using std::print
    class Emitter
    {
    public:
        Emitter() = default;

        /// Emit a status event
        void status(std::string_view state)
        {
            emit(EventType::Status, "state", state);
        }

        /// Emit a transcribed text event
        void text(std::string_view content)
        {
            emit(EventType::Text, "body", content);
        }

        /// Emit a transcribed text with timestamp
        void text(std::string_view content, std::chrono::milliseconds timestamp)
        {
            std::lock_guard lock(mutex_);
            std::print(stdout, "{{\"evt\":\"{}\",\"body\":\"{}\",\"time\":{}}}\n",
                       to_string(EventType::Text),
                       json_escape(content),
                       timestamp.count());
            std::fflush(stdout);
        }

        /// Emit an audio level event
        void level(float db)
        {
            std::lock_guard lock(mutex_);
            std::print(stdout, "{{\"evt\":\"{}\",\"db\":{:.1f}}}\n",
                       to_string(EventType::Level), db);
            std::fflush(stdout);
        }

        /// Emit an error event
        void error(std::string_view message)
        {
            emit(EventType::Error, "body", message);
        }

        /// Emit an info event
        void info(std::string_view message)
        {
            emit(EventType::Info, "body", message);
        }

        /// Emit a heartbeat
        void heartbeat()
        {
            std::lock_guard lock(mutex_);
            auto now = std::chrono::system_clock::now();
            auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch())
                             .count();
            std::print(stdout, "{{\"evt\":\"{}\",\"ts\":{}}}\n",
                       to_string(EventType::Heartbeat), epoch);
            std::fflush(stdout);
        }

        /// Emit session start info
        void session_start(std::string_view session_id,
                           std::string_view output_path)
        {
            std::lock_guard lock(mutex_);
            std::print(stdout,
                       "{{\"evt\":\"session\",\"action\":\"start\",\"id\":\"{}\",\"path\":\"{}\"}}\n",
                       json_escape(session_id), json_escape(output_path));
            std::fflush(stdout);
        }

        /// Emit session end info
        void session_end(std::string_view session_id,
                         std::size_t bytes_written,
                         std::chrono::seconds duration)
        {
            std::lock_guard lock(mutex_);
            std::print(stdout,
                       "{{\"evt\":\"session\",\"action\":\"end\",\"id\":\"{}\",\"bytes\":{},\"duration\":{}}}\n",
                       json_escape(session_id), bytes_written, duration.count());
            std::fflush(stdout);
        }

    private:
        void emit(EventType type, std::string_view key, std::string_view value)
        {
            std::lock_guard lock(mutex_);
            std::print(stdout, "{{\"evt\":\"{}\",\"{}\":\"{}\"}}\n",
                       to_string(type), key, json_escape(value));
            std::fflush(stdout);
        }

        std::mutex mutex_;
    };

    // ============================================================================
    // Global Emitter Instance
    // ============================================================================

    /// Get the global telemetry emitter
    inline Emitter &global()
    {
        static Emitter instance;
        return instance;
    }

    // ============================================================================
    // Convenience Functions
    // ============================================================================

    inline void emit_status(std::string_view state) { global().status(state); }
    inline void emit_text(std::string_view text) { global().text(text); }
    inline void emit_level(float db) { global().level(db); }
    inline void emit_error(std::string_view msg) { global().error(msg); }
    inline void emit_info(std::string_view msg) { global().info(msg); }

} // namespace harness::telemetry
