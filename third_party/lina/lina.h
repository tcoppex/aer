//  lina.h - v0.5.0
//
//  Public domain linear algebra header, wrapping sgorsten/linalg.h
//  <http://unlicense.org/>
//

#ifndef LINA_LINA_H_
#define LINA_LINA_H_

/* -------------------------------------------------------------------------- */

#ifndef _MSC_VER
#define _MSC_VER 1901
#define UNDEF_MSC_VER
#endif

#include "linalg.h" // v2.2
using namespace linalg::aliases;

#ifdef UNDEF_MSC_VER
#undef _MSC_VER
#undef UNDEF_MSC_VER
#endif

#include <limits>

/* -------------------------------------------------------------------------- */

#ifdef LINA_NO_NAMESPACE

#define BEGIN_LINA_NAMESPACE  namespace {
#define END_LINA_NAMESPACE    }

#else

#ifndef LINA_NAMESPACE
#define LINA_NAMESPACE lina
#endif // LINA_NAMESPACE

#define BEGIN_LINA_NAMESPACE  namespace LINA_NAMESPACE {
#define END_LINA_NAMESPACE    }

#endif // LINA_NO_NAMESPACE

/* -------------------------------------------------------------------------- */

#ifdef LINA_USE_DOUBLE_PRECISION
#define LINA_FP double
#else
#define LINA_FP float
#endif

/* -------------------------------------------------------------------------- */

BEGIN_LINA_NAMESPACE

//
// Aliases.
//
namespace aliases {

template<class T> using vec2_t = linalg::vec<T,2>;
template<class T> using vec3_t = linalg::vec<T,3>;
template<class T> using vec4_t = linalg::vec<T,4>;

using vec2f = vec2_t<LINA_FP>;
using vec3f = vec3_t<LINA_FP>;
using vec4f = vec4_t<LINA_FP>;

using vec2i = vec2_t<int32_t>;
using vec3i = vec3_t<int32_t>;
using vec4i = vec4_t<int32_t>;

using vec2u = vec2_t<uint32_t>;
using vec3u = vec3_t<uint32_t>;
using vec4u = vec4_t<uint32_t>;

using mat3f = linalg::mat<LINA_FP, 3, 3>;
using mat4f = linalg::mat<LINA_FP, 4, 4>;
using mat3x4f = linalg::mat<LINA_FP, 3, 4>;
// using mat3x3f = mat3f;
// using mat4x4f = mat4f;

// GLSL types
using vec2 = vec2f;
using vec3 = vec3f;
using vec4 = vec4f;

using uvec2 = vec2u;
using uvec3 = vec3u;
using uvec4 = vec4u;

using ivec2 = vec2i;
using ivec3 = vec3i;
using ivec4 = vec4i;

using mat3 = mat3f;
using mat4 = mat4f;
using mat3x3 = mat3;
using mat3x4 = mat3x4f;
using mat4x4 = mat4;

} // namespace "aliases"

//
// Constants.
//
constexpr LINA_FP kTrueEpsilon  = std::numeric_limits<LINA_FP>::epsilon();
constexpr LINA_FP kEpsilon      = 10.0f * kTrueEpsilon;
constexpr LINA_FP kHalfLog      = -0.693147180599;  // log(0.5);
constexpr LINA_FP kQuarterPi    = 0.785398163397;   // atanf(1.0f);
constexpr LINA_FP kHalfPi       = 2.0 * kQuarterPi;
constexpr LINA_FP kPi           = 4.0 * kQuarterPi;
constexpr LINA_FP kTwoPi        = 8.0 * kQuarterPi;
constexpr LINA_FP kInvPi        = 1.0 / kPi;
constexpr LINA_FP kSqrtTwo      = 1.4142135623730951;
constexpr LINA_FP kHalfSqrtTwo  = 0.5 * kSqrtTwo;

//
// Functions.
//

template<class T, int M> constexpr T* ptr(linalg::vec<T, M> & v) { return &v.x; }
template<class T, int M, int N> constexpr T* ptr(linalg::mat<T, M, N> & m) { return &m.x.x; }

