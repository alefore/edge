#!/usr/bin/bash
set -e
make -j $(nproc)
if [ -z "$EDGE_SKIP_TESTS" ]; then
  echo "Running tests."
  ./edge --tests=run
fi
