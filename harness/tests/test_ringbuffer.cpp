// ============================================================================
// TopNotchNotes Harness - Ring Buffer Tests
// ============================================================================

#include <array>
#include <cstddef>
#include <print>
#include <span>

import harness;

namespace
{

    bool test_basic_operations()
    {
        harness::RingBuffer<int, 8> buffer;

        // Test empty buffer
        if (!buffer.empty())
            return false;
        if (buffer.size() != 0)
            return false;

        // Test push
        if (!buffer.push(1))
            return false;
        if (!buffer.push(2))
            return false;
        if (!buffer.push(3))
            return false;

        if (buffer.size() != 3)
            return false;
        if (buffer.empty())
            return false;

        // Test pop
        auto val = buffer.pop();
        if (!val || *val != 1)
            return false;

        val = buffer.pop();
        if (!val || *val != 2)
            return false;

        if (buffer.size() != 1)
            return false;

        return true;
    }

    bool test_full_buffer()
    {
        harness::RingBuffer<int, 4> buffer; // Capacity 4, usable 3

        if (!buffer.push(1))
            return false;
        if (!buffer.push(2))
            return false;
        if (!buffer.push(3))
            return false;

        // Buffer should be full now
        if (!buffer.full())
            return false;
        if (buffer.push(4))
            return false; // Should fail

        // Pop one and push should work
        (void)buffer.pop();
        if (!buffer.push(4))
            return false;

        return true;
    }

    bool test_wraparound()
    {
        harness::RingBuffer<int, 4> buffer;

        // Fill and drain multiple times to test wraparound
        for (int round = 0; round < 10; ++round)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (!buffer.push(round * 10 + i))
                    return false;
            }
            for (int i = 0; i < 3; ++i)
            {
                auto val = buffer.pop();
                if (!val || *val != round * 10 + i)
                    return false;
            }
        }

        return buffer.empty();
    }

    bool test_span_operations()
    {
        harness::RingBuffer<float, 16> buffer;

        std::array<float, 5> input = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        auto pushed = buffer.push(std::span(input));
        if (pushed != 5)
            return false;

        std::array<float, 5> output;
        auto popped = buffer.pop(std::span(output));
        if (popped != 5)
            return false;

        for (std::size_t i = 0; i < 5; ++i)
        {
            if (input[i] != output[i])
                return false;
        }

        return true;
    }

} // anonymous namespace

int run_ringbuffer_tests()
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

    run("basic_operations", test_basic_operations);
    run("full_buffer", test_full_buffer);
    run("wraparound", test_wraparound);
    run("span_operations", test_span_operations);

    std::print("\nRingBuffer Tests: {} passed, {} failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
