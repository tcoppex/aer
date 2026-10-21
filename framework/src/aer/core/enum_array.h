#ifndef AER_CORE_ENUM_ARRAY_H_
#define AER_CORE_ENUM_ARRAY_H_

/* -------------------------------------------------------------------------- */

#include <cassert>
#include <cstddef>

#include <array>
#include <algorithm>
#include <initializer_list>

/* -------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_ENUM_ARRAY_H_