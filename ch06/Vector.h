// Ch. 6 -- Vector<T>
//
// STARTER FILE. The interface is complete and every function compiles; the
// interesting ones are stubs marked TODO. Stubs that are allowed to throw
// call todo(), so an enabled test fails with "TODO: <function>" instead of
// crashing. Stubs that are noexcept (destructor, moves) cannot throw, so they
// are empty and the tests that need them fail on their assertions instead.
//
// Rules for this exercise -- the point is to write these by hand:
//   * no std::vector, std::unique_ptr, or other owning containers inside;
//   * no std::uninitialized_copy / uninitialized_move / std::destroy and
//     friends. Write the loops. Compare against them afterwards.
//   * raw storage comes from ::operator new; objects are built in it with
//     placement new and torn down with explicit ~T() calls.
//
// Work through the stages in test.cpp in order. The main guide's Ch 6
// section has the four-step order for reallocation -- that order is the
// whole strong-guarantee lesson.

#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace tour {

// Placeholder for unimplemented functions. Delete once nothing calls it.
[[noreturn]] inline void todo(const char* what) {
    throw std::logic_error(std::string{"TODO: "} + what);
}

template <class T>
class Vector {
public:
    using value_type     = T;
    using size_type      = std::size_t;
    using iterator       = T*;
    using const_iterator = const T*;

    // ---- Construction and destruction: the rule of five ------------------

    // Done for you: the default member initializers below make this empty,
    // allocation-free, and noexcept.
    Vector() noexcept = default;

    // Stage 1. Builds a vector holding a copy of each element of `init`.
    // Note: initializer_list elements are const, so these are copies, never
    // moves -- the Stage 2 tests account for that.
    Vector(std::initializer_list<T> init) : Vector() {
        reserve(init.size());
        for (const T& x : init) {
            push_back(x);
        }
    }

    // Stage 1. Destroy every live element (size_, not capacity_), then
    // release the storage. Until this is written, LeakSanitizer fails every
    // test that allocates -- which is exactly why it is in Stage 1.
    ~Vector() {
        for (iterator i{data_}; i != data_ + size_; ++i) {
            i->~T();
        }
        ::operator delete(data_);
    }

    // Stage 2. A deep copy: new storage, every element copy-constructed.
    // If the k-th copy throws, what must happen to the k-1 already built?
    Vector(const Vector& other) : Vector() {
        reserve(other.size_);
        for (const T& x : other) {
            push_back(x);
        }
    }

    // Stage 2. Must survive `v = v`. Consider: is there a way to write this
    // in terms of the copy constructor so self-assignment and exception
    // safety come for free?
    Vector& operator=(const Vector& other) {
        if (this != &other) {
            Vector copy(other);
            std::swap(data_, copy.data_);
            std::swap(size_, copy.size_);
            std::swap(capacity_, copy.capacity_);
        }
        return *this;
    }

    // Stage 3. Steal other's buffer; leave other empty and destructible.
    // No element is copied or moved -- only three members change hands.
    // noexcept is not decoration: Stage 3's tests check what it buys you.
    Vector(Vector&& other) noexcept 
        : data_{other.data_}, size_{other.size_}, capacity_{other.capacity_} 
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // Stage 3. Release what *this owns, then steal from other.
    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            for (iterator i{data_}; i != data_ + size_; ++i) {
                i->~T();
            }
            ::operator delete(data_);

            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    // ---- Capacity ----------------------------------------------------------

    size_type size() const noexcept { return size_; }
    size_type capacity() const noexcept { return capacity_; }
    bool empty() const noexcept { return size_ == 0; }

    // Stage 1. Ensure capacity() >= new_capacity. Never shrinks. Never
    // changes size() or the values of the elements.
    void reserve(size_type new_capacity) {
        if (new_capacity > capacity_) {
            reallocate(new_capacity);
        }
    }

    // ---- Modifiers ---------------------------------------------------------

