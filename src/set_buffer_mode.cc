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

bool CharConsumer(wint_t c, Data* data) {
  switch (c) {
    case L'l':
      data->operations.push_back({Operation::Type::kForward});
      return true;

    case L'h':
      data->operations.push_back({Operation::Type::kBackward});
      return true;

    case L'0':
    case L'1':
    case L'2':
    case L'3':
    case L'4':
    case L'5':
    case L'6':
    case L'7':
    case L'8':
    case L'9':
      data->operations.push_back(
          {.type = Operation::Type::kNumber, .number = c - L'0'});
      return true;

    default:
      return false;
  }
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
        output += std::to_wstring(operation.number);
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
        if (index == 1) {
          index = buffers_list->BuffersCount();
        } else {
          index--;
        }
        break;

      case Operation::Type::kNumber:
        if (last_type != Operation::Type::kNumber) {
          if (operation.number == 0) {
            break;
          }
          index = 0;
        }
        index = index * 10 + operation.number;
        break;
    }
    last_type = operation.type;
  }
  index--;  // Silly humans prefer to count from 1.
  index %= buffers_list->BuffersCount();
  editor->set_current_buffer(buffers_list->GetBuffer(index));
  return futures::Past(true);
}

}  // namespace
std::unique_ptr<EditorMode> NewSetBufferMode(EditorState* editor) {
  auto initial_buffer = editor->buffer_tree()->GetActiveLeaf()->Lock();

  TransformationArgumentMode<Data>::Options options{
      .editor_state = editor,
      .char_consumer = &CharConsumer,
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
