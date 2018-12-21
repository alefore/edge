# Fuzz testing

## Building

CXXFLAGS=-fpermissive CXX=$(pwd)/afl-2.39b/afl-g++ ./configure

## Running fuzzer

GLOG_logtostderr=y EDGE_TEST_STDIN=yes afl-fuzz -i testcases -o findings_dir .libs/fuzz_test
