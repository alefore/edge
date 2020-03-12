#ifndef __AFC_EDITOR_TRANSFORMATION_REPETITIONS_H__
#define __AFC_EDITOR_TRANSFORMATION_REPETITIONS_H__

#include <memory>

#include "src/transformation.h"
#include "src/vm/public/environment.h"

namespace afc::editor::transformation {
// Repeats a transformation a given number of times.
struct Repetitions {
  size_t repetitions;
  std::shared_ptr<Transformation> transformation;
};

futures::Value<Transformation::Result> ApplyBase(const Repetitions& parameters,
                                                 Transformation::Input input);

}  // namespace afc::editor::transformation
#endif  // __AFC_EDITOR_TRANSFORMATION_REPETITIONS_H__