    // Stage 1, then hardened in Stage 4. When full, grow geometrically --
    // not by one, not by a constant. Stage 4 asks two hard questions of it:
    // what if T's copy throws halfway through growing, and what if `value`
    // is a reference to one of this vector's own elements?
    void push_back(const T& value) {
        if (size_ == capacity_) {
            reallocate(capacity_ == 0 ? 1 : capacity_ * 2, value);
        } else {
            ::new (static_cast<void*>(data_ + size_)) T(value);
        }
        ++size_;
    }

    // Stage 1. Same, but the new element is move-constructed from `value`.
    void push_back(T&& value) {
        if (size_ == capacity_) {
            reallocate(capacity_ == 0 ? 1 : capacity_ * 2, std::move(value));
        } else {
            ::new (static_cast<void*>(data_ + size_)) T(std::move(value));
        }
        ++size_;
    }

    // ---- Element access: done for you --------------------------------------

    T& operator[](size_type i) noexcept { return data_[i]; }
    const T& operator[](size_type i) const noexcept { return data_[i]; }

    iterator begin() noexcept { return data_; }
    iterator end() noexcept { return data_ + size_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator end() const noexcept { return data_ + size_; }

    // ---- Comparison --------------------------------------------------------
    //
    // Both are constrained so Vector<T> still compiles for a T that cannot be
    // compared -- the Counter and Thrower test types, for instance. The
    // return type of <=> is `auto` on purpose: spelling it as
    // std::compare_three_way_result_t<T> would be evaluated as soon as
    // Vector<T> is instantiated, and break every non-comparable T.

    // Stage 5. Same size and pairwise-equal elements.
    bool operator==(const Vector& rhs) const
        requires std::equality_comparable<T>
    {
        if (size_ != rhs.size_) {
            return false;
        }
        for (size_type i{0}; i < size_; ++i) {
            if (!(data_[i] == rhs.data_[i])) {
                return false;
            }
        }
        return true;
    }

    // Stage 5. Lexicographic, like std::vector: the first differing element
    // decides; if one is a prefix of the other, the shorter is less.
    auto operator<=>(const Vector& rhs) const
        requires std::three_way_comparable<T>
    {
        using Ordering = std::compare_three_way_result_t<T>;
        for (size_type i{0}; i < size_ && i < rhs.size_; ++i) {
            if (auto cmp = data_[i] <=> rhs.data_[i]; cmp != 0) {
                return cmp;
            }
        }
        return size_ == rhs.size_ ? Ordering::equivalent 
             : size_ < rhs.size_  ? Ordering::less 
                                  : Ordering::greater; 
    }

private:
    // ---- Suggested helpers (optional) -------------------------------------
    //
    // reserve() and both push_backs all need "move everything into a bigger
    // buffer". Writing that once, correctly, is most of the exercise:
    //
    template <class... Args>
    void reallocate(size_type new_capacity, Args&&... args) {
        T* new_data{static_cast<T*>(::operator new(new_capacity * sizeof(T)))};

        if constexpr (sizeof...(Args) > 0) {
            try {
                ::new (static_cast<void*>(new_data + size_)) T(std::forward<Args>(args)...);
            } catch (...) {
                ::operator delete(new_data);
                throw;
            }
        }

        size_type constructed{0};
        try {
            for (; constructed < size_; ++constructed) {
                ::new (static_cast<void*>(new_data + constructed)) T(std::move_if_noexcept(data_[constructed]));
            }
        } catch (...) {
            for (iterator j{new_data}; j != new_data + constructed; ++j) {
                j->~T();
            }
            if constexpr (sizeof...(Args) > 0) {
                new_data[size_].~T();
            }
            ::operator delete(new_data);
            throw;
        }
        for (iterator k{data_}; k != data_ + size_; ++k) {
            k->~T();
        }
        ::operator delete(data_);
        data_ = new_data;
        capacity_ = new_capacity;
    }
    //
    // Tools you will want inside it:
    //   static_cast<T*>(::operator new(n * sizeof(T)))   raw storage
    //   ::new (static_cast<void*>(p)) T(args...)        construct in place
    //   p->~T()                                          destroy in place
    //   ::operator delete(ptr)                           release storage
    //   std::move_if_noexcept(x)                         the Stage 3/4 key

    T*        data_{nullptr};
    size_type size_{0};
    size_type capacity_{0};
};

}  // namespace tour