template<class T, int M> constexpr T const* ptr(linalg::vec<T, M> const& v) { return &v.x; }
template<class T, int M, int N> constexpr T const* ptr(linalg::mat<T, M, N> const& m) { return &m.x.x; }

template<class T> constexpr linalg::vec<T, 2> to_vec2(linalg::vec<T, 3> const& v) { return {v.x,v.y}; }
template<class T> constexpr linalg::vec<T, 3> to_vec3(linalg::vec<T, 4> const& v) { return {v.x,v.y,v.z}; }

template<class T> constexpr linalg::vec<T, 3> to_vec3(linalg::vec<T, 2> const& v, T z = 0) { return {v.x,v.y,z}; }
template<class T> constexpr linalg::vec<T, 4> to_vec4(linalg::vec<T, 3> const& v, T w = 0) { return {v.x,v.y,v.z,w}; }

template<class T> constexpr linalg::mat<T, 3, 3> to_mat3(linalg::mat<T, 4, 4> const& v) { return {to_vec3(v.x),to_vec3(v.y),to_vec3(v.z)}; }
template<class T> constexpr linalg::mat<T, 3, 4> to_mat3x4(linalg::mat<T, 4, 4> const& v) { return {to_vec3(v.x),to_vec3(v.y),to_vec3(v.z), to_vec3(v.w)}; }
template<class T> constexpr linalg::mat<T, 4, 4> to_mat4(linalg::mat<T, 3, 3> const& v) { return {to_vec4(v.x),to_vec4(v.y),to_vec4(v.z), {0, 0, 0, 1}}; }

template<class T> constexpr linalg::mat<T, 4, 4> remove_translation(linalg::mat<T, 4, 4> const& v) { return {v.x,v.y,v.z, {0, 0, 0, 1}}; }

template<class T> constexpr linalg::vec<T, 4> qidentity() { return {0,0,0,1}; }

template<class T> constexpr T degrees(T const& _radians) { return _radians * (180.0 / kPi); }
template<class T> constexpr T radians(T const& _degrees) { return _degrees * (kPi / 180.0); }

template<class T> constexpr T min4(T const& a, T const& b, T const& c, T const& d) { return min( a, min( b, min( c, d))); }
template<class T> constexpr T max4(T const& a, T const& b, T const& c, T const& d) { return max( a, max( b, max( c, d))); }

template<class T>
constexpr bool almost_equal(T const& a, T const& b, T tolerance) {
  static_assert(std::is_floating_point<T>::value);
  T const distance = linalg::abs(b - a);
  return (distance <= tolerance)
      || (distance <= std::numeric_limits<T>::min() * linalg::abs(b + a));
}

constexpr bool almost_equal(aliases::vec3 const& a, aliases::vec3 const& b, float tolerance) {
  return almost_equal(a.x, b.x, tolerance)
      && almost_equal(a.y, b.y, tolerance)
      && almost_equal(a.z, b.z, tolerance)
      ;
}

template<class T>
constexpr T saturate(T const& a) {
  static_assert( std::is_floating_point<T>::value );
  return linalg::clamp(a, 0, 1);
}

// ----------------------------------------------------------------------------

template<class T, int M>
constexpr linalg::vec<T,M> mul(const linalg::vec<T,4> & a, const linalg::mat<T,M,4> & b) {
  return a.x*b.row(0) + a.y*b.row(1) + a.z*b.row(2) + a.w*b.row(3);
}

// ----------------------------------------------------------------------------

template<class T>
linalg::mat<T,4,4> rotation_matrix_axis(linalg::vec<T, 3> const& axis, T const angle) {
  return linalg::rotation_matrix(
    linalg::rotation_quat(linalg::normalize(axis), angle)
  );
}

template<class T> linalg::mat<T,4,4> rotation_matrix_x(T const angle) { return rotation_matrix_axis(linalg::vec<T, 3>(1.0f, 0.0f, 0.0f), angle); }
template<class T> linalg::mat<T,4,4> rotation_matrix_y(T const angle) { return rotation_matrix_axis(linalg::vec<T, 3>(0.0f, 1.0f, 0.0f), angle); }
template<class T> linalg::mat<T,4,4> rotation_matrix_z(T const angle) { return rotation_matrix_axis(linalg::vec<T, 3>(0.0f, 0.0f, 1.0f), angle); }

