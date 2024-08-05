#ifndef __AFC_EDITOR_TRANSFORMATION_STACK_H__
#define __AFC_EDITOR_TRANSFORMATION_STACK_H__

#include <list>
#include <memory>

#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"
#include "src/transformation/type.h"

namespace afc::editor {
namespace transformation {
class ShellCommand : public language::GhostType<ShellCommand, std::wstring> {
 public:
  language::lazy_string::LazyString ReadLazyString() const {
    return language::lazy_string::LazyString{read()};
  }
};

struct Stack {
  using value_type = Variant;

  enum PostTransformationBehavior {
    kNone,
    kDeleteRegion,
    kCopyRegion,
    kCommandSystem,
    kCommandCpp,
    kCapitalsSwitch,
    // If the region is non-empty, remove the cursors of the current document
    // and add a cursor in every line that intersects with the range.
    kCursorOnEachLine,
  };

  std::list<Variant> stack;
  PostTransformationBehavior post_transformation_behavior =
      PostTransformationBehavior::kNone;

  // Used if post_transformation_behavior is kCommandSystem.
  std::optional<ShellCommand> shell = std::nullopt;

  void push_back(Variant transformation);
  void push_front(Variant transformation);
};

Variant OptimizeBase(Stack stack);

futures::Value<Result> ApplyBase(const Stack& parameters, Input input);
std::wstring ToStringBase(const Stack& stack);
}  // namespace transformation

transformation::Variant ComposeTransformation(transformation::Variant a,
                                              transformation::Variant b);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_STACK_H__
