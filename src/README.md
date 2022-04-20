# Directory layout

The code is structured in the following subdirectories:

## src/concurrent

Code that deals with concurrency: threads, protected sections, notifications...

### Dependencies

Code here should only depend on code in:

* src/futures
* src/tests

## src/language

This directory contains modules with generic infrastructure extending C++ to
make it safer.

### Dependencies

Code here should only depend on code in:

* src/tests

## src/tests

Infrastructure for defining unit tests.

### Dependencies

Code here should only depend on code in:

* src/language

## src/futures

Implementation of futures.

### Dependencies

Code here should only depend on code in:

* src/language
* src/tests

## src/infrastructure

Infrastructure for interacting with the Unix kernel.

Obviously, there's some overlap with src/language.

Reasons for src/infrastructure:

* Code that makes/exposes (wraps) syscalls.
* Code that interacts with the world.

Reasons for src/language:

* Code that augments the typing system (even if it makes syscalls).
* Code that is entirely contained within the process.

If in doubt, it probably belongs in src/infrastructure.

### Dependencies

Code here should only depend on code in:

* src/concurrent
* src/futures
* src/language
* src/tests

## src/math

Code that implements math formulas.

### Dependencies

Code here should only depend on code in:

* src/language

## src/vm

Implementation of our subset of C++.

### Dependencies

Code here should only depend on code in:

* src/futures
* src/language
* src/tests

## Checking dependencies

for d in concurrent language tests futures infrastructure math vm
do
  echo $d:
  grep ^.include src/$d/{*/,}*.{cc,h} 2>/dev/null | cut -f2 -d\  | grep 'src/' | cut -f2 -d'"' | cut -f2 -d/ | sort -u
  echo
done
