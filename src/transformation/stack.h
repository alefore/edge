#ifndef __AFC_EDITOR_TRANSFORMATION_STACK_H__
#define __AFC_EDITOR_TRANSFORMATION_STACK_H__

#include <list>
#include <memory>

#include "src/transformation.h"

namespace afc::editor {
class TransformationStack : public Transformation {
 public:
  TransformationStack();
  void PushBack(std::unique_ptr<Transformation> transformation);
  void PushFront(std::unique_ptr<Transformation> transformation);

  futures::DelayedValue<Result> Apply(const Input& input) const override;

  std::unique_ptr<Transformation> Clone() const override;
  // Same as `Clone` but returns the specific type.
  std::unique_ptr<TransformationStack> CloneStack() const;

 private:
  // We use a shared_ptr so that a TransformationStack can be deleted while the
  // evaluation of `Apply` is still running.
  std::shared_ptr<std::list<std::unique_ptr<Transformation>>> stack_;
};

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_STACK_H__
