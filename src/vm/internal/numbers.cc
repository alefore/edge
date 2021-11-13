#include "src/vm/internal/numbers.h"

#include <tgmath.h>

#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"

namespace afc::vm {
void RegisterNumberFunctions(Environment* environment) {
  environment->Define(
      L"pow", vm::NewCallback(std::function<double(double, double)>(pow),
                              VMType::PurityType::kPure));
}
}  // namespace afc::vm
