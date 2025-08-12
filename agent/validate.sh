#!/usr/bin/bash
make -j $(nproc) && ./edge --tests=run
