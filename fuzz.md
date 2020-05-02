# Fuzz testing

## Building

CXXFLAGS=-fpermissive CXX=$(pwd)/afl-2.39b/afl-g++ ./configure

## Create input

mkdir -p fuzz-data/{testcases,findings_dir}
edge fuzz-data/testcases/start

## Running fuzzer

GLOG_logtostderr=y EDGE_TEST_STDIN=yes afl-fuzz -i testcases -o findings_dir .libs/fuzz_test

Master:

GLOG_logtostderr=y afl-fuzz -i fuzz-data/testcases -o fuzz-data/findings_dir -M fuzz00 .libs/test_fuzz_vm

Slaves:

GLOG_logtostderr=y afl-fuzz -i fuzz-data/testcases -o fuzz-data/findings_dir -S fuzz01 .libs/test_fuzz_vm
GLOG_logtostderr=y afl-fuzz -i fuzz-data/testcases -o fuzz-data/findings_dir -S fuzz02 .libs/test_fuzz_vm
...

## Minimize

afl-tmin -i /tmp/crash -o /tmp/crash.min -- ./test_fuzz_vm
