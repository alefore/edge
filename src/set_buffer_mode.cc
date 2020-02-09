#include "src/set_buffer_mode.h"

#include "src/char_buffer.h"
#include "src/tokenize.h"

namespace afc::editor {
namespace {
struct Operation {
  enum class Type { kForward, kBackward, kNumber, kFilter };
  Type type;
  size_t number = 0;
  std::wstring filter = L"";
};
struct Data {
  enum class State { kDefault, kReadingFilter };
  // If state is kReadingFilter, the back of operations must be of type kFilter.
  State state = State::kDefault;

  std::vector<Operation> operations;
  std::optional<size_t> initial_number;
};

bool CharConsumer(wint_t c, Data* data) {
  CHECK(data->state != Data::State::kReadingFilter ||
        (!data->operations.empty() &&
         data->operations.back().type == Operation::Type::kFilter));
  switch (data->state) {
    case Data::State::kDefault:
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
          if (data->operations.empty() ||
              data->operations.back().type != Operation::Type::kNumber) {
            if (c == L'0') {
              return true;
            }
            data->operations.push_back({Operation::Type::kNumber});
          }
          data->operations.back().number *= 10;
          data->operations.back().number += c - L'0';
          return true;

        case L'w':
          data->state = Data::State::kReadingFilter;
          data->operations.push_back({Operation::Type::kFilter});
          return true;
        default:
          return false;
      }

    case Data::State::kReadingFilter:
      switch (c) {
        case L'\n':
          data->state = Data::State::kDefault;
          return true;
        case Terminal::ESCAPE:
          return false;
        default:
          data->operations.back().filter.push_back(c);
          return true;
      }
  }
  LOG(FATAL) << "Invalid state!";
  return false;
}

std::wstring BuildStatus(const Data& data) {
  static constexpr auto initial_value = L"set-buffer";
  std::wstring output = initial_value;
  for (size_t i = 0; i < data.operations.size(); ++i) {
    const auto& operation = data.operations[i];
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
      case Operation::Type::kFilter:
        output += L" w:" + operation.filter;
        if (i == data.operations.size() - 1 &&
            data.state == Data::State::kReadingFilter) {
          output += L"…";
        }
        break;
    }
  }
  return output;
}

bool FilterMatches(const std::vector<Token>& filter, const OpenBuffer* buffer) {
  if (buffer == nullptr) return false;
  auto name = buffer->Read(buffer_variables::name);
  for (auto& token : filter) {
    // TODO(easy): Instead of this, split the name into tokens: first break it
    // by non-alpha, and then break it based on uppercase. Then improve the
    // search to assert that the filter tokens match a *prefix* of one of the
    // name's tokens.
    //
    // TODO(easy): Make this case insensitive.
    if (name.find(token.value) == string::npos) return false;
  }
  return true;
}

futures::Value<bool> Apply(EditorState* editor,
                           CommandArgumentModeApplyMode mode, Data data) {
  auto buffers_list = editor->buffer_tree();

  // Each entry is an index (e.g., for BuffersList::GetBuffer) for an available
  // buffer.
  std::vector<size_t> indices(buffers_list->BuffersCount());
  for (size_t i = 0; i < indices.size(); ++i) {
    indices[i] = i;
  }

  // This is an index into `indices`.
  size_t index = data.initial_number.value_or(buffers_list->GetCurrentIndex()) %
                 indices.size();
  for (const auto& operation : data.operations) {
    switch (operation.type) {
      case Operation::Type::kForward:
        index++;
        break;

      case Operation::Type::kBackward:
        if (index == 0) {
          index = indices.size() - 1;
        } else {
          index--;
        }
        break;

      case Operation::Type::kNumber: {
        CHECK_GT(operation.number, 0ul);
        int number_requested =
            (operation.number - 1) % buffers_list->BuffersCount();
        auto it = std::find_if(indices.begin(), indices.end(),
                               [number_requested](int index) {
                                 return index >= number_requested;
                               });
        index = it == indices.end() ? indices[0]
                                    : std::distance(indices.begin(), it);
      } break;

      case Operation::Type::kFilter:
        std::vector<size_t> new_indices;
        auto filter = TokenizeBySpaces(*NewLazyString(operation.filter));
        for (auto& index : indices) {
          if (FilterMatches(filter, buffers_list->GetBuffer(index).get())) {
            new_indices.push_back(index);
          }
        }
        if (new_indices.empty()) return futures::Past(true);
        indices = std::move(new_indices);
        break;
    }
  }
  CHECK(!indices.empty());
  index %= indices.size();
  editor->set_current_buffer(buffers_list->GetBuffer(indices[index]));
  switch (mode) {
    case CommandArgumentModeApplyMode::kFinal:
      editor->buffer_tree()->set_filter(std::nullopt);
      break;

    case CommandArgumentModeApplyMode::kPreview:
      if (indices.size() != buffers_list->BuffersCount()) {
        std::vector<std::weak_ptr<OpenBuffer>> filter;
        filter.reserve(indices.size());
        for (const auto& i : indices) {
          filter.push_back(buffers_list->GetBuffer(i));
        }
        editor->buffer_tree()->set_filter(std::move(filter));
      }
      break;
  }

  return futures::Past(true);
}

}  // namespace

std::unique_ptr<EditorMode> NewSetBufferMode(EditorState* editor) {
  auto buffers_list = editor->buffer_tree();
  Data initial_value;
  if (editor->modifiers().repetitions.has_value()) {
    editor->set_current_buffer(buffers_list->GetBuffer(
        (max(editor->modifiers().repetitions.value(), 1ul) - 1) %
        buffers_list->BuffersCount()));
    return nullptr;
  }
  auto initial_buffer = editor->buffer_tree()->GetActiveLeaf()->Lock();
  CommandArgumentMode<Data>::Options options{
      .editor_state = editor,
      .initial_value = std::move(initial_value),
      .char_consumer = &CharConsumer,
      .status_factory = &BuildStatus,
      .undo =
          [editor, initial_buffer]() {
            editor->set_current_buffer(initial_buffer);
            editor->buffer_tree()->set_filter(std::nullopt);
            return futures::Past(true);
          },
      .apply = [editor](CommandArgumentModeApplyMode mode,
                        Data data) { return Apply(editor, mode, data); }};

  return std::make_unique<CommandArgumentMode<Data>>(std::move(options));
}
}  // namespace afc::editor
