#ifndef __AFC_EDITOR_TRANSFORMATION_REPETITIONS_H__
#define __AFC_EDITOR_TRANSFORMATION_REPETITIONS_H__

#include <memory>

#include "src/transformation/input.h"
#include "src/transformation/result.h"
#include "src/transformation/type.h"
#include "src/vm/public/environment.h"

namespace afc::editor::transformation {
// Repeats a transformation a given number of times.
struct Repetitions {
  size_t repetitions;
  std::shared_ptr<Variant> transformation;
};

futures::Value<Result> ApplyBase(const Repetitions& parameters, Input input);
std::wstring ToStringBase(const Repetitions& v);
Variant OptimizeBase(Repetitions transformation);
}  // namespace afc::editor::transformation
#endif  // __AFC_EDITOR_TRANSFORMATION_REPETITIONS_H__
