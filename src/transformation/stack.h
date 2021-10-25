#ifndef __AFC_EDITOR_TRANSFORMATION_STACK_H__
#define __AFC_EDITOR_TRANSFORMATION_STACK_H__

#include <list>
#include <memory>

#include "src/transformation.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"

namespace afc::editor {
namespace transformation {
struct Stack {
  enum PostTransformationBehavior {
    kNone,
    kDeleteRegion,
    kCopyRegion,
    kCommandSystem,
    kCommandCpp
  };

  void PushBack(Variant transformation);
  void PushFront(Variant transformation);
  std::list<Variant> stack;
  PostTransformationBehavior post_transformation_behavior =
      PostTransformationBehavior::kNone;
};

futures::Value<Result> ApplyBase(const Stack& parameters, Input input);
std::wstring ToStringBase(const Stack& stack);
}  // namespace transformation

transformation::Variant ComposeTransformation(transformation::Variant a,
                                              transformation::Variant b);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_STACK_H__
