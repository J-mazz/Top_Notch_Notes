// ============================================================================
// TopNotchNotes Harness - Generator Polyfill
// Custom std::generator implementation for C++23 coroutines
// ============================================================================

#ifndef TOPNOTCHNOTES_GENERATOR_HPP
#define TOPNOTCHNOTES_GENERATOR_HPP

#include <coroutine>
#include <exception>
#include <utility>
#include <type_traits>
#include <iterator>
#include <memory>

namespace harness
{

    /// Simple generator coroutine (polyfill for std::generator)
    template <typename T>
    class generator
    {
    public:
        struct promise_type
        {
            T current_value;
            std::exception_ptr exception;

            generator get_return_object()
            {
                return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            std::suspend_always yield_value(T value) noexcept
            {
                current_value = std::move(value);
                return {};
            }

            void return_void() noexcept {}

            void unhandled_exception()
            {
                exception = std::current_exception();
            }

            template <typename U>
            std::suspend_never await_transform(U &&) = delete;
        };

        using handle_type = std::coroutine_handle<promise_type>;

        class iterator
        {
        public:
            using iterator_category = std::input_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using reference = T &;
            using pointer = T *;

            iterator() noexcept : handle_(nullptr) {}
            explicit iterator(handle_type handle) noexcept : handle_(handle) {}

            iterator &operator++()
            {
                handle_.resume();
                if (handle_.done())
                {
                    if (handle_.promise().exception)
                    {
                        std::rethrow_exception(handle_.promise().exception);
                    }
                }
                return *this;
            }

            iterator operator++(int)
            {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            reference operator*() const
            {
                return handle_.promise().current_value;
            }

            pointer operator->() const
            {
                return std::addressof(handle_.promise().current_value);
            }

            bool operator==(const iterator &other) const noexcept
            {
                // Both are end iterators, or both point to same (done) handle
                if (!handle_ && !other.handle_)
                    return true;
                if (!handle_)
                    return other.handle_.done();
                if (!other.handle_)
                    return handle_.done();
                return handle_.done() && other.handle_.done();
            }

            bool operator!=(const iterator &other) const noexcept
            {
                return !(*this == other);
            }

        private:
            handle_type handle_;
        };

        generator() noexcept : handle_(nullptr) {}

        explicit generator(handle_type handle) noexcept : handle_(handle) {}

        generator(generator &&other) noexcept : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        generator &operator=(generator &&other) noexcept
        {
            if (this != &other)
            {
                if (handle_)
                    handle_.destroy();
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        generator(const generator &) = delete;
        generator &operator=(const generator &) = delete;

        ~generator()
        {
            if (handle_)
                handle_.destroy();
        }

        iterator begin()
        {
            if (handle_)
            {
                handle_.resume();
                if (handle_.done())
                {
                    if (handle_.promise().exception)
                    {
                        std::rethrow_exception(handle_.promise().exception);
                    }
                    return end();
                }
            }
            return iterator{handle_};
        }

        iterator end() noexcept
        {
            return iterator{};
        }

        explicit operator bool() const noexcept
        {
            return handle_ && !handle_.done();
        }

    private:
        handle_type handle_;
    };

} // namespace harness

#endif // TOPNOTCHNOTES_GENERATOR_HPP
