// ============================================================================
// TopNotchNotes Harness - Async I/O Module
// Non-blocking file writing for audio data persistence
// ============================================================================

module;

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <queue>
#include <fstream>
#include <filesystem>
#include <expected>
#include <span>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <cstring>

export module harness:io;

export namespace harness::io
{

    // ============================================================================
    // Type Aliases
    // ============================================================================

    template <typename T>
    using IOResult = std::expected<T, std::string>;

    // ============================================================================
    // Async Writer - Non-blocking disk I/O
    // ============================================================================

    /// Asynchronous file writer that doesn't block the audio thread
    /// Uses a background thread with a queue for write operations
    class AsyncWriter
    {
    public:
        explicit AsyncWriter(const std::filesystem::path &path, std::size_t buffer_size = 65536);
        ~AsyncWriter();

        // Non-copyable, non-movable
        AsyncWriter(const AsyncWriter &) = delete;
        AsyncWriter &operator=(const AsyncWriter &) = delete;
        AsyncWriter(AsyncWriter &&) = delete;
        AsyncWriter &operator=(AsyncWriter &&) = delete;

        /// Queue data for writing (non-blocking)
        /// Returns false if the write queue is full
        [[nodiscard]] bool write(std::span<const float> data);

        /// Queue data for writing (non-blocking) - raw bytes
        [[nodiscard]] bool write_bytes(std::span<const std::byte> data);

        /// Flush all pending writes and close the file
        void close();

        /// Check if writer is active
        [[nodiscard]] bool is_open() const noexcept { return is_open_.load(); }

        /// Get bytes written so far
        [[nodiscard]] std::size_t bytes_written() const noexcept
        {
            return bytes_written_.load();
        }

        /// Check if there are pending writes
        [[nodiscard]] bool has_pending() const noexcept;

    private:
        void writer_thread_func();

        std::filesystem::path path_;
        std::ofstream file_;

        std::vector<std::byte> buffer_;
        std::mutex mutex_;
        std::condition_variable cv_;

        std::jthread writer_thread_;
        std::atomic<bool> is_open_{false};
        std::atomic<bool> should_stop_{false};
        std::atomic<std::size_t> bytes_written_{0};

        // Write queue
        std::queue<std::vector<std::byte>> write_queue_;
        static constexpr std::size_t max_queue_size = 100;
    };

    // ============================================================================
    // Implementation
    // ============================================================================

    AsyncWriter::AsyncWriter(const std::filesystem::path &path, std::size_t buffer_size)
        : path_(path), buffer_(buffer_size)
    {
        // Ensure directory exists
        if (auto parent = path_.parent_path(); !parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        file_.open(path_, std::ios::binary | std::ios::trunc);
        if (!file_.is_open())
        {
            throw std::runtime_error("Failed to open file: " + path_.string());
        }

        is_open_ = true;
        writer_thread_ = std::jthread([this]
                                      { writer_thread_func(); });
    }

    AsyncWriter::~AsyncWriter()
    {
        close();
    }

    bool AsyncWriter::write(std::span<const float> data)
    {
        // Convert floats to bytes
        auto byte_span = std::as_bytes(data);
        return write_bytes(std::span<const std::byte>(byte_span.data(), byte_span.size()));
    }

    bool AsyncWriter::write_bytes(std::span<const std::byte> data)
    {
        if (!is_open_)
            return false;

        std::lock_guard lock(mutex_);

        if (write_queue_.size() >= max_queue_size)
        {
            return false; // Queue full, drop data
        }

        write_queue_.emplace(data.begin(), data.end());
        cv_.notify_one();
        return true;
    }

    void AsyncWriter::close()
    {
        if (!is_open_.exchange(false))
            return;

        should_stop_ = true;
        cv_.notify_all();

        if (writer_thread_.joinable())
        {
            writer_thread_.join();
        }

        file_.close();
    }

    bool AsyncWriter::has_pending() const noexcept
    {
        return !write_queue_.empty();
    }

    void AsyncWriter::writer_thread_func()
    {
        while (!should_stop_ || !write_queue_.empty())
        {
            std::vector<std::byte> data;

            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this]
                         { return !write_queue_.empty() || should_stop_; });

                if (write_queue_.empty())
                    continue;

                data = std::move(write_queue_.front());
                write_queue_.pop();
            }

