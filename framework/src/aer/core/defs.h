#ifndef AER_CORE_DEFS_H_
#define AER_CORE_DEFS_H_

/* -------------------------------------------------------------------------- */

#include <cassert>
#include <cstddef>

#include <concepts>
#include <limits>
#include <span>
#include <type_traits>

/* -------------------------------------------------------------------------- */
/* -- Maths (with aliases) -- */

#include "lina/lina.h"
using namespace lina::aliases;

/* -------------------------------------------------------------------------- */
/* -- c++ features -- */

template<typename T, typename U>
concept DerivedFrom = std::is_base_of_v<U, T>;

template<typename T>
concept SpanConvertible = requires(T t) { std::span(t); };

template<typename T>
concept NonSpanConvertible = !SpanConvertible<T>;

// ----------------------------------------------------------------------------
/* -- custom type -- */

// Wrap an array to accept enum class as indexer.
// Original code from Daniel P. Wright.
template<typename T, typename Indexer>
class EnumArray : public std::array<T, static_cast<size_t>(Indexer::kCount)> {
  using super = std::array<T, static_cast<size_t>(Indexer::kCount)>;

 public:
  constexpr EnumArray(std::initializer_list<T> il) {
    assert( il.size() == super::size());
    std::copy(il.begin(), il.end(), super::begin());
  }

  T&       operator[](Indexer i)       { return super::at((size_t)i); }
  const T& operator[](Indexer i) const { return super::at((size_t)i); }

  EnumArray() : super() {}
  using super::operator[];
};

struct NullType {};

// ----------------------------------------------------------------------------
/* -- constants -- */

static constexpr uint32_t kInvalidIndexU32{ std::numeric_limits<uint32_t>::max() };

// ----------------------------------------------------------------------------
/* -- macros -- */

// Trivial POD check (trivially copyable + moveable)
#define STATIC_ASSERT_TRIVIALITY(T)                                 \
    static_assert(std::is_trivially_move_constructible_v<T>,        \
                  #T " must be trivially move constructible");      \
    static_assert(std::is_trivially_move_assignable_v<T>,           \
                  #T " must be trivially move assignable");         \
    static_assert(std::is_trivially_copyable_v<T>,                  \
                  #T " must be trivially copyable")

// RAII resource check (non-copyable, movable)
#define STATIC_ASSERT_MOVABLE_ONLY(T)                               \
    static_assert(!std::is_copy_constructible_v<T>,                 \
                  #T " must be non-copyable");                      \
    static_assert(std::is_move_constructible_v<T>,                  \
                  #T " must be move constructible");                \
    static_assert(std::is_move_assignable_v<T>,                     \
                  #T " must be move assignable")

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_DEFS_H_