// template<class T> linalg::mat<T,4,4> scaling_matrix(T const s) { return linalg::scaling_matrix(linalg::vec<T, 3>(s)); }

// ----------------------------------------------------------------------------

template<class T, int M>
constexpr linalg::vec<T, M> quadratic_bezier(
  linalg::vec<T, M> const& a, // start point
  linalg::vec<T, M> const& b, // control point
  linalg::vec<T, M> const& c, // end point
  T x
) {
  T u = (1.0 - x) * (1.0 - x);
  T v = 2.0 * x * (1.0 - x);
  T w = x * x;
  return u * a + v * b + w * c;
}

template<class T>
constexpr T step(T const& a, T const& x) {
  static_assert( std::is_floating_point<T>::value );
  return static_cast<T>(select(a > x, 0, 1));
}

template<class T>
constexpr T smoothstep(T const& edge0, T const& edge1, T x) {
  static_assert( std::is_floating_point<T>::value );
  x = saturate((x - edge0) / (edge1 - edge0));
  return x * x * (3.0 - 2.0 * x);
}

// Interpolate in two steps. ([0, 0.5, 1.0]->[a, b, c])
template<class T>
constexpr T trilerp(T const& a, T const& b, T const& c, T const& x) {
  static_assert( std::is_floating_point<T>::value );
  T l1 = a + 2 * x * (b - a);
  T l2 = b + 2 * (x - 1) * (c - b);
  return lerp(l1, l2, step(0.5, x));
}

// Interpolate following a pyramid curve. ([0, 0.5, 1.0]->[a, b, a])
template<class T>
constexpr T stepcurve(T const& a, T const& b, T const& x) {
  static_assert( std::is_floating_point<T>::value );
  return lerp(a, b, 1-abs(2*x-1));
}

// Adjust interpolation dynamics for a and b, clamping near boundaries for smooth transitions.
template<class T>
constexpr T bias(T const& a, T const& b) {
  static_assert( std::is_floating_point<T>::value );
  if (a < kEpsilon || b < kEpsilon) {
    return 0.0;
  }
  if (a > (1.0-kEpsilon) || b > (1.0-kEpsilon)) {
    return 1.0;
  }
  return pow(a, log(b) / kHalfLog);
}

template<class T>
constexpr T gain(T const& a, T const& b) {
  static_assert( std::is_floating_point<T>::value );
  if (a < kEpsilon) {
    return 0.0;
  }
  if (a > (1.0-kEpsilon)) {
    return 1.0;
  }
  const T bb = linalg::clamp(b, kEpsilon, 1.0 - kEpsilon);
  const T e = log(1.0-bb) / kHalfLog;
  auto f = [e](auto x) { return pow(2.0 * x, e) / 2.0; };
  return (a < 0.5) ? f(a) : 1.0 - f(1.0 - a);

  // equivalent to :
  //return lerp(f(a), 1.0-f(1.0-a), step(0.5, a));
  // (note : when using a smoothstep instead we obtain an elastic effect).
}

// ----------------------------------------------------------------------------

// The far plane is placed at infinity if farZ <= nearZ, better for far off objects.
// https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/b15ef6ce120dad1c7d3ff57039e73ba1a9f17102/src/common/xr_linear.h#L564
template<class T>
linalg::mat<T,4,4> frustum_tan_fov_matrix(T l, T r, T d, T u, T n, T f, linalg::fwd_axis a, linalg::z_range z)
{
  const T s = a == linalg::pos_z ? T(1) : T(-1),
          o = z == linalg::neg_one_to_one ? n : 0,
          zz = (f<=n) ? T(-1) : (f+o)/(n-f),
          zw = (f<=n) ? -(n+o) : (f*(n+o))/(n-f);
  const T w = T(1)/(r-l);
  const T h = T(1)/(u-d);
  return {{2*w,0,0,0},{0,2*h,0,0},{(r+l)*w,(u+d)*h,zz,s},{0,0,zw,0}};
}

