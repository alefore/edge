#include "src/transformation/noop.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/transformation/vm.h"
#include "src/vm/types.h"

namespace gc = afc::language::gc;

using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;

namespace afc::editor {
namespace transformation {
namespace {
class Noop : public CompositeTransformation {
 public:
  static void Register(language::gc::Pool& pool, vm::Environment& environment) {
    environment.Define(
        vm::Identifier{
            NonEmptySingleLine{SingleLine{LazyString{L"NoopTransformation"}}}},
        vm::NewCallback(pool, vm::kPurityTypePure, []() {
          return MakeNonNullShared<Variant>(NewNoopTransformation());
        }));
  }

  std::wstring Serialize() const override { return L"NoopTransformation();"; }
  futures::Value<Output> Apply(Input) const override {
    return futures::Past(Output());
  }
};
}  // namespace
}  // namespace transformation

NonNull<std::unique_ptr<CompositeTransformation>> NewNoopTransformation() {
  return NonNull<std::unique_ptr<transformation::Noop>>();
}

void RegisterNoopTransformation(gc::Pool& pool, vm::Environment& environment) {
  transformation::Noop::Register(pool, environment);
}
}  // namespace afc::editor
