CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude

.PHONY: all demo test test-fast bench baseline clean

all: demo test test-fast

demo: bin/demo
	./bin/demo

test: bin/test
	./bin/test

test-fast: bin/test_fast
	./bin/test_fast

bench: bin/bench
	./bin/bench

baseline: bin/bench
	@chmod +x bench/run_baseline.sh
	./bench/run_baseline.sh

bin/bench: bench/latency_bench.cpp src/order_book.cpp src/fast_order_book.cpp include/order_book.hpp include/fast_order_book.hpp include/order.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) bench/latency_bench.cpp src/order_book.cpp src/fast_order_book.cpp -o bin/bench

bin/test_fast: tests/test_fast_order_book.cpp src/fast_order_book.cpp include/fast_order_book.hpp include/order.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) tests/test_fast_order_book.cpp src/fast_order_book.cpp -o bin/test_fast

bin/demo: src/main.cpp src/order_book.cpp include/order_book.hpp include/order.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) src/main.cpp src/order_book.cpp -o bin/demo

bin/test: tests/test_order_book.cpp src/order_book.cpp include/order_book.hpp include/order.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) tests/test_order_book.cpp src/order_book.cpp -o bin/test

clean:
	rm -rf bin
