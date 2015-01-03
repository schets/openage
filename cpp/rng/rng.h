// Copyright 2014-2014 the openage authors. See copying.md for legal info.
#ifndef OPENAGE_RNG_RNG_H_
#define OPENAGE_RNG_RNG_H_

extern "C" {
#define HAVE_SSE2
#include "SFMT.h"
}

#include "../util/compiler.h"

#include <string>
#include <limits>
#include <iosfwd>

namespace std {

}

namespace openage {
namespace rng {
namespace _rng_h_ {

struct serialize_data {
	uint64_t *buffer;
	size_t n64;
	ptrdiff_t ptrdiff;
	sfmt_t& sm;
};

void rng_to_stream(std::ostream& strstr, const serialize_data& dat);
std::string rng_to_string(const serialize_data& data);
void rng_from_stream(std::istream& strstr, serialize_data& dat, size_t ts);
void rng_from_string(const std::string& instr, serialize_data& dat, size_t ts);

template<class T, class rng_t>
class base_rng {
	serialize_data get_ser_dat() {
		rng_t *myrng = static_cast<rng_t *>(this);
		return {(uint64_t *) myrng->buffer,
				rng_t::bytes / 8,
				myrng->cur_ptr - myrng->buffer,
				myrng->sm};
	}
public:

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

	void to_stream(std::ostream &strstr) {
		rng_to_stream(strstr, this->get_ser_dat);
	}

	std::string to_string() {
		return rng_to_string(this->get_ser_dat());
	}

	void from_stream(std::istream& strstr) {
		rng_t *myrng = static_cast<rng_t *>(this);
		serialize_data mydat = this->get_ser_dat();
		rng_from_stream(strstr, mydat, sizeof(T));
		myrng->cur_ptr = &myrng->buffer[0] + mydat.ptrdiff;
	}

	void from_string(const std::string& istr) {
		rng_t *myrng = static_cast<rng_t *>(this);
		serialize_data mydat = this->get_ser_dat();
		rng_from_string(istr, mydat, sizeof(T));
		myrng->cur_ptr = &myrng->buffer[0] + mydat.ptrdiff;
	}
};

} //namespace _rng_h_

template<class T, size_t block_size = (SFMT_N32 * 4)/sizeof(T)>
class rng : public _rng_h_::base_rng<T, rng<T, block_size>> {

protected:
	constexpr static size_t bytes = block_size * sizeof(T);
	static_assert(!(bytes % 16), "Total number of bytes in the block must be divisible by 16. Using the default block_size is recommended");
	
	constexpr static size_t num32 = bytes/4;

	//!The buffer which holds the data elements
	T buffer[block_size];

	T *const buf_end;

	//!The current buffer index
	T *cur_ptr;

	//!The state struct of the C RNG
	sfmt_t sm;

	rng()
		:
		buf_end(&buffer[0] + block_size) {
	}
	
public:
	using result_type = T;

	rng(uint32_t sval)
		:
		rng() {
		this->seed(sval);
	}

	inline T random() {
		if(unlikely(this->cur_ptr == buf_end)) {
			sfmt_fill_array32(&this->sm, (uint32_t*)this->buffer, num32);
			this->cur_ptr = this->buffer;
		}
		return *this->cur_ptr++;
	}

	inline void seed(uint32_t sval) {
		this->cur_ptr = this->buffer;
		sfmt_init_gen_rand(&this->sm, sval);
		sfmt_fill_array32(&this->sm, (uint32_t*)this->buffer, num32);
	}

	friend class _rng_h_::base_rng<T, rng<T, block_size>>;
	
};

template<size_t block_size>
class rng<bool, block_size> : public _rng_h_::base_rng<bool, rng<bool, block_size>> {

protected:
	using bit_type = unsigned char;

	constexpr static size_t bytes = block_size * sizeof(bool);
	static_assert(!(bytes % 16), "Total number of bytes in the block must be divisible by 16. Using the default block_size is recommended");
	
	constexpr static size_t num32 = bytes/4;

	//!The buffer which holds the data elements
	bit_type buffer[bytes];

	bit_type *const buf_end;

	//!The current buffer index
	bit_type* cur_ptr;

	//!The rng state
	sfmt_t sm;
	
	rng()
		:
		buf_end(&buffer[0] + block_size) {
	}
	
public:
	using result_type = bool;

	rng(uint32_t sval)
		:
		rng() {
		this->seed(sval);
	}

	inline bool random() {
		if(unlikely(this->cur_ptr == this->buf_end)) {
			sfmt_fill_array32(&this->sm, (uint32_t*)this->buffer, num32);
			this->cur_ptr = this->buffer;
		}
		return *this->cur_ptr++ & 1;
	}

	inline void seed(uint32_t sval) {
		this->cur_ptr = this->buffer;
		sfmt_init_gen_rand(&this->sm, sval);
		sfmt_fill_array32(&this->sm, (uint32_t*)this->buffer, num32);
	}

	friend class _rng_h_::base_rng<bool, rng<bool, block_size>>;
};

//!returns a seed based upon high-resolution time
uint32_t time_seed();

} //namespace rng
} //namespace openage

#endif
