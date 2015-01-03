// Copyright 2014-2014 the openage authors. See copying.md for legal info.
#include "rng.h"
#include "../util/error.h"

#include <chrono>
#include <sstream>

namespace openage {
namespace rng {
namespace _rng_h_{

void rng_to_stream(std::ostream& strstr, const serialize_data& dat) {
	strstr << dat.n64 <<  " ";
	strstr << SFMT_N64 << " ";

	for(size_t i = 0; i < dat.n64; i++) {
		strstr << dat.buffer[i] << " ";
	}

	strstr << dat.ptrdiff << " ";

	for(size_t i = 0; i < SFMT_N64; i++) {
		strstr << ((uint64_t *)dat.sm.state)[i] << " ";
	}
	strstr << dat.sm.idx;
}

std::string rng_to_string(const serialize_data& data) {
	std::stringstream dat;
	rng_to_stream(dat, data);
	return dat.str();
}

void rng_from_stream(std::istream& strstr, serialize_data& dat, size_t ts) {

	size_t test_bytes;
	strstr >> test_bytes;
	if(test_bytes != dat.n64) {
		throw util::Error("Trying to read an rng of block_size %d into an rng of size %d", int(8 * test_bytes / ts), int(8 * dat.n64 / ts));
	}

	strstr >> test_bytes;
	if(test_bytes != SFMT_N64) {
		throw util::Error("Trying to read an rng from SFMT_N64 == %d into an rng of SFMT_N64 == %d", int(test_bytes), SFMT_N64);
	}

	for(size_t i = 0; i < dat.n64; i++) {
		strstr >> dat.buffer[i];
	}
		
	strstr >> dat.ptrdiff;

	for(size_t i = 0; i < SFMT_N64; i++) {
		strstr >> ((uint64_t *)dat.sm.state)[i];
	}
	strstr >> dat.sm.idx;
}

void rng_from_string(const std::string& instr, serialize_data& dat, size_t ts) {
	std::stringstream ss(instr);
	rng_from_stream(ss, dat, ts);
}

} //namespace _rng_h_

uint32_t time_seed() {
	uint64_t ct = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	return (uint32_t)(ct - (ct >> 32));
}

} //namespace rng
} //namespace openage
