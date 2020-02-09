#include "src/transformation/expand.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/file_link_mode.h"
#include "src/predictor.h"
#include "src/run_cpp_command.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/vm_transformation.h"

namespace afc::editor {
namespace {
std::wstring GetToken(const CompositeTransformation::Input& input,
                      EdgeVariable<wstring>* characters_variable) {
  if (input.position.column < ColumnNumber(2)) return L"";
  ColumnNumber end = input.position.column.previous().previous();
  auto line = input.buffer->LineAt(input.position.line);
  auto line_str = line->ToString();

  size_t index_before_symbol = line_str.find_last_not_of(
      input.buffer->Read(characters_variable), end.column);
  ColumnNumber symbol_start;
  if (index_before_symbol == wstring::npos) {
    symbol_start = ColumnNumber(0);
  } else {
    symbol_start = ColumnNumber(index_before_symbol + 1);
  }
  return line_str.substr(symbol_start.column,
                         (end - symbol_start).column_delta + 1);
}

std::unique_ptr<Transformation> DeleteLastCharacters(int characters) {
  DeleteOptions delete_options;
  delete_options.modifiers.direction = Direction::kBackwards;
  delete_options.modifiers.repetitions = characters;
  delete_options.modifiers.paste_buffer_behavior =
      Modifiers::PasteBufferBehavior::kDoNothing;
  return NewDeleteTransformation(std::move(delete_options));
}

class PredictorTransformation : public CompositeTransformation {
 public:
  PredictorTransformation(Predictor predictor, std::wstring text)
      : predictor_(std::move(predictor)), text_(std::move(text)) {}

  std::wstring Serialize() const override {
    return L"PredictorTransformation();";
  }

  futures::Value<Output> Apply(Input input) const override {
    PredictOptions predict_options;
    predict_options.editor_state = input.buffer->editor();
    predict_options.predictor = predictor_;
    predict_options.text = text_;
    // TODO: Ugh, the const_cast below is fucking ugly. I have a lake in my
    // model: should PredictionOptions::source_buffer be `const` so that it can
    // be applied here? But then... search handler can't really be mapped to a
    // predictor, since it wants to modify the buffer. Perhaps the answer is to
    // make search handler not modify the buffer, but rather do that on the
    // caller, based on its outputs.
    predict_options.source_buffers.push_back(
        std::const_pointer_cast<OpenBuffer>(input.buffer->shared_from_this()));
    return futures::ImmediateTransform(
        Predict(std::move(predict_options)),
        [text_size = text_.size(),
         editor = input.editor](std::optional<PredictResults> results) {
          if (!results.has_value() || !results->common_prefix.has_value())
            return Output();
          Output output;
          output.Push(DeleteLastCharacters(text_size));

          auto buffer_to_insert =
              OpenBuffer::New({.editor = editor, .name = L"- text inserted"});
          buffer_to_insert->AppendLazyString(
              NewLazyString(results.value().common_prefix.value()));
          buffer_to_insert->EraseLines(LineNumber(0), LineNumber(1));

          InsertOptions insert_options;
          insert_options.buffer_to_insert = buffer_to_insert;
          output.Push(NewInsertBufferTransformation(std::move(insert_options)));
          return output;
        });
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<PredictorTransformation>(predictor_, text_);
  }

 private:
  const Predictor predictor_;
  const std::wstring text_;
};

class ReadAndInsert : public CompositeTransformation {
 public:
  ReadAndInsert(std::wstring path) : path_(std::move(path)) {}

  std::wstring Serialize() const override { return L"ReadAndInsert();"; }

  futures::Value<Output> Apply(Input input) const override {
    OpenFileOptions open_file_options;
    open_file_options.editor_state = input.buffer->editor();
    open_file_options.path =
        PathJoin(input.buffer->editor()->edge_path().front(),
                 PathJoin(L"expand", path_));
    open_file_options.ignore_if_not_found = true;
    open_file_options.insertion_type = BuffersList::AddBufferType::kIgnore;
    open_file_options.use_search_paths = false;
    auto buffer_it = OpenFile(open_file_options);
    if (buffer_it == input.buffer->editor()->buffers()->end()) {
      return futures::Past(Output());
    }
    futures::Future<Output> output;
    buffer_it->second->AddEndOfFileObserver(
        [consumer = std::move(output.consumer),
         buffer_to_insert = buffer_it->second, input = std::move(input)] {
          Output output;
          InsertOptions options;
          options.buffer_to_insert = buffer_to_insert;
          output.Push(NewInsertBufferTransformation(std::move(options)));
          LineColumn position = buffer_to_insert->position();
          if (position.line.IsZero()) {
            position.column += input.position.column.ToDelta();
          }
          position.line += input.position.line.ToDelta();
          output.Push(NewSetPositionTransformation(position));
          consumer(std::move(output));
        });
    return output.value;
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<ReadAndInsert>(path_);
  }

 private:
  const std::wstring path_;
};

class Execute : public CompositeTransformation {
 public:
  Execute(std::wstring command) : command_(std::move(command)) {}

  std::wstring Serialize() const override { return L"Execute();"; }

  futures::Value<Output> Apply(Input input) const override {
    return futures::Transform(
        RunCppCommandShell(command_, input.editor),
        [command_size = command_.size(),
         editor = input.editor](std::unique_ptr<Value> value) {
          Output output;
          if (value != nullptr) {
            output.Push(DeleteLastCharacters(command_size));
          }
          if (value != nullptr && value->type == VMType::String()) {
            auto buffer_to_insert =
                OpenBuffer::New({.editor = editor, .name = L"- text inserted"});
            buffer_to_insert->AppendLazyString(NewLazyString(value->str));
            buffer_to_insert->EraseLines(LineNumber(0), LineNumber(1));
            output.Push(NewInsertBufferTransformation(
                {.buffer_to_insert = buffer_to_insert}));
          }
          return futures::Past(std::move(output));
        });
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<Execute>(command_);
  }

 private:
  const std::wstring command_;
};

class ExpandTransformation : public CompositeTransformation {
 public:
  std::wstring Serialize() const override { return L"ExpandTransformation();"; }

  futures::Value<Output> Apply(Input input) const override {
    Output output;
    if (input.position.column.IsZero()) return futures::Past(std::move(output));

    auto line = input.buffer->LineAt(input.position.line);
    auto c = line->get(input.position.column.previous());
    switch (c) {
      case 'r': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output.Push(DeleteLastCharacters(1 + symbol.size()));
        output.Push(NewTransformation(Modifiers(),
                                      std::make_unique<ReadAndInsert>(symbol)));
      } break;
      case '/': {
        auto path = GetToken(input, buffer_variables::path_characters);
        output.Push(DeleteLastCharacters(1));
        output.Push(NewTransformation(
            Modifiers(),
            std::make_unique<PredictorTransformation>(FilePredictor, path)));
      } break;
      case ' ': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output.Push(DeleteLastCharacters(1));
        output.Push(NewTransformation(Modifiers(),
                                      std::make_unique<PredictorTransformation>(
                                          SyntaxBasedPredictor, symbol)));
      } break;
      case ':': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output.Push(DeleteLastCharacters(1));
        output.Push(
            NewTransformation(Modifiers(), std::make_unique<Execute>(symbol)));
      }
    }
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<ExpandTransformation>();
  }

 private:
};
}  // namespace

std::unique_ptr<Transformation> NewExpandTransformation() {
  return NewTransformation(Modifiers(),
                           std::make_unique<ExpandTransformation>());
}
}  // namespace afc::editor
