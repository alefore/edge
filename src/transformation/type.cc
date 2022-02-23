#include "src/transformation/type.h"

#include "src/futures/futures.h"
#include "src/transformation/composite.h"
#include "src/vm_transformation.h"

namespace afc::editor::transformation {

futures::Value<Result> Apply(Variant base_transformation, const Input& input) {
  return std::visit([&](auto& value) { return ApplyBase(value, input); },
                    base_transformation);
}

std::wstring ToString(const Variant& transformation) {
  return std::visit(
      [&](auto& value) -> std::wstring { return ToStringBase(value); },
      transformation);
}

Input::Input(OpenBuffer* buffer) : buffer(buffer) {}

Input Input::NewChild(LineColumn new_position) const {
  Input child(buffer);
  child.mode = mode;
  child.delete_buffer = delete_buffer;
  child.position = new_position;
  return child;
}

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
  added_to_paste_buffer |= sub_result.added_to_paste_buffer;
  position = sub_result.position;
}

Variant Optimize(Variant transformation) {
  return std::visit(
      [](auto value) -> Variant { return OptimizeBase(std::move(value)); },
      std::move(transformation));
}

}  // namespace afc::editor::transformation
