// ============================================================================
// TopNotchNotes Harness - Audio Device Module
// C++23 Generator-based audio streaming with miniaudio backend
// ============================================================================

module;

#include <cstdint>
#include <cstring>
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

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "generator.hpp"

export module harness:audio;

import :ringbuffer;

export namespace harness::audio {

// ============================================================================
// Type Aliases
// ============================================================================

using AudioFrame = std::span<const float>;
using AudioFrameMut = std::span<float>;

template<typename T>
using AudioResult = std::expected<T, std::string>;

// ============================================================================
// Audio Device Configuration
// ============================================================================

struct DeviceConfig {
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 1;
    std::uint32_t buffer_frames = 1024;
    std::string device_name = "";  // Empty = default device
    bool enable_loopback = false;
};

// ============================================================================
// Audio Device Information
// ============================================================================

struct DeviceInfo {
    std::string id;
    std::string name;
    bool is_default;
    std::uint32_t max_channels;
    std::uint32_t min_sample_rate;
    std::uint32_t max_sample_rate;
};

// ============================================================================
// Audio Device Handle (miniaudio integration)
// ============================================================================

struct DeviceHandle {
    ma_device device;
    ma_context context;
    bool context_initialized = false;
    bool device_initialized = false;
};

// ============================================================================
// Audio Device Class
// ============================================================================

class AudioDevice {
public:
    static AudioResult<AudioDevice> create(const DeviceConfig& config);
    
    AudioDevice(AudioDevice&& other) noexcept;
    AudioDevice& operator=(AudioDevice&& other) noexcept;
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    
    ~AudioDevice();
    
    [[nodiscard]] AudioResult<void> start();
    [[nodiscard]] AudioResult<void> stop();
    [[nodiscard]] bool is_active() const noexcept;
    
    [[nodiscard]] AudioResult<AudioFrame> wait_for_data();
    [[nodiscard]] std::optional<AudioFrame> try_get_data() noexcept;
    
    [[nodiscard]] const DeviceConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::string_view name() const noexcept { return device_name_; }
    
    static std::vector<DeviceInfo> enumerate_devices();

    // Called from audio callback
    void on_audio_data(const float* samples, std::size_t frame_count);

private:
    explicit AudioDevice(const DeviceConfig& config);
    
    DeviceConfig config_;
    std::string device_name_;
    std::unique_ptr<DeviceHandle> handle_;
    
    // Ring buffer for lock-free audio transfer from callback
    harness::AudioRingBuffer ring_buffer_;
    
    // Consumer-side buffer for returning frames
    std::vector<float> frame_buffer_;
    
