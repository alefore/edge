#include "src/transformation/expand.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/dirname.h"
#include "src/file_link_mode.h"
#include "src/predictor.h"
#include "src/run_cpp_command.h"
#include "src/tests/tests.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
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

transformation::Delete DeleteLastCharacters(int characters) {
  return transformation::Delete{
      .modifiers = {
          .direction = Direction::kBackwards,
          .repetitions = characters,
          .delete_behavior = Modifiers::DeleteBehavior::kDeleteText,
          .paste_buffer_behavior = Modifiers::PasteBufferBehavior::kDoNothing}};
}

class PredictorTransformation : public CompositeTransformation {
 public:
  PredictorTransformation(Predictor predictor, std::wstring text)
      : predictor_(std::move(predictor)), text_(std::move(text)) {}

  std::wstring Serialize() const override {
    return L"PredictorTransformation();";
  }

  futures::Value<Output> Apply(Input input) const override {
    return Predict({.editor_state = input.buffer->editor(),
                    .predictor = predictor_,
                    .text = text_,
                    // TODO: Ugh, the const_cast below is fucking ugly. I have a
                    // lake in my model: should PredictionOptions::source_buffer
                    // be `const` so that it can be applied here? But then...
                    // search handler can't really be mapped to a predictor,
                    // since it wants to modify the buffer. Perhaps the answer
                    // is to make search handler not modify the buffer, but
                    // rather do that on the caller, based on its outputs.
                    .source_buffers = {std::const_pointer_cast<OpenBuffer>(
                        input.buffer->shared_from_this())}})
        .Transform([text = text_, buffer = input.buffer](
                       std::optional<PredictResults> results) {
          if (!results.has_value()) {
            return Output();
          }
          if (!results->common_prefix.has_value() ||
              results->common_prefix.value().size() < text.size()) {
            CHECK_LE(results->longest_prefix, ColumnNumberDelta(text.size()));
            auto prefix = text.substr(0, results->longest_prefix.column_delta);
            if (!prefix.empty()) {
              buffer->status()->SetInformationText(
                  L"No matches found. Longest prefix with matches: \"" +
                  prefix + L"\"");
            }
            return Output();
          }

          Output output;
          output.Push(DeleteLastCharacters(text.size()));

          auto buffer_to_insert =
              OpenBuffer::New({.editor = buffer->editor(),
                               .name = BufferName::TextInsertion()});
          buffer_to_insert->AppendLazyString(
              NewLazyString(results.value().common_prefix.value()));
          buffer_to_insert->EraseLines(LineNumber(0), LineNumber(1));

          transformation::Insert insert_options;
          insert_options.buffer_to_insert = buffer_to_insert;
          output.Push(std::move(insert_options));
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

using OpenFileCallback = std::function<
    futures::Value<std::map<BufferName, std::shared_ptr<OpenBuffer>>::iterator>(
        const OpenFileOptions& options)>;

class ReadAndInsert : public CompositeTransformation {
 public:
  ReadAndInsert(Path path, OpenFileCallback open_file_callback)
      : path_(std::move(path)),
        open_file_callback_(std::move(open_file_callback)) {
    CHECK(open_file_callback_ != nullptr);
  }

  std::wstring Serialize() const override { return L"ReadAndInsert();"; }

  futures::Value<Output> Apply(Input input) const override {
    if (input.buffer->editor().edge_path().empty()) {
      LOG(INFO) << "Error preparing path for completion: Empty "
                   "edge_path.";
      return futures::Past(Output());
    }
    auto edge_path_front = input.buffer->editor().edge_path().front();
    auto full_path =
        Path::Join(edge_path_front,
                   Path::Join(Path::FromString(L"expand").value(), path_));
    futures::Future<Output> output;
    open_file_callback_(
        OpenFileOptions{.editor_state = input.buffer->editor(),
                        .path = full_path,
                        .ignore_if_not_found = true,
                        .insertion_type = BuffersList::AddBufferType::kIgnore,
                        .use_search_paths = false})
        .SetConsumer(
            [consumer = std::move(output.consumer), full_path,
             input = std::move(input)](
                std::map<BufferName, std::shared_ptr<OpenBuffer>>::iterator
                    buffer_it) {
              if (buffer_it == input.buffer->editor().buffers()->end()) {
                LOG(INFO) << "Unable to open file: " << full_path;
                consumer(Output());
                return;
              }

              buffer_it->second->AddEndOfFileObserver(
                  [consumer, buffer_to_insert = buffer_it->second,
                   input = std::move(input)] {
                    Output output;
                    output.Push(transformation::Insert{.buffer_to_insert =
                                                           buffer_to_insert});
                    LineColumn position = buffer_to_insert->position();
                    if (position.line.IsZero()) {
                      position.column += input.position.column.ToDelta();
                    }
                    position.line += input.position.line.ToDelta();
                    output.Push(transformation::SetPosition(position));
                    consumer(std::move(output));
                  });
            });
    return std::move(output.value);
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<ReadAndInsert>(path_, open_file_callback_);
  }

 private:
  const Path path_;
  const OpenFileCallback open_file_callback_;
};

const bool read_and_insert_tests_registration = tests::Register(
    L"ReadAndInsert",
    {
        {.name = L"BadPathCorrectlyHandled",
         .callback =
             [] {
               auto buffer = NewBufferForTests();
               std::optional<Path> path_opened;
               bool transformation_done = false;
               ReadAndInsert(
                   Path::FromString(L"unexistent").value(),
                   [&](OpenFileOptions options) {
                     path_opened = options.path;
                     return futures::Past(buffer->editor().buffers()->end());
                   })
                   .Apply(CompositeTransformation::Input{
                       .editor = buffer->editor(), .buffer = buffer.get()})
                   .SetConsumer([&](CompositeTransformation::Output) {
                     transformation_done = true;
                   });
               CHECK(transformation_done);
               CHECK(path_opened.has_value());
               CHECK(path_opened.value() ==
                     Path::FromString(
                         L"/home/edge-test-user/.edge/expand/unexistent")
                         .value());
             }},
    });

class Execute : public CompositeTransformation {
 public:
  Execute(std::wstring command) : command_(std::move(command)) {}

  std::wstring Serialize() const override { return L"Execute();"; }

  futures::Value<Output> Apply(Input input) const override {
    return RunCppCommandShell(command_, &input.editor)
        .Transform([command_size = command_.size(),
                    &editor = input.editor](std::unique_ptr<Value> value) {
          Output output;
          if (value != nullptr && value->IsString()) {
            auto buffer = OpenBuffer::New(
                {.editor = editor, .name = BufferName::TextInsertion()});
            buffer->AppendLazyString(NewLazyString(value->str));
            buffer->EraseLines(LineNumber(0), LineNumber(1));
            output.Push(
                transformation::Insert{.buffer_to_insert = std::move(buffer)});
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
    using transformation::ModifiersAndComposite;
    Output output;
    if (input.position.column.IsZero()) return futures::Past(std::move(output));

    auto line = input.buffer->LineAt(input.position.line);
    auto c = line->get(input.position.column.previous());
    std::unique_ptr<CompositeTransformation> transformation;
    switch (c) {
      case 'r': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output.Push(DeleteLastCharacters(1 + symbol.size()));
        if (auto path = Path::FromString(symbol); !path.IsError()) {
          transformation =
              std::make_unique<ReadAndInsert>(path.value(), OpenFile);
        }
      } break;
      case '/': {
        auto path = GetToken(input, buffer_variables::path_characters);
        output.Push(DeleteLastCharacters(1));
        transformation =
            std::make_unique<PredictorTransformation>(FilePredictor, path);
      } break;
      case ' ': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output.Push(DeleteLastCharacters(1));
        transformation = std::make_unique<PredictorTransformation>(
            SyntaxBasedPredictor, symbol);
      } break;
      case ':': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output.Push(DeleteLastCharacters(1 + symbol.size() + 1));
        transformation = std::make_unique<Execute>(symbol);
      }
    }
    if (transformation != nullptr) {
      output.Push(
          ModifiersAndComposite{.transformation = std::move(transformation)});
    }

    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<ExpandTransformation>();
  }
};
}  // namespace

std::unique_ptr<CompositeTransformation> NewExpandTransformation() {
  return std::make_unique<ExpandTransformation>();
}
}  // namespace afc::editor
