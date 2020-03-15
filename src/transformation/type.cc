#include "src/transformation/type.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor::transformation {

futures::Value<Result> Apply(BaseTransformation base_transformation,
                             const Input& input) {
  return std::visit([&](auto& value) { return ApplyBase(value, input); },
                    base_transformation);
}

std::wstring ToString(const Variant& transformation) {
  return std::visit(
      [&](auto& value) -> std::wstring { return ToStringBase(value); },
      transformation);
}

Input::Input(OpenBuffer* buffer) : buffer(buffer) {}

Result::Result(LineColumn position)
    : undo_stack(std::make_unique<transformation::Stack>()),
      position(position) {}

Result::Result(Result&&) = default;
Result::~Result() = default;

void Result::MergeFrom(Result sub_result) {
  success &= sub_result.success;
  made_progress |= sub_result.made_progress;
  modified_buffer |= sub_result.modified_buffer;
  undo_stack->PushFront(std::move(*sub_result.undo_stack));
  if (sub_result.delete_buffer != nullptr) {
    delete_buffer = std::move(sub_result.delete_buffer);
  }
  position = sub_result.position;
}

}  // namespace afc::editor::transformation
