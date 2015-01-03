add_sources(${PROJECT_NAME}
	SFMT.c
	rng_tests.cpp
)

add_test_cpp(openage::rng::tests::rng_tests "test functionality of rng - this *may* fail due to a statistical anomaly, but is extremely unlikely")