template<class T>
linalg::mat<T,4,4> perspective_fov_matrix(
  T angleLeft,
  T angleRight,
  T angleDown,
  T angleUp,
  T n,
  T f,
  linalg::fwd_axis a = linalg::neg_z,
  linalg::z_range z = linalg::neg_one_to_one
) {
  return frustum_tan_fov_matrix(
    std::tan(angleLeft), std::tan(angleRight), std::tan(angleDown), std::tan(angleUp), n, f, a, z
  );
}

template <class T>
constexpr linalg::mat<T, 4, 4> rigidbody_inverse(const linalg::mat<T, 4, 4>& a) {
  auto rt = linalg::transpose(linalg::mat<T, 3, 3>{
    {a[0][0], a[0][1], a[0][2]},
    {a[1][0], a[1][1], a[1][2]},
    {a[2][0], a[2][1], a[2][2]}
  });
  auto inv_t = - linalg::mul(rt, linalg::vec<T, 3>{ a[0][3], a[1][3], a[2][3] });
  return linalg::mat<T, 4, 4>{
    {rt[0][0], rt[0][1], rt[0][2], inv_t[0]},
    {rt[1][0], rt[1][1], rt[1][2], inv_t[1]},
    {rt[2][0], rt[2][1], rt[2][2], inv_t[2]},
    {0.0, 0.0, 0.0, 1.0}
  };
}

// ----------------------------------------------------------------------------

#if 0
template<class T>
bool solve_basic_ik(
  T const lenA,
  T const lenB,
  const linalg::vec<T, 3>& target,
  const linalg::vec<T, 3>& targetUp,
  linalg::vec<T, 3>& _outIK
) {
    T const lenA2 = lenA * lenA;
    T const lenB2 = lenB * lenB;

    linalg::vec<T, 3> normalizedTarget = normalize(target);
    T const lenTarget = length(normalizedTarget);

    if (lenTarget < kEpsilon) {
      return false;
    }

    linalg::mat<T, 3, 3> Minv;
    Minv[0] = normalizedTarget;
    linalg::vec<T, 3> y_axis = normalize(targetUp - dot(targetUp, Minv[0]) * Minv[0]);
    Minv[1] = y_axis;
    Minv[2] = cross(Minv[0], y_axis);

    linalg::vec<T, 3> R = mul(Minv, target);
    T const lenR = length(R);

    if (lenR < kEpsilon) {
      return false;
    }

    linalg::vec<T, 3> S;
    S.x = linalg::clamp(0.5 * (lenR + (lenA2 - lenB2) / lenR), 0.0, (1.0 - kEpsilon) * lenA);

    T const ySquared = lenA2 - S.x * S.x;
    if (ySquared < 0.0) {
      return false;
    }
    S.y = sqrt(ySquared);
    S.z = 0;

    _outIK = mul(Minv, S);

    return S.x > 0.0;
}
#endif

// ----------------------------------------------------------------------------

END_LINA_NAMESPACE

/* -------------------------------------------------------------------------- */

//
// Types interoperability.
//

#if defined(LINA_OSTREAM)

#include <iostream>

template<class T> std::ostream &operator<<(std::ostream& o, linalg::vec<T,2> const& v) {
  return o << "(" << v.x << ", " << v.y << ")";
}

template<class T> std::ostream &operator<<(std::ostream& o, linalg::vec<T,3> const& v) {
  return o << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}

template<class T> std::ostream &operator<<(std::ostream& o, linalg::vec<T,4> const& v) {
  return o << '(' << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ')';
}

template<class T, int N> std::ostream &operator<<(std::ostream& o, linalg::mat<T,2,N> const& m) {
  return o << m.row(0) << std::endl
           << m.row(1);
}

template<class T, int N> std::ostream &operator<<(std::ostream& o, linalg::mat<T,3,N> const& m) {
  return o << m.row(0) << std::endl
           << m.row(1) << std::endl
           << m.row(2);
}

template<class T, int N> std::ostream &operator<<(std::ostream& o, linalg::mat<T,4,N> const& m) {
  return o << m.row(0) << std::endl
           << m.row(1) << std::endl
           << m.row(2) << std::endl
           << m.row(3);
}

#endif

/* -------------------------------------------------------------------------- */

#endif  // LINA_LINA_H_ 
