#!/usr/bin/bash
set -e
make -j $(nproc)
if [ -z "$EDGE_SKIP_TESTS" ]; then
  echo "Running tests."
  GLOG_vmodule=gc_tests=10 GLOG_alsologtostderr=y ./edge --tests=run --tests_filter=GC.RootAssignmentReleasesOld
fi
