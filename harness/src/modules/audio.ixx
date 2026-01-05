// ============================================================================
// TopNotchNotes Harness - Audio Device Module
// C++23 Generator-based audio streaming abstraction
// ============================================================================

module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <expected>
#include <span>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <coroutine>
#include <cmath>
#include <print>
#include "generator.hpp"

export module harness:audio;

export namespace harness::audio
{

    // ============================================================================
    // Forward Declarations (miniaudio types wrapped)
    // ============================================================================

    /// Opaque handle to the underlying audio device (stub for now)
    struct DeviceHandle
    {
        // Will contain miniaudio device when integrated
        bool initialized = false;
    };

    /// Audio frame as a span of floats
    using AudioFrame = std::span<const float>;
    using AudioFrameMut = std::span<float>;

    /// Result type for audio operations
    template <typename T>
    using AudioResult = std::expected<T, std::string>;

    // ============================================================================
    // Audio Device Configuration
    // ============================================================================

    struct DeviceConfig
    {
        std::uint32_t sample_rate = 48000;
        std::uint32_t channels = 1;
        std::uint32_t buffer_frames = 1024;
        std::string device_name = ""; // Empty = default device
        bool enable_loopback = false;
    };

    // ============================================================================
    // Audio Device Information
    // ============================================================================

    struct DeviceInfo
    {
        std::string id;
        std::string name;
        bool is_default;
        std::uint32_t max_channels;
        std::uint32_t min_sample_rate;
        std::uint32_t max_sample_rate;
    };

    // ============================================================================
    // Audio Device Class
    // ============================================================================

    /// RAII wrapper around the audio capture device
    class AudioDevice
    {
    public:
        // Construction
        static AudioResult<AudioDevice> create(const DeviceConfig &config);

        // Move-only semantics
        AudioDevice(AudioDevice &&other) noexcept;
        AudioDevice &operator=(AudioDevice &&other) noexcept;
        AudioDevice(const AudioDevice &) = delete;
        AudioDevice &operator=(const AudioDevice &) = delete;

        // Destructor - stops and releases device
        ~AudioDevice();

        // Device Control
        [[nodiscard]] AudioResult<void> start();
        [[nodiscard]] AudioResult<void> stop();
        [[nodiscard]] bool is_active() const noexcept;

        // Data Access
        /// Block until audio data is available, then return a view
        [[nodiscard]] AudioResult<AudioFrame> wait_for_data();

        /// Non-blocking: get data if available
        [[nodiscard]] std::optional<AudioFrame> try_get_data() noexcept;

        // Device Information
        [[nodiscard]] const DeviceConfig &config() const noexcept { return config_; }
        [[nodiscard]] std::string_view name() const noexcept { return device_name_; }

        // Static: Enumerate available devices
        static std::vector<DeviceInfo> enumerate_devices();

    private:
        explicit AudioDevice(const DeviceConfig &config);

        DeviceConfig config_;
        std::string device_name_;
        std::unique_ptr<DeviceHandle> handle_;
        std::vector<float> buffer_;
        std::atomic<bool> active_{false};
        std::atomic<bool> data_ready_{false};
        std::mutex mutex_;
        std::condition_variable cv_;
    };

    // ============================================================================
    // C++23 Generator: The Modern Primitive
    // ============================================================================

    /// Generator coroutine that yields audio frames from the device
    /// This inverts control: instead of callbacks pushing data, we pull it
    harness::generator<AudioFrame> create_audio_stream(AudioDevice &device)
    {
        while (device.is_active())
        {
            auto result = device.wait_for_data();
            if (result)
            {
                co_yield *result;
            }
            else
            {
                // Log error but continue
                std::print(stderr, "Audio read error: {}\n", result.error());
            }
        }
    }

    // ============================================================================
    // Audio Level Calculation
    // ============================================================================

    /// Calculate RMS level in decibels from audio frame
    [[nodiscard]] inline float calculate_db_level(AudioFrame frame) noexcept
    {
        if (frame.empty())
            return -100.0f;

        float sum_squares = 0.0f;
        for (float sample : frame)
        {
            sum_squares += sample * sample;
        }

        float rms = std::sqrt(sum_squares / static_cast<float>(frame.size()));

        // Convert to dB (reference: 1.0 = 0 dB)
        if (rms < 1e-10f)
            return -100.0f;
        return 20.0f * std::log10(rms);
    }

    /// Detect voice activity (simple energy-based)
    [[nodiscard]] inline bool detect_voice_activity(AudioFrame frame, float threshold_db = -40.0f) noexcept
    {
        return calculate_db_level(frame) > threshold_db;
    }

    // ============================================================================
    // Implementation Details (in separate .cpp or inline here for header-only)
    // ============================================================================

    // Note: Actual miniaudio integration would go here
    // For now, we provide a stub implementation

    AudioDevice::AudioDevice(const DeviceConfig &config)
        : config_(config), device_name_("Default Audio Input"), buffer_(config.buffer_frames * config.channels)
    {
    }

    AudioDevice::AudioDevice(AudioDevice &&other) noexcept
        : config_(std::move(other.config_)), device_name_(std::move(other.device_name_)), handle_(std::move(other.handle_)), buffer_(std::move(other.buffer_)), active_(other.active_.load())
    {
    }

    AudioDevice &AudioDevice::operator=(AudioDevice &&other) noexcept
    {
        if (this != &other)
        {
            config_ = std::move(other.config_);
            device_name_ = std::move(other.device_name_);
            handle_ = std::move(other.handle_);
            buffer_ = std::move(other.buffer_);
            active_ = other.active_.load();
        }
        return *this;
    }

    AudioDevice::~AudioDevice()
    {
        if (active_)
        {
            [[maybe_unused]] auto _ = stop();
        }
    }

    AudioResult<AudioDevice> AudioDevice::create(const DeviceConfig &config)
    {
        AudioDevice device(config);
        // In real implementation: initialize miniaudio device here
        return device;
    }

    AudioResult<void> AudioDevice::start()
    {
        active_ = true;
        // In real implementation: start the miniaudio device
        return {};
    }

    AudioResult<void> AudioDevice::stop()
    {
        active_ = false;
        cv_.notify_all();
        // In real implementation: stop the miniaudio device
        return {};
    }

    bool AudioDevice::is_active() const noexcept
    {
        return active_.load(std::memory_order_relaxed);
    }

    AudioResult<AudioFrame> AudioDevice::wait_for_data()
    {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this]
                 { return data_ready_ || !active_; });

        if (!active_)
        {
            return std::unexpected("Device stopped");
        }

        data_ready_ = false;
        return AudioFrame(buffer_);
    }

    std::optional<AudioFrame> AudioDevice::try_get_data() noexcept
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || !data_ready_)
        {
            return std::nullopt;
        }
        data_ready_ = false;
        return AudioFrame(buffer_);
    }

    std::vector<DeviceInfo> AudioDevice::enumerate_devices()
    {
        // In real implementation: query miniaudio for available devices
        return {{.id = "default",
                 .name = "Default Audio Input",
                 .is_default = true,
                 .max_channels = 2,
                 .min_sample_rate = 8000,
                 .max_sample_rate = 192000}};
    }

} // namespace harness::audio
