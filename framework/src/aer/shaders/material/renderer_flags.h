#ifndef SHADERS_MATERIAL_RENDERER_FLAGS_H_
#define SHADERS_MATERIAL_RENDERER_FLAGS_H_

/* -------------------------------------------------------------------------- */

#if defined(__SLANG__)
#define AER_SLANG_FLAGS [Flags]
#else
#define AER_SLANG_FLAGS
#endif

// ----------------------------------------------------------------------------

#if defined(_GLSL_)

// (deprecated, currently used in glsl shaders)

const uint RendererFlags_IrradianceBit = (1 << 0);

#else

// (future, currently used on host but not in slang shaders)

AER_SLANG_FLAGS
enum class RendererFlags : uint32_t {
  None              = 0,
  EnableIrradiance  = 1 << 0,
  All               = 0xFFFFFFFFu
};

#endif

// ----------------------------------------------------------------------------

#if defined(__cplusplus)

template<typename T>
constexpr auto underlying_cast(T val) noexcept {
  return static_cast<std::underlying_type_t<T>>(val);
}

template<typename T>
constexpr bool enable_bitmask_operators = false;

template<>
constexpr bool enable_bitmask_operators<RendererFlags> = true;

template<typename T>
requires (std::is_enum_v<T> && enable_bitmask_operators<T>)
constexpr T operator|(T lhs, T rhs) {
  return static_cast<T>(underlying_cast(lhs) | underlying_cast(rhs));
}

template<typename T>
requires (std::is_enum_v<T> && enable_bitmask_operators<T>)
constexpr T operator&(T lhs, T rhs) {
  return static_cast<T>(underlying_cast(lhs) & underlying_cast(rhs));
}

template<typename T>
requires (std::is_enum_v<T> && enable_bitmask_operators<T>)
constexpr T operator^(T lhs, T rhs) {
  return static_cast<T>(underlying_cast(lhs) ^ underlying_cast(rhs));
}

template<typename T>
requires (std::is_enum_v<T> && enable_bitmask_operators<T>)
constexpr T operator~(T rhs) {
  return static_cast<T>(~underlying_cast(rhs));
}

template<typename T>
requires (std::is_enum_v<T> && enable_bitmask_operators<T>)
constexpr T& operator|=(T& lhs, T rhs) {
  lhs = lhs | rhs;
  return lhs;
}

template<typename T>
requires (std::is_enum_v<T> && enable_bitmask_operators<T>)
constexpr T& operator&=(T& lhs, T rhs) {
  lhs = lhs & rhs;
  return lhs;
}

template<typename T>
requires (std::is_enum_v<T> && enable_bitmask_operators<T>)
constexpr bool has_flag(T flags, T flag) {
  return (underlying_cast(flags) & underlying_cast(flag)) == underlying_cast(flag);
}

#endif

/* -------------------------------------------------------------------------- */

#endif // SHADERS_MATERIAL_RENDERER_FLAGS_H_
