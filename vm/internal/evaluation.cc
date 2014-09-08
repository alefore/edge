#include "evaluation.h"

#include <cassert>

#include "../public/value.h"
#include "../public/vm.h"

namespace afc {
namespace vm {

namespace {

pair<Expression::Continuation, unique_ptr<Value>> Crash(
    unique_ptr<Value> value_ignored) {
  assert(false);
  return make_pair(Expression::Continuation(Crash), nullptr);
}

}

Evaluation::Evaluation()
    : return_continuation(Crash),
      continuation(Crash),
      environment(nullptr) {}

}  // namespace vm
}  // namespace afc
