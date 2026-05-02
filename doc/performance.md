# Performance Tracking

This document describes how to profile Edge.

1. Build Edge with profiling:
   bazel build src/edge --config=profiling

2. Run Edge under `perf` in order to collect data:
   perf record -g -- ./bazel-bin/src/edge

3. Visualize the profiling data:
   perf report -g
