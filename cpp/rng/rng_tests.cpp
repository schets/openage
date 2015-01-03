// Copyright 2014-2014 the openage authors. See copying.md for legal info.
#include "../log.h"
#include "rng.h"

#include <vector>
#include <cmath>

namespace openage {
namespace rng {
namespace tests {
constexpr size_t num_rand = 5000000;

static int rng_freq_t(void) {
	constexpr size_t dsize = std::numeric_limits<unsigned char>::max() + 1;
	constexpr size_t mean = num_rand / dsize;
	constexpr size_t max_diff = mean * 0.1;
	size_t data_points[dsize];
	for(auto& dat : data_points) {
		dat = 0;
	}
	rng<unsigned char> myrng(time_seed());
	for(size_t i = 0; i < num_rand; i++) {
		++data_points[myrng.random()];
	}
	for(size_t count : data_points) {
		if((size_t)abs(mean - count) > max_diff) {
				return 0;
		}
	}
	return -1;
}

static int rng_bool_t (void) {
	constexpr size_t expected = num_rand / 2;
	constexpr size_t max_diff = expected * 0.1;
	rng<bool> mybool(time_seed());
	size_t num_true = 0;
	for(size_t i = 0; i < num_rand; i++) {
		num_true = mybool() ? num_true + 1 : num_true;
	}
	if((size_t)abs(num_true - expected) > max_diff) {
				return 0;
	}
	return -1;
}

struct my_data {
	size_t datas[4];
};

static int rng_reproduce_t () {
	rng<my_data> test(10);
	rng<my_data> test2(10);
	auto comp_rands = [](const my_data &dat1, const my_data &dat2) {
		return !memcmp(&dat1, &dat2, sizeof(my_data));
	};
	for(size_t i = 0; i < num_rand; i++) {
		if(! comp_rands(test(), test2())) {
			return 0;
		}
	}
	return -1;
}

static int rng_serialize_t() {
	rng<my_data> test(time_seed());
	rng<my_data> test2(10);
	test2.from_string(test.to_string());
	auto comp_rands = [](const my_data &dat1, const my_data &dat2) {
		return !memcmp(&dat1, &dat2, sizeof(my_data));
	};
	for(size_t i = 0; i < num_rand; i++) {
		if(! comp_rands(test(), test2())) {
			return 0;
		}
	}
	return -1;
}


struct data_test {
	int (*test_fnc)();
	const char *name;
};
	
void perform_tests(std::vector<data_test> tests) {
	int ret;
	for(data_test test : tests) {
		if((ret = test.test_fnc()) != -1) {
			log::err("%s failed at stage %d", test.name, ret);
			throw "failed pairing heap tests";
		}
	}
}

void rng_tests() {
	perform_tests({
			{&rng_bool_t, "Tests the distribution of the specialized bool generator"},
			{&rng_freq_t, "Tests the distribution of the generic generator"},
			{&rng_reproduce_t, "Tests whether the rngs are reproducible"},
			{&rng_serialize_t, "Tests whether the rngs are serializable"},
		});
}

} //namespace tests
} //namespace rng
} //namespace openage
