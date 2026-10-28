// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef __ATOMIC64_H__
#define __ATOMIC64_H__

#include <atomic>
#include <bit>
#include <cstdint>
#include <type_traits>

namespace nexus
{
#if defined(__i386__)
// 32-bit x86 compilers do lock-free 64-bit atomic loads and stores with plain
// SSE (movq) or x87 moves and rely on x86 ordering for acquire and release.
// FEX (WoW64 on ARM64) orders those vector accesses only with VectorTSOEnabled,
// so without it the task queue loses updates and crashes. Every access here is
// a lock cmpxchg8b, which FEX always keeps atomic and fully ordered.
template<typename T>
class Atomic64
{
    static_assert(sizeof(T) == sizeof(uint64_t) && std::is_trivially_copyable_v<T>, "Atomic64 holds 64-bit values");

public:
    Atomic64() noexcept = default;
    constexpr Atomic64(T value) noexcept : m_value(std::bit_cast<uint64_t>(value)) {}
    Atomic64(const Atomic64&) = delete;
    Atomic64& operator=(const Atomic64&) = delete;

    // Locked instructions are full barriers, so every order is seq_cst.
    T load(std::memory_order = std::memory_order_seq_cst) const noexcept
    {
        uint64_t value = 0;
        __atomic_compare_exchange_n(&m_value, &value, 0, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        return std::bit_cast<T>(value);
    }

    void store(T value, std::memory_order = std::memory_order_seq_cst) noexcept
    {
        exchange(value);
    }

    T exchange(T value, std::memory_order = std::memory_order_seq_cst) noexcept
    {
        uint64_t current = 0;
        while (!__atomic_compare_exchange_n(&m_value, &current, std::bit_cast<uint64_t>(value), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
        }
        return std::bit_cast<T>(current);
    }

    bool compare_exchange_strong(T& expected, T desired, std::memory_order = std::memory_order_seq_cst, std::memory_order = std::memory_order_seq_cst) noexcept
    {
        uint64_t current = std::bit_cast<uint64_t>(expected);
        if (__atomic_compare_exchange_n(&m_value, &current, std::bit_cast<uint64_t>(desired), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
            return true;
        }
        expected = std::bit_cast<T>(current);
        return false;
    }

    bool compare_exchange_weak(T& expected, T desired, std::memory_order success = std::memory_order_seq_cst, std::memory_order failure = std::memory_order_seq_cst) noexcept
    {
        return compare_exchange_strong(expected, desired, success, failure);
    }

    T fetch_add(T value, std::memory_order = std::memory_order_seq_cst) noexcept requires std::is_integral_v<T>
    {
        uint64_t current = 0;
        while (!__atomic_compare_exchange_n(&m_value, &current, current + value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
        }
        return static_cast<T>(current);
    }

    T operator++() noexcept requires std::is_integral_v<T> { return fetch_add(1) + 1; }
    T operator++(int) noexcept requires std::is_integral_v<T> { return fetch_add(1); }

    operator T() const noexcept { return load(); }
    T operator=(T value) noexcept
    {
        store(value);
        return value;
    }

private:
    // load() writes the value back unchanged, so it needs a mutable word.
    alignas(8) mutable uint64_t m_value = 0;
};

// Proton reads this from the i386 module to skip VectorTSOEnabled for it.
[[gnu::used]] inline constexpr char fexSafeAtomicsMarker[] = "nexus-fex-safe-i386-atomics-v1";
#else
// Other targets keep std::atomic and its codegen unchanged.
template<typename T>
using Atomic64 = std::atomic<T>;
#endif
}

#endif