    std::atomic<bool> active_{false};
    std::atomic<bool> data_ready_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
};

// ============================================================================
// Miniaudio Callback (C linkage)
// ============================================================================

inline void audio_data_callback(ma_device* pDevice, void* pOutput, 
                                const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;  // Capture only, no playback
    
    auto* device = static_cast<AudioDevice*>(pDevice->pUserData);
    if (device && pInput) {
        const auto* samples = static_cast<const float*>(pInput);
        device->on_audio_data(samples, frameCount);
    }
}

// ============================================================================
// AudioDevice Implementation
// ============================================================================

AudioDevice::AudioDevice(const DeviceConfig& config)
    : config_(config)
    , handle_(std::make_unique<DeviceHandle>())
    , frame_buffer_(config.buffer_frames * config.channels)
{
}

AudioDevice::AudioDevice(AudioDevice&& other) noexcept
    : config_(std::move(other.config_))
    , device_name_(std::move(other.device_name_))
    , handle_(std::move(other.handle_))
    , frame_buffer_(std::move(other.frame_buffer_))
    , active_(other.active_.load())
{
    other.active_ = false;
}

AudioDevice& AudioDevice::operator=(AudioDevice&& other) noexcept {
    if (this != &other) {
        if (active_) {
            (void)stop();
        }
        config_ = std::move(other.config_);
        device_name_ = std::move(other.device_name_);
        handle_ = std::move(other.handle_);
        frame_buffer_ = std::move(other.frame_buffer_);
        active_ = other.active_.load();
        other.active_ = false;
    }
    return *this;
}

AudioDevice::~AudioDevice() {
    if (active_) {
        (void)stop();
    }
    if (handle_) {
        if (handle_->device_initialized) {
            ma_device_uninit(&handle_->device);
        }
        if (handle_->context_initialized) {
            ma_context_uninit(&handle_->context);
        }
    }
}

AudioResult<AudioDevice> AudioDevice::create(const DeviceConfig& config) {
    AudioDevice device(config);
    
    // Initialize context
    ma_context_config ctx_config = ma_context_config_init();
    if (ma_context_init(nullptr, 0, &ctx_config, &device.handle_->context) != MA_SUCCESS) {
        return std::unexpected("Failed to initialize audio context");
    }
    device.handle_->context_initialized = true;
    
    // Configure capture device
    ma_device_config dev_config = ma_device_config_init(ma_device_type_capture);
    dev_config.capture.format = ma_format_f32;
    dev_config.capture.channels = config.channels;
    dev_config.sampleRate = config.sample_rate;
    dev_config.periodSizeInFrames = config.buffer_frames;
    dev_config.dataCallback = audio_data_callback;
    dev_config.pUserData = &device;
    
    if (ma_device_init(&device.handle_->context, &dev_config, &device.handle_->device) != MA_SUCCESS) {
        return std::unexpected("Failed to initialize capture device");
    }
    device.handle_->device_initialized = true;
    
    // Get device name
    device.device_name_ = device.handle_->device.capture.name;
    
    return device;
}

AudioResult<void> AudioDevice::start() {
    if (!handle_ || !handle_->device_initialized) {
        return std::unexpected("Device not initialized");
    }
    
    if (ma_device_start(&handle_->device) != MA_SUCCESS) {
        return std::unexpected("Failed to start audio capture");
    }
    
    active_ = true;
    return {};
}

AudioResult<void> AudioDevice::stop() {
    active_ = false;
    cv_.notify_all();
    
    if (handle_ && handle_->device_initialized) {
        ma_device_stop(&handle_->device);
    }
    
    return {};
}

bool AudioDevice::is_active() const noexcept {
    return active_.load(std::memory_order_relaxed);
}

void AudioDevice::on_audio_data(const float* samples, std::size_t frame_count) {
    // Push samples into ring buffer (lock-free, called from audio thread)
    std::size_t sample_count = frame_count * config_.channels;
    ring_buffer_.push(std::span<const float>(samples, sample_count));
    
    // Signal that data is available
    data_ready_.store(true, std::memory_order_release);
    cv_.notify_one();
}

AudioResult<AudioFrame> AudioDevice::wait_for_data() {
    std::unique_lock lock(mutex_);
    
    cv_.wait(lock, [this] { 
        return data_ready_.load(std::memory_order_acquire) || !active_; 
    });
    
    if (!active_) {
        return std::unexpected("Device stopped");
    }
    
    // Pop data from ring buffer into frame buffer
    std::size_t expected_samples = config_.buffer_frames * config_.channels;
    std::size_t available = ring_buffer_.size();
    std::size_t to_read = std::min(available, expected_samples);
    
    if (to_read > 0) {
        ring_buffer_.pop(std::span<float>(frame_buffer_.data(), to_read));
    }
    
    // Check if we need more data before signaling ready again
    if (ring_buffer_.size() < expected_samples) {
        data_ready_.store(false, std::memory_order_release);
    }
    
    return AudioFrame(frame_buffer_.data(), to_read);
}

std::optional<AudioFrame> AudioDevice::try_get_data() noexcept {
    if (!data_ready_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    
    std::size_t expected_samples = config_.buffer_frames * config_.channels;
    std::size_t available = ring_buffer_.size();
    
    if (available < expected_samples / 2) {
        return std::nullopt;
    }
    
    std::size_t to_read = std::min(available, expected_samples);
    ring_buffer_.pop(std::span<float>(frame_buffer_.data(), to_read));
    
    if (ring_buffer_.size() < expected_samples) {
        data_ready_.store(false, std::memory_order_release);
    }
    
    return AudioFrame(frame_buffer_.data(), to_read);
}

std::vector<DeviceInfo> AudioDevice::enumerate_devices() {
    std::vector<DeviceInfo> devices;
    
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        return devices;
    }
    
    ma_device_info* capture_devices;
    ma_uint32 capture_count;
    
    if (ma_context_get_devices(&context, nullptr, nullptr, 
                                &capture_devices, &capture_count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < capture_count; ++i) {
            devices.push_back({
                .id = std::to_string(i),
                .name = capture_devices[i].name,
                .is_default = capture_devices[i].isDefault != 0,
                .max_channels = 2,
                .min_sample_rate = 8000,
                .max_sample_rate = 192000
            });
        }
    }
    
    ma_context_uninit(&context);
    return devices;
}

// ============================================================================
// C++23 Generator
// ============================================================================

harness::generator<AudioFrame> create_audio_stream(AudioDevice& device) {
    while (device.is_active()) {
        auto result = device.wait_for_data();
        if (result) {
            co_yield *result;
        } else {
            std::print(stderr, "Audio read error: {}\n", result.error());
        }
    }
}

// ============================================================================
// Audio Level Calculation
// ============================================================================

[[nodiscard]] inline float calculate_db_level(AudioFrame frame) noexcept {
    if (frame.empty()) return -100.0f;
    
    float sum_squares = 0.0f;
    for (float sample : frame) {
        sum_squares += sample * sample;
    }
    
    float rms = std::sqrt(sum_squares / static_cast<float>(frame.size()));
    if (rms < 1e-10f) return -100.0f;
    return 20.0f * std::log10(rms);
}

[[nodiscard]] inline bool detect_voice_activity(AudioFrame frame, 
                                                 float threshold_db = -40.0f) noexcept {
    return calculate_db_level(frame) > threshold_db;
}

} // namespace harness::audio
