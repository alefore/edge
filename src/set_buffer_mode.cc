#include "src/set_buffer_mode.h"

namespace afc::editor {
namespace {
struct Operation {
  enum class Type { kForward, kBackward, kNumber };
  Type type;
  size_t number = 0;
};
struct Data {
  std::vector<Operation> operations;
  std::optional<size_t> initial_number;
};

std::unordered_map<wint_t, TransformationArgumentMode<Data>::CharHandler>
GetMap() {
  std::unordered_map<wint_t, TransformationArgumentMode<Data>::CharHandler>
      output;
  output['l'] = {.apply = [](Data data) {
    data.operations.push_back({Operation::Type::kForward});
    return data;
  }};

  output['h'] = {.apply = [](Data data) {
    data.operations.push_back({Operation::Type::kBackward});
    return data;
  }};

  for (size_t i = 0; i < 10; i++) {
    output['0' + i] = {.apply = [i](Data data) {
      data.operations.push_back({Operation::Type::kNumber, .number = i});
      return data;
    }};
  }

  return output;
}

std::wstring BuildStatus(const Data& data) {
  static constexpr auto initial_value = L"set-buffer";
  std::wstring output = initial_value;
  for (const auto& operation : data.operations) {
    output += L" ";
    switch (operation.type) {
      case Operation::Type::kForward:
        output += L"⮞";
        break;
      case Operation::Type::kBackward:
        output += L"⮜";
        break;
      case Operation::Type::kNumber:
        output += std::to_wstring(operation.number + 1);
        break;
    }
  }
  return output;
}

futures::Value<bool> Apply(EditorState* editor, Data data) {
  auto buffers_list = editor->buffer_tree();
  size_t index = data.initial_number.value_or(buffers_list->GetCurrentIndex());
  std::optional<Operation::Type> last_type;
  for (const auto& operation : data.operations) {
    switch (operation.type) {
      case Operation::Type::kForward:
        index++;
        break;

      case Operation::Type::kBackward:
        if (index == 0) {
          index = buffers_list->BuffersCount() - 1;
        } else {
          index--;
        }
        break;

      case Operation::Type::kNumber:
        if (last_type != Operation::Type::kNumber) {
          index = 0;
        }
        index = index * 10 + operation.number;
        break;
    }
    last_type = operation.type;
  }
  index %= buffers_list->BuffersCount();
  editor->set_current_buffer(buffers_list->GetBuffer(index));
  return futures::Past(true);
}

}  // namespace
std::unique_ptr<EditorMode> NewSetBufferMode(EditorState* editor) {
  static const auto characters_map = std::make_shared<std::unordered_map<
      wint_t, TransformationArgumentMode<Data>::CharHandler>>(GetMap());
  auto initial_buffer = editor->buffer_tree()->GetActiveLeaf()->Lock();

  TransformationArgumentMode<Data>::Options options{
      .editor_state = editor,
      .characters = characters_map,
      .status_factory = &BuildStatus,
      .undo =
          [editor, initial_buffer]() {
            editor->set_current_buffer(initial_buffer);
            return futures::Past(true);
          },
      .apply = [editor](Transformation::Input::Mode,
                        Data data) { return Apply(editor, data); }};

  return std::make_unique<TransformationArgumentMode<Data>>(std::move(options));
}
}  // namespace afc::editor
