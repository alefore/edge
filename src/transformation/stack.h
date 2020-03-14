#ifndef __AFC_EDITOR_TRANSFORMATION_STACK_H__
#define __AFC_EDITOR_TRANSFORMATION_STACK_H__

#include <list>
#include <memory>

#include "src/transformation.h"
#include "src/transformation/type.h"

namespace afc::editor {
namespace transformation {
struct Stack {
  void PushBack(std::unique_ptr<editor::Transformation> transformation);
  void PushFront(std::unique_ptr<editor::Transformation> transformation);
  void PushFront(BaseTransformation transformation);

  // TODO: This shouldn't be a shared_ptr. I guess ideally it'd just be a list
  // of BaseTransformation.
  std::list<std::shared_ptr<Transformation>> stack;
};

futures::Value<Transformation::Result> ApplyBase(const Stack& parameters,
                                                 Transformation::Input input);
}  // namespace transformation

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_STACK_H__
