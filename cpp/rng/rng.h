// Copyright 2014-2014 the openage authors. See copying.md for legal info.
#ifndef OPENAGE_RNG_RNG_H_
#define OPENAGE_RNG_RNG_H_

extern "C" {
#define HAVE_SSE2
#include "SFMT.h"
}

#include "../util/compiler.h"
#include "../util/misc.h"

#include <chrono>
#include <numeric>
#include <utility>
#include <iostream>

namespace openage {
namespace rng {

namespace _rng_h_ {
template<class T, class rng_t>
class base_rng {
public:

	static_assert(std::is_standard_layout<T>::value, "Value type must have a default layout");

	constexpr static inline T max() {
		return std::numeric_limits<T>::max();
	}

	constexpr static inline T min() {
		return std::numeric_limits<T>::min();
	}

	//!Returns a random value from the generator
	inline T operator()() {
		return static_cast<rng_t*>(this)->random();
	}

	//!discards the next n values from the rng
	inline void discard(unsigned long long num) {
		for(size_t i = 0; i < num; i++) {
			*(this)();
		}
	}

	//For edge cases, standard library distributions may be more appropriate
	//but they can be slower and are far clunkier

	//!Returns a random value in the given range
	inline T random_range(const T& lower, const T& upper) {
		return ((*this)() % (upper - lower)) + lower;
	}

	//!Returns a random float_type from '0' to '1', ~= random() / (max() - min())
	template<class float_type>
	inline float_type random_float() {
		return (float_type((*this)()) - min()) / (float_type(max()) - float_type(min()));
	}

	template<class float_type>
	inline float_type random_float(const float_type& min, const float_type& max) {
		return this->random_float<float_type>() * (max - min) + min;
	}

	//!Returns true with probability prob_true
	template<class float_type>
	inline bool with_probability(const float_type& prob_true) {
		return (*this)() < (max() * prob_true);
	}
};

} //namespace _rng_h_

template<class T, size_t block_size = (SFMT_N32 * 4)/sizeof(T)>
class rng : public _rng_h_::base_rng<T, rng<T, block_size>> {

	constexpr static size_t bytes = block_size * sizeof(T);
	static_assert(!(bytes % 16), "Total number of bytes in the block must be divisible by 16. Using the default block_size is recommended");
	
	constexpr static size_t num32 = bytes/4;

	//!The buffer which holds the data elements
	T buffer[block_size];

	T * const buf_end;

	//!The current buffer index
	T* cur_ptr;

	//!The state struct of the C RNG
	sfmt_t sm;
	
public:
	using result_type = T;

	rng(uint32_t sval)
		:
		buf_end(&buffer[0] + block_size) {
		this->seed(sval);
	}

	inline T random() {
		if(unlikely(this->cur_ptr == buf_end)) {
			sfmt_fill_array32(&sm, (uint32_t*)this->buffer, num32);
			this->cur_ptr = this->buffer;
		}
		return *this->cur_ptr++;
	}

	inline void seed(uint32_t sval) {
		this->cur_ptr = this->buffer;
		sfmt_init_gen_rand(&sm, sval);
		sfmt_fill_array32(&sm, (uint32_t*)this->buffer, num32);
	}
};

template<size_t block_size>
class rng<bool, block_size> : public _rng_h_::base_rng<bool, rng<bool, block_size>> {

	using bit_type = unsigned char;

	constexpr static size_t bytes = block_size * sizeof(bool);
	static_assert(!(bytes % 16), "Total number of bytes in the block must be divisible by 16. Using the default block_size is recommended");
	
	constexpr static size_t num32 = bytes/4;

	//!The buffer which holds the data elements
	bit_type buffer[bytes];

	const bit_type* buf_end;

	//!The current buffer index
	bit_type* cur_ptr;

	//!The rng state
	sfmt_t sm;
	
public:
	using result_type = bool;

	rng(uint32_t sval)
		:
		buf_end(&buffer[0] + bytes) {
		this->seed(sval);
	}

	inline bool random() {
		if(unlikely(this->cur_ptr == this->buf_end)) {
			sfmt_fill_array32(&sm, (uint32_t*)buffer, num32);
			this->cur_ptr = this->buffer;
		}
		return *this->cur_ptr++ & 1;
	}

	inline void seed(uint32_t sval) {
		this->cur_ptr = this->buffer;
		sfmt_init_gen_rand(&sm, sval);
		sfmt_fill_array32(&sm, (uint32_t*)buffer, num32);
	}
};

inline uint32_t time_seed() {
	uint64_t ct = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	return (uint32_t)(ct - (ct >> 32));
}

} //namespace rng
} //namespace openage

#endif
