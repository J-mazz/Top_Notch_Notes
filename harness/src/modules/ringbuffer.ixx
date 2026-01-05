// ============================================================================
// TopNotchNotes Harness - Ring Buffer Module
// Lock-free SPSC ring buffer for audio data transfer
// ============================================================================

module;

#include <cstddef>
#include <array>
#include <atomic>
#include <optional>
#include <span>
#include <ranges>
#include <algorithm>
#include <type_traits>

export module harness:ringbuffer;

export namespace harness
{

    // ============================================================================
    // Lock-Free Single-Producer Single-Consumer Ring Buffer
    // ============================================================================

    /// Thread-safe ring buffer optimized for audio callback -> main thread transfer
    /// Uses atomic indices for lock-free operation
    template <typename T, std::size_t Capacity>
        requires(std::is_trivially_copyable_v<T> && (Capacity & (Capacity - 1)) == 0)
    class RingBuffer
    {
    public:
        static constexpr std::size_t capacity = Capacity;
        static constexpr std::size_t mask = Capacity - 1;

        RingBuffer() = default;

        // Non-copyable, non-movable (due to atomics)
        RingBuffer(const RingBuffer &) = delete;
        RingBuffer &operator=(const RingBuffer &) = delete;
        RingBuffer(RingBuffer &&) = delete;
        RingBuffer &operator=(RingBuffer &&) = delete;

        /// Push a single element (producer thread)
        /// Returns false if buffer is full
        [[nodiscard]] bool push(const T &value) noexcept
        {
            const auto write = write_index_.load(std::memory_order_relaxed);
            const auto next_write = (write + 1) & mask;

            if (next_write == read_index_.load(std::memory_order_acquire))
            {
                return false; // Buffer full
            }

            buffer_[write] = value;
            write_index_.store(next_write, std::memory_order_release);
            return true;
        }

        /// Push multiple elements (producer thread)
        /// Returns number of elements actually pushed
        [[nodiscard]] std::size_t push(std::span<const T> data) noexcept
        {
            std::size_t pushed = 0;
            for (const auto &value : data)
            {
                if (!push(value))
                    break;
                ++pushed;
            }
            return pushed;
        }

        /// Pop a single element (consumer thread)
        /// Returns nullopt if buffer is empty
        [[nodiscard]] std::optional<T> pop() noexcept
        {
            const auto read = read_index_.load(std::memory_order_relaxed);

            if (read == write_index_.load(std::memory_order_acquire))
            {
                return std::nullopt; // Buffer empty
            }

            T value = buffer_[read];
            read_index_.store((read + 1) & mask, std::memory_order_release);
            return value;
        }

        /// Pop multiple elements into a span (consumer thread)
        /// Returns number of elements actually popped
        [[nodiscard]] std::size_t pop(std::span<T> out) noexcept
        {
            std::size_t popped = 0;
            for (auto &slot : out)
            {
                if (auto value = pop())
                {
                    slot = *value;
                    ++popped;
                }
                else
                {
                    break;
                }
            }
            return popped;
        }

        /// Check if buffer is empty
        [[nodiscard]] bool empty() const noexcept
        {
            return read_index_.load(std::memory_order_acquire) ==
                   write_index_.load(std::memory_order_acquire);
        }

        /// Check if buffer is full
        [[nodiscard]] bool full() const noexcept
        {
            const auto write = write_index_.load(std::memory_order_acquire);
            const auto read = read_index_.load(std::memory_order_acquire);
            return ((write + 1) & mask) == read;
        }

        /// Get number of elements available for reading
        [[nodiscard]] std::size_t size() const noexcept
        {
            const auto write = write_index_.load(std::memory_order_acquire);
            const auto read = read_index_.load(std::memory_order_acquire);
            return (write - read + Capacity) & mask;
        }

        /// Get number of free slots for writing
        [[nodiscard]] std::size_t available() const noexcept
        {
            return Capacity - size() - 1;
        }

        /// Clear the buffer (must be called from consumer thread only)
        void clear() noexcept
        {
            read_index_.store(write_index_.load(std::memory_order_acquire),
                              std::memory_order_release);
        }

    private:
        alignas(64) std::array<T, Capacity> buffer_{};
        alignas(64) std::atomic<std::size_t> write_index_{0};
        alignas(64) std::atomic<std::size_t> read_index_{0};
    };

    // ============================================================================
    // Audio Ring Buffer Specialization
    // ============================================================================

    /// Pre-configured ring buffer for audio samples
    /// 64KB of float samples = ~16384 samples = ~341ms at 48kHz mono
    using AudioRingBuffer = RingBuffer<float, 16384>;

    /// Ring buffer for complete audio frames (for frame-based processing)
    template <std::size_t FrameSize>
    struct AudioFrameBuffer
    {
        RingBuffer<std::array<float, FrameSize>, 64> frames;

        [[nodiscard]] bool push_frame(std::span<const float> data) noexcept
        {
            if (data.size() != FrameSize)
                return false;
            std::array<float, FrameSize> frame;
            std::ranges::copy(data, frame.begin());
            return frames.push(frame);
        }

        [[nodiscard]] std::optional<std::array<float, FrameSize>> pop_frame() noexcept
        {
            return frames.pop();
        }
    };

} // namespace harness
