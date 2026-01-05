// ============================================================================
// TopNotchNotes Harness - Transcription Module
// Algorithmic speech-to-text processing
// ============================================================================

module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <expected>
#include <span>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <print>

export module harness:transcribe;

export namespace harness::transcribe
{

    // ============================================================================
    // Type Aliases
    // ============================================================================

    using AudioFrame = std::span<const float>;

    /// Timestamped transcription result
    struct TranscriptWord
    {
        std::string text;
        std::chrono::milliseconds start_time;
        std::chrono::milliseconds end_time;
        float confidence;
    };

    /// A segment of transcribed speech
    struct TranscriptSegment
    {
        std::vector<TranscriptWord> words;
        std::chrono::milliseconds start_time;
        std::chrono::milliseconds end_time;

        [[nodiscard]] std::string full_text() const
        {
            std::string result;
            for (const auto &word : words)
            {
                if (!result.empty())
                    result += ' ';
                result += word.text;
            }
            return result;
        }
    };

    // ============================================================================
    // Transcription Configuration
    // ============================================================================

    struct TranscribeConfig
    {
        std::filesystem::path model_path = "";
        std::filesystem::path dictionary_path = "";
        std::uint32_t sample_rate = 16000; // Most ASR models use 16kHz
        bool enable_punctuation = true;
        bool enable_diarization = false;
        float silence_threshold_db = -40.0f;
        std::chrono::milliseconds min_silence_duration{300};
    };

    // ============================================================================
    // Transcription Engine Interface
    // ============================================================================

    /// Abstract interface for transcription backends
    class ITranscribeEngine
    {
    public:
        virtual ~ITranscribeEngine() = default;

        /// Process an audio frame, may return partial results
        [[nodiscard]] virtual std::optional<TranscriptSegment> process(AudioFrame frame) = 0;

        /// Finalize and get any remaining transcription
        [[nodiscard]] virtual std::optional<TranscriptSegment> finalize() = 0;

        /// Reset the decoder state
        virtual void reset() = 0;

        /// Check if engine is ready
        [[nodiscard]] virtual bool is_ready() const noexcept = 0;
    };

    // ============================================================================
    // Voice Activity Detection (VAD)
    // ============================================================================

    /// Simple energy-based VAD
    class VoiceActivityDetector
    {
    public:
        explicit VoiceActivityDetector(float threshold_db = -40.0f,
                                       std::size_t hangover_frames = 10)
            : threshold_db_(threshold_db), hangover_frames_(hangover_frames)
        {
        }

        /// Returns true if voice activity is detected
        [[nodiscard]] bool process(AudioFrame frame)
        {
            float db = calculate_db(frame);

            if (db > threshold_db_)
            {
                hangover_counter_ = hangover_frames_;
                is_speech_ = true;
            }
            else if (hangover_counter_ > 0)
            {
                --hangover_counter_;
            }
            else
            {
                is_speech_ = false;
            }

            return is_speech_;
        }

        [[nodiscard]] bool is_speech() const noexcept { return is_speech_; }

        void reset() noexcept
        {
            is_speech_ = false;
            hangover_counter_ = 0;
        }

    private:
        [[nodiscard]] static float calculate_db(AudioFrame frame) noexcept
        {
            if (frame.empty())
                return -100.0f;

            float sum_squares = 0.0f;
            for (float sample : frame)
            {
                sum_squares += sample * sample;
            }

            float rms = std::sqrt(sum_squares / static_cast<float>(frame.size()));
            if (rms < 1e-10f)
                return -100.0f;
            return 20.0f * std::log10(rms);
        }

        float threshold_db_;
        std::size_t hangover_frames_;
        std::size_t hangover_counter_ = 0;
        bool is_speech_ = false;
    };

    // ============================================================================
    // Null/Stub Transcription Engine (for testing)
    // ============================================================================

    /// Stub engine that simulates transcription without actual ASR
    class StubTranscribeEngine : public ITranscribeEngine
    {
    public:
        StubTranscribeEngine() = default;

        [[nodiscard]] std::optional<TranscriptSegment> process(AudioFrame frame) override
        {
            ++frame_count_;

            // Simulate periodic output
            if (frame_count_ % 50 == 0)
            {
                auto now = std::chrono::milliseconds(frame_count_ * 20); // ~20ms per frame
                return TranscriptSegment{
                    .words = {{.text = "[audio detected]",
                               .start_time = now - std::chrono::milliseconds(500),
                               .end_time = now,
                               .confidence = 0.9f}},
                    .start_time = now - std::chrono::milliseconds(500),
                    .end_time = now};
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<TranscriptSegment> finalize() override
        {
            return std::nullopt;
        }

        void reset() override
        {
            frame_count_ = 0;
        }

        [[nodiscard]] bool is_ready() const noexcept override
        {
            return true;
        }

    private:
        std::size_t frame_count_ = 0;
    };

    // ============================================================================
    // PocketSphinx Integration (Optional)
    // ============================================================================

#ifdef HAS_POCKETSPHINX

    class PocketSphinxEngine : public ITranscribeEngine
    {
    public:
        static std::expected<std::unique_ptr<PocketSphinxEngine>, std::string>
        create(const TranscribeConfig &config);

        [[nodiscard]] std::optional<TranscriptSegment> process(AudioFrame frame) override;
        [[nodiscard]] std::optional<TranscriptSegment> finalize() override;
        void reset() override;
        [[nodiscard]] bool is_ready() const noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

#endif // HAS_POCKETSPHINX

    // ============================================================================
    // Factory Function
    // ============================================================================

    /// Create a transcription engine based on configuration
    [[nodiscard]] inline std::unique_ptr<ITranscribeEngine>
    create_engine(const TranscribeConfig &config)
    {
#ifdef HAS_POCKETSPHINX
        if (!config.model_path.empty())
        {
            auto result = PocketSphinxEngine::create(config);
            if (result)
            {
                return std::move(*result);
            }
            std::print(stderr, "Failed to create PocketSphinx engine: {}\n", result.error());
        }
#else
        (void)config;
#endif

        // Fallback to stub engine
        return std::make_unique<StubTranscribeEngine>();
    }

    // ============================================================================
    // Convenience Function
    // ============================================================================

    /// Pure function: Process audio frame and return text if detected
    /// This is the simple API used in the main loop
    [[nodiscard]] inline std::optional<std::string>
    transcribe(ITranscribeEngine &engine, AudioFrame frame)
    {
        if (auto segment = engine.process(frame))
        {
            auto text = segment->full_text();
            if (!text.empty())
            {
                return text;
            }
        }
        return std::nullopt;
    }

} // namespace harness::transcribe