            // Write outside the lock
            file_.write(reinterpret_cast<const char *>(data.data()),
                        static_cast<std::streamsize>(data.size()));
            bytes_written_ += data.size();
        }

        file_.flush();
    }

    // ============================================================================
    // WAV File Writer
    // ============================================================================

    /// WAV file header structure
    struct WavHeader
    {
        // RIFF chunk
        char riff[4] = {'R', 'I', 'F', 'F'};
        std::uint32_t file_size = 0; // File size - 8
        char wave[4] = {'W', 'A', 'V', 'E'};

        // fmt chunk
        char fmt[4] = {'f', 'm', 't', ' '};
        std::uint32_t fmt_size = 16;
        std::uint16_t audio_format = 3; // IEEE float
        std::uint16_t num_channels = 1;
        std::uint32_t sample_rate = 48000;
        std::uint32_t byte_rate = 0;   // sample_rate * num_channels * bytes_per_sample
        std::uint16_t block_align = 0; // num_channels * bytes_per_sample
        std::uint16_t bits_per_sample = 32;

        // data chunk
        char data[4] = {'d', 'a', 't', 'a'};
        std::uint32_t data_size = 0;

        void configure(std::uint32_t rate, std::uint16_t channels)
        {
            sample_rate = rate;
            num_channels = channels;
            std::uint16_t bytes_per_sample = bits_per_sample / 8;
            block_align = static_cast<std::uint16_t>(num_channels * bytes_per_sample);
            byte_rate = sample_rate * block_align;
        }

        void finalize(std::size_t data_bytes)
        {
            data_size = static_cast<std::uint32_t>(data_bytes);
            file_size = 36 + data_size;
        }
    };

    static_assert(sizeof(WavHeader) == 44, "WavHeader must be 44 bytes");

    /// WAV file writer with proper header management
    class WavWriter
    {
    public:
        static IOResult<WavWriter> create(const std::filesystem::path &path,
                                          std::uint32_t sample_rate,
                                          std::uint16_t channels = 1);

        ~WavWriter();

        WavWriter(WavWriter &&other) noexcept;
        WavWriter &operator=(WavWriter &&other) noexcept;
        WavWriter(const WavWriter &) = delete;
        WavWriter &operator=(const WavWriter &) = delete;

        /// Write audio samples
        bool write(std::span<const float> samples);

        /// Finalize the file (updates header)
        void close();

        [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }
        [[nodiscard]] std::size_t samples_written() const noexcept { return samples_written_; }

    private:
        WavWriter(const std::filesystem::path &path, std::uint32_t sample_rate, std::uint16_t channels);

        std::filesystem::path path_;
        std::ofstream file_;
        WavHeader header_;
        std::size_t samples_written_ = 0;
    };

    IOResult<WavWriter> WavWriter::create(const std::filesystem::path &path,
                                          std::uint32_t sample_rate,
                                          std::uint16_t channels)
    {
        try
        {
            return WavWriter(path, sample_rate, channels);
        }
        catch (const std::exception &e)
        {
            return std::unexpected(e.what());
        }
    }

    WavWriter::WavWriter(const std::filesystem::path &path, std::uint32_t sample_rate, std::uint16_t channels)
        : path_(path)
    {
        if (auto parent = path_.parent_path(); !parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        file_.open(path_, std::ios::binary | std::ios::trunc);
        if (!file_.is_open())
        {
            throw std::runtime_error("Failed to open file: " + path_.string());
        }

        header_.configure(sample_rate, channels);

        // Write placeholder header
        file_.write(reinterpret_cast<const char *>(&header_), sizeof(header_));
    }

    WavWriter::~WavWriter()
    {
        if (file_.is_open())
        {
            close();
        }
    }

    WavWriter::WavWriter(WavWriter &&other) noexcept
        : path_(std::move(other.path_)), file_(std::move(other.file_)), header_(other.header_), samples_written_(other.samples_written_)
    {
    }

    WavWriter &WavWriter::operator=(WavWriter &&other) noexcept
    {
        if (this != &other)
        {
            if (file_.is_open())
                close();
            path_ = std::move(other.path_);
            file_ = std::move(other.file_);
            header_ = other.header_;
            samples_written_ = other.samples_written_;
        }
        return *this;
    }

    bool WavWriter::write(std::span<const float> samples)
    {
        if (!file_.is_open())
            return false;

        file_.write(reinterpret_cast<const char *>(samples.data()),
                    static_cast<std::streamsize>(samples.size_bytes()));
        samples_written_ += samples.size();
        return true;
    }

    void WavWriter::close()
    {
        if (!file_.is_open())
            return;

        // Update header with final sizes
        header_.finalize(samples_written_ * sizeof(float));

        // Seek back and write final header
        file_.seekp(0);
        file_.write(reinterpret_cast<const char *>(&header_), sizeof(header_));
        file_.close();
    }

} // namespace harness::io
