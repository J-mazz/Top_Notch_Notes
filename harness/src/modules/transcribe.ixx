// ============================================================================
// TopNotchNotes Harness - Transcription Module
// PocketSphinx-based speech-to-text processing
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
#include <algorithm>

// PocketSphinx headers
#include <pocketsphinx.h>
#include <sphinxbase/cmd_ln.h>

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
    // PocketSphinx Transcription Engine
    // ============================================================================

    class PocketSphinxEngine : public ITranscribeEngine
    {
    public:
        static std::expected<std::unique_ptr<PocketSphinxEngine>, std::string>
        create(const TranscribeConfig &config);

        ~PocketSphinxEngine() override;

        [[nodiscard]] std::optional<TranscriptSegment> process(AudioFrame frame) override;
        [[nodiscard]] std::optional<TranscriptSegment> finalize() override;
        void reset() override;
        [[nodiscard]] bool is_ready() const noexcept override;

    private:
        explicit PocketSphinxEngine(ps_decoder_t* decoder, std::uint32_t sample_rate);
        
        ps_decoder_t* decoder_ = nullptr;
        std::uint32_t sample_rate_ = 16000;
        std::vector<std::int16_t> resample_buffer_;
        std::size_t frame_count_ = 0;
        bool utterance_started_ = false;
    };

    PocketSphinxEngine::PocketSphinxEngine(ps_decoder_t* decoder, std::uint32_t sample_rate)
        : decoder_(decoder)
        , sample_rate_(sample_rate)
    {
    }

    PocketSphinxEngine::~PocketSphinxEngine() {
        if (decoder_) {
            ps_free(decoder_);
        }
    }

    std::expected<std::unique_ptr<PocketSphinxEngine>, std::string>
    PocketSphinxEngine::create(const TranscribeConfig &config) {
        // Default model paths
        const char* hmm_path = "/usr/share/pocketsphinx/model/en-us/en-us";
        const char* lm_path = "/usr/share/pocketsphinx/model/en-us/en-us.lm.bin";
        const char* dict_path = "/usr/share/pocketsphinx/model/en-us/cmudict-en-us.dict";
        
        // Override with custom paths if provided
        std::string custom_hmm, custom_dict;
        if (!config.model_path.empty()) {
            custom_hmm = config.model_path.string();
            hmm_path = custom_hmm.c_str();
        }
        if (!config.dictionary_path.empty()) {
            custom_dict = config.dictionary_path.string();
            dict_path = custom_dict.c_str();
        }

        // Create command-line configuration using SphinxBase
        cmd_ln_t* ps_cfg = cmd_ln_init(nullptr, ps_args(), TRUE,
            "-hmm", hmm_path,
            "-lm", lm_path,
            "-dict", dict_path,
            "-logfn", "/dev/null",  // Suppress verbose logging
            nullptr);
        
        if (!ps_cfg) {
            return std::unexpected("Failed to create PocketSphinx config");
        }

        // Create decoder
        ps_decoder_t* decoder = ps_init(ps_cfg);
        
        if (!decoder) {
            cmd_ln_free_r(ps_cfg);
            return std::unexpected("Failed to initialize PocketSphinx decoder - check model paths");
        }

        auto engine = std::unique_ptr<PocketSphinxEngine>(
            new PocketSphinxEngine(decoder, config.sample_rate)
        );
        
        std::print("PocketSphinx engine initialized (sample_rate={})\n", config.sample_rate);
        return engine;
    }

    std::optional<TranscriptSegment> PocketSphinxEngine::process(AudioFrame frame) {
        if (!decoder_ || frame.empty()) {
            return std::nullopt;
        }

        // Start utterance if not already started
        if (!utterance_started_) {
            if (ps_start_utt(decoder_) < 0) {
                std::print(stderr, "Failed to start utterance\n");
                return std::nullopt;
            }
            utterance_started_ = true;
        }

        // Convert float [-1,1] to int16
        resample_buffer_.resize(frame.size());
        std::transform(frame.begin(), frame.end(), resample_buffer_.begin(),
            [](float s) -> std::int16_t {
                return static_cast<std::int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
            });

        // Process audio
        if (ps_process_raw(decoder_, resample_buffer_.data(), 
                           resample_buffer_.size(), FALSE, FALSE) < 0) {
            std::print(stderr, "Failed to process audio\n");
            return std::nullopt;
        }

        ++frame_count_;

        // Check for hypothesis periodically (every ~0.5s at typical frame rates)
        if (frame_count_ % 25 == 0) {
            const char* hyp = ps_get_hyp(decoder_, nullptr);
            if (hyp && hyp[0] != '\0') {
                auto now = std::chrono::milliseconds(frame_count_ * 20);
                return TranscriptSegment{
                    .words = {{
                        .text = hyp,
                        .start_time = std::chrono::milliseconds(0),
                        .end_time = now,
                        .confidence = 0.8f
                    }},
                    .start_time = std::chrono::milliseconds(0),
                    .end_time = now
                };
            }
        }

        return std::nullopt;
    }

    std::optional<TranscriptSegment> PocketSphinxEngine::finalize() {
        if (!decoder_ || !utterance_started_) {
            return std::nullopt;
        }

        ps_end_utt(decoder_);
        utterance_started_ = false;

        const char* hyp = ps_get_hyp(decoder_, nullptr);
        if (hyp && hyp[0] != '\0') {
            auto end_time = std::chrono::milliseconds(frame_count_ * 20);
            return TranscriptSegment{
                .words = {{
                    .text = hyp,
                    .start_time = std::chrono::milliseconds(0),
                    .end_time = end_time,
                    .confidence = 0.9f
                }},
                .start_time = std::chrono::milliseconds(0),
                .end_time = end_time
            };
        }

        return std::nullopt;
    }

    void PocketSphinxEngine::reset() {
        if (decoder_ && utterance_started_) {
            ps_end_utt(decoder_);
            utterance_started_ = false;
        }
        frame_count_ = 0;
    }

    bool PocketSphinxEngine::is_ready() const noexcept {
        return decoder_ != nullptr;
    }

    // ============================================================================
    // Factory Function
    // ============================================================================

    /// Create a transcription engine based on configuration
    [[nodiscard]] inline std::unique_ptr<ITranscribeEngine>
    create_engine(const TranscribeConfig &config)
    {
        // Try PocketSphinx first
        auto result = PocketSphinxEngine::create(config);
        if (result) {
            return std::move(*result);
        }
        std::print(stderr, "PocketSphinx failed: {} - falling back to stub\n", result.error());

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
