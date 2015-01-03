// Copyright 2013-2014 the openage authors. See copying.md for legal info.

#ifndef OPENAGE_UTIL_MISC_H_
#define OPENAGE_UTIL_MISC_H_

namespace openage {
namespace util {

/**
 * modulo operation that guarantees to return positive values.
 */
template <typename T>
inline T mod(T x, T m) {
	T r = x % m;
	return r < 0 ? r + m : r;
}

/**
 * compiletime defined modulo function.
 */
template <typename T, unsigned int modulo>
inline T mod(T x) {
	T r = x % modulo;
	return r < 0 ? r + modulo : r;
}

/**
 * compiletime defined rotate left function
 */
template <typename T, int amount>
constexpr inline T rol(T x) {
	static_assert(8 * sizeof(T) > amount && amount > 0, "invalid rotation amount");
	return (x << amount) | (x >> (sizeof(T) * 8 - amount));
}

/**
 * implements the 'correct' version of the division operator,
 * which always rounds to -inf
 */
template <typename T>
inline T div(T x, T m) {
	return (x - mod<T>(x, m)) / m;
}

/**
 * generic callable, that compares any types for creating a total order.
 *
 * use for stdlib structures like std::set.
 * the template paramter has to be a pointer type.
 */
template <typename T>
struct less {
	bool operator ()(const T x, const T y) const {
		return *x < *y;
	}
};

} //namespace util
} //namespace openage

#endif
