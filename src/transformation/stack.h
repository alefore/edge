#ifndef __AFC_EDITOR_TRANSFORMATION_STACK_H__
#define __AFC_EDITOR_TRANSFORMATION_STACK_H__

#include <list>
#include <memory>

#include "src/transformation.h"

namespace afc::editor {
class TransformationStack : public Transformation {
 public:
  void PushBack(std::unique_ptr<Transformation> transformation);
  void PushFront(std::unique_ptr<Transformation> transformation);

  void Apply(const Input& input, Result* result) const override;

  std::unique_ptr<Transformation> Clone() const override;

 private:
  std::list<std::unique_ptr<Transformation>> stack_;
};

std::unique_ptr<Transformation> ComposeTransformation(
    std::unique_ptr<Transformation> a, std::unique_ptr<Transformation> b);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_STACK_H__
