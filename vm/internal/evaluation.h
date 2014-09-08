#ifndef __AFC_VM_EVALUATOR_H__
#define __AFC_VM_EVALUATOR_H__

#include "../public/vm.h"

namespace afc {
namespace vm {

struct Evaluation {
  Evaluation();

  Evaluation(const Evaluation& evaluation,
             const Expression::Continuation& continuation)
      : return_continuation(evaluation.return_continuation),
        continuation(continuation),
        environment(evaluation.environment) {}

  Expression::Continuation return_continuation;
  Expression::Continuation continuation;
  Environment* environment;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_EVALUATOR_H__
