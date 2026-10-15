#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace tour {

// A variable template: a constant parameterised by a type or value.
template <std::size_t N>
inline constexpr bool is_power_of_two = std::has_single_bit(N);

template <class T, std::size_t N>
    requires std::is_nothrow_move_constructible_v<T> && is_power_of_two<N>
class RingBuffer {
public:
    using value_type = T;
    using size_type  = std::size_t;

    RingBuffer() noexcept = default;

    // Fills the buffer from an array. Exists mainly for CTAD: given
    // `RingBuffer rb{std::array{1, 2, 3, 4}};` the compiler deduces both
    // template arguments, T = int and N = 4, from this constructor.
    explicit RingBuffer(const std::array<T, N>& init) {
        for (const T& x : init) {
            (void)push(x);
        }
    }

    ~RingBuffer() { clear(); }

    RingBuffer(const RingBuffer&)            = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // ---- Capacity ----------------------------------------------------------

    static constexpr size_type capacity() noexcept { return N; }
    size_type size() const noexcept { return head_ - tail_; }
    bool      empty() const noexcept { return head_ == tail_; }
    bool      full() const noexcept { return size() == N; }

    // ---- Single elements ---------------------------------------------------

    // Returns false, and leaves `value` untouched, when the buffer is full.
    [[nodiscard]] bool push(const T& value) { return emplace(value); }
    [[nodiscard]] bool push(T&& value) { return emplace(std::move(value)); }

    template <class... Args>
    [[nodiscard]] bool emplace(Args&&... args) {
        if (full()) {
            return false;
        }
        ::new (raw(index(head_))) T(std::forward<Args>(args)...);
        ++head_;  // only once construction has succeeded
        return true;
    }

    // Oldest element, or nothing if empty. Moving out is noexcept by the
    // class constraint, so pop() cannot fail half way.
    std::optional<T> pop() noexcept(std::is_nothrow_move_constructible_v<std::optional<T>>) {
        if (empty()) {
            return std::nullopt;
        }
        T* p{slot(tail_)};
        std::optional<T> out{std::move(*p)};
        p->~T();
        ++tail_;
        return out;
    }

    T&       front() noexcept { return *slot(tail_); }
    const T& front() const noexcept { return *slot(tail_); }

    void clear() noexcept {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (; tail_ != head_; ++tail_) {
                slot(tail_)->~T();
            }
        }
        head_ = tail_ = 0;
    }

    // ---- Bulk --------------------------------------------------------------

    // Copies as many of `src` as fit; returns how many were copied.
    size_type push_bulk(std::span<const T> src) {
        const size_type n{std::min(src.size(), N - size())};
        const size_type start{index(head_)};
        const size_type first{std::min(n, N - start)};  // up to the end of storage
        const size_type second{n - first};              // wrapped to the front

        if constexpr (std::is_trivially_copyable_v<T>) {
            // Raw bytes are a valid way to make trivially copyable objects:
            // two memcpys, one per contiguous run.
            std::memcpy(raw(start), src.data(), first * sizeof(T));
            std::memcpy(raw(0), src.data() + first, second * sizeof(T));
            head_ += n;
        } else {
            // Discarded entirely for trivially copyable T, and vice versa:
            // this branch is never even instantiated for int.
            for (size_type i{0}; i < n; ++i) {
                ::new (raw(index(head_))) T(src[i]);
                ++head_;
            }
        }
        return n;
    }

    // ---- Traversal ---------------------------------------------------------

    // Calls f on each element, oldest first. F&& binds to lvalues and rvalues
    // without copying, so a stateful callable passed by name keeps its state.
    // Not std::forward<F>(f) inside the loop: that hands f over as an rvalue
    // on every call, so a callable that consumes itself when called as an
    // rvalue (an &&-qualified operator()) would be reused after the first.
    template <class F>
    void for_each(F&& f) {
        for (size_type i{tail_}; i != head_; ++i) {
            std::invoke(f, *slot(i));
        }
    }

    template <class F>
    void for_each(F&& f) const {
        for (size_type i{tail_}; i != head_; ++i) {
            std::invoke(f, *slot(i));
        }
    }

private:
    // head_ and tail_ count up forever and are masked only on access, so
    // empty is head == tail and full is head - tail == N, with no wasted
    // slot. Unsigned wraparound after 2^64 pushes is still correct, because
    // N divides 2^64.
    static constexpr size_type kMask{N - 1};

    static constexpr size_type index(size_type counter) noexcept { return counter & kMask; }

    // Raw bytes for slot i, where an object is about to be created.
    void* raw(size_type i) noexcept { return storage_ + i * sizeof(T); }

    // A live object in slot i. std::launder is only valid on a live object,
    // which is why construction goes through raw() instead.
    T* slot_at(size_type i) noexcept {
        return std::launder(reinterpret_cast<T*>(storage_ + i * sizeof(T)));
    }
    const T* slot_at(size_type i) const noexcept {
        return std::launder(reinterpret_cast<const T*>(storage_ + i * sizeof(T)));
    }
    T*       slot(size_type counter) noexcept { return slot_at(index(counter)); }
    const T* slot(size_type counter) const noexcept { return slot_at(index(counter)); }

    // Raw storage, not std::array<T, N>: an array would default-construct N
    // elements up front, and require T to be default-constructible at all.
    alignas(T) std::byte storage_[N * sizeof(T)];
    size_type head_{0};  // next slot to write
    size_type tail_{0};  // oldest live element
};

// An alias template: names a family of types, not just one.
template <class T>
using Ring64 = RingBuffer<T, 64>;

}  // namespace tour