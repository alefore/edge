#include "src/transformation/expand.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/predictor.h"
#include "src/run_cpp_command.h"
#include "src/tests/tests.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"
#include "src/transformation/vm.h"

namespace afc::editor {
using language::NonNull;
namespace {
using futures::OnError;
using infrastructure::Path;
using language::EmptyValue;
using language::Error;
using language::IgnoreErrors;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::overload;
using language::Success;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::Append;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineSequence;

namespace gc = language::gc;

std::wstring GetToken(const CompositeTransformation::Input& input,
                      EdgeVariable<std::wstring>* characters_variable) {
  if (input.position.column < ColumnNumber(2)) return L"";
  ColumnNumber end = input.position.column.previous().previous();
  auto line = input.buffer.LineAt(input.position.line);
  auto line_str = line->ToString();

  size_t index_before_symbol = line_str.find_last_not_of(
      input.buffer.Read(characters_variable), end.read());
  ColumnNumber symbol_start;
  if (index_before_symbol == std::wstring::npos) {
    symbol_start = ColumnNumber(0);
  } else {
    symbol_start = ColumnNumber(index_before_symbol + 1);
  }
  return line_str.substr(symbol_start.read(), (end - symbol_start).read() + 1);
}

transformation::Delete DeleteLastCharacters(int characters) {
  return transformation::Delete{
      .modifiers = {.direction = Direction::kBackwards,
                    .repetitions = characters,
                    .paste_buffer_behavior =
                        Modifiers::PasteBufferBehavior::kDoNothing},
      .initiator = transformation::Delete::Initiator::kInternal};
}

class PredictorTransformation : public CompositeTransformation {
 public:
  PredictorTransformation(Predictor predictor, std::wstring text)
      : predictor_(std::move(predictor)), text_(std::move(text)) {}

  std::wstring Serialize() const override {
    return L"PredictorTransformation();";
  }

  futures::Value<Output> Apply(Input input) const override {
    return Predict({.editor_state = input.buffer.editor(),
                    .predictor = predictor_,
                    .text = text_,
                    // TODO: Ugh, the const_cast below is fucking ugly. I have a
                    // lake in my model: should PredictionOptions::source_buffer
                    // be `const` so that it can be applied here? But then...
                    // search handler can't really be mapped to a predictor,
                    // since it wants to modify the buffer. Perhaps the answer
                    // is to make search handler not modify the buffer, but
                    // rather do that on the caller, based on its outputs.
                    .source_buffers = {(
                        const_cast<OpenBuffer*>(&input.buffer)->NewRoot())}})
        .Transform([text = text_, &buffer = input.buffer](
                       std::optional<PredictResults> results) {
          if (!results.has_value()) {
            return Output();
          }
          if (!results->common_prefix.has_value() ||
              results->common_prefix.value().size() < text.size()) {
            CHECK_LE(results->predictor_output.longest_prefix,
                     ColumnNumberDelta(text.size()));
            auto prefix =
                text.substr(0, results->predictor_output.longest_prefix.read());
            if (!prefix.empty()) {
              VLOG(5) << "Setting buffer status.";
              buffer.status().SetInformationText(MakeNonNullShared<Line>(
                  LineBuilder(Append(NewLazyString(L"No matches found. Longest "
                                                   L"prefix with matches: \""),
                                     NewLazyString(prefix),
                                     NewLazyString(L"\"")))
                      .Build()));
            }
            return Output();
          }

          Output output;
          output.Push(DeleteLastCharacters(text.size()));
          output.Push(transformation::Insert{
              .contents_to_insert =
                  LineSequence::WithLine(MakeNonNullShared<Line>(
                      results.value().common_prefix.value()))});
          return output;
        });
  }

 private:
  const Predictor predictor_;
  const std::wstring text_;
};

class InsertHistoryTransformation : public CompositeTransformation {
 public:
  InsertHistoryTransformation(transformation::Variant delete_transformation,
                              std::wstring query)
      : delete_transformation_(std::move(delete_transformation)),
        search_options_({.query = std::move(query)}) {}

  std::wstring Serialize() const override {
    return L"InsertHistoryTransformation";
  }

  futures::Value<Output> Apply(Input input) const override {
    Output output;
    VisitPointer(
        input.editor.insert_history().Search(input.editor, search_options_),
        [&](LineSequence text) {
          output.Push(delete_transformation_);
          output.Push(
              transformation::Insert{.contents_to_insert = std::move(text)});
        },
        [&] {
          input.editor.status().InsertError(
              Error(L"No matches: " + search_options_.query));
        });
    return futures::Past(std::move(output));
  }

 private:
  const transformation::Variant delete_transformation_;
  const InsertHistory::SearchOptions search_options_;
};

namespace {
bool predictor_transformation_tests_register = tests::Register(
    L"PredictorTransformation",
    {{.name = L"DeleteBufferDuringPrediction", .callback = [] {
        futures::Future<PredictorOutput> inner_future;
        OpenBuffer* predictions_buffer = nullptr;
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        auto final_value =
            NewBufferForTests(editor.value())
                .ptr()
                ->ApplyToCursors(MakeNonNullShared<PredictorTransformation>(
                    [&](PredictorInput input)
                        -> futures::Value<PredictorOutput> {
                      predictions_buffer = &input.predictions;
                      return std::move(inner_future.value);
                    },
                    L"foo"));
        // We've dropped our reference to the buffer, even though it still has
        // work scheduled. This is the gist of this tests: validate that the
        // transformation executes successfully regardless.
        LOG(INFO) << "Preparing predictions buffer";
        CHECK(predictions_buffer != nullptr);
        predictions_buffer->AppendLine(NewLazyString(L"footer"));
        predictions_buffer->AppendLine(NewLazyString(L"foxtrot"));
        predictions_buffer->EraseLines(LineNumber(), LineNumber().next());
        LOG(INFO) << "Signaling EOF to predictions_buffer.";
        predictions_buffer->EndOfFile();
        LOG(INFO) << "Notifying inner future";
        CHECK(!final_value.has_value());
        inner_future.consumer(
            PredictorOutput{.longest_prefix = ColumnNumberDelta(2)});
        CHECK(final_value.has_value());
      }}});
}

using OpenFileCallback =
    std::function<futures::ValueOrError<gc::Root<OpenBuffer>>(
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
    if (input.buffer.editor().edge_path().empty()) {
      LOG(INFO) << "Error preparing path for completion: Empty "
                   "edge_path.";
      return futures::Past(Output());
    }
    auto edge_path_front = input.buffer.editor().edge_path().front();
    auto full_path =
        Path::Join(edge_path_front,
                   Path::Join(ValueOrDie(Path::FromString(L"expand")), path_));

    return open_file_callback_(
               OpenFileOptions{
                   .editor_state = input.buffer.editor(),
                   .path = full_path,
                   .insertion_type = BuffersList::AddBufferType::kIgnore,
                   .use_search_paths = false})
        .Transform(
            [full_path, input = std::move(input)](gc::Root<OpenBuffer> buffer) {
              return buffer.ptr()->WaitForEndOfFile().Transform(
                  [buffer_to_insert = buffer,
                   input = std::move(input)](EmptyValue) {
                    Output output;
                    output.Push(transformation::Insert{
                        .contents_to_insert =
                            buffer_to_insert.ptr()->contents().snapshot()});
                    LineColumn position = buffer_to_insert.ptr()->position();
                    if (position.line.IsZero()) {
                      position.column += input.position.column.ToDelta();
                    }
                    position.line += input.position.line.ToDelta();
                    output.Push(transformation::SetPosition(position));
                    return Success(std::move(output));
                  });
            })
        .ConsumeErrors([full_path](Error) {
          LOG(INFO) << "Unable to open file: " << full_path;
          return futures::Past(Output());
        });
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
               NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
               gc::Root<OpenBuffer> buffer = NewBufferForTests(editor.value());
               std::optional<Path> path_opened;
               bool transformation_done = false;
               ReadAndInsert(
                   ValueOrDie(Path::FromString(L"unexistent")),
                   [&](OpenFileOptions options) {
                     path_opened = options.path;
                     return futures::Past(Error(L"File does not exist."));
                   })
                   .Apply(CompositeTransformation::Input{
                       .editor = buffer.ptr()->editor(),
                       .buffer = buffer.ptr().value()})
                   .SetConsumer([&](CompositeTransformation::Output) {
                     transformation_done = true;
                   });
               CHECK(transformation_done);
               CHECK(path_opened.has_value());
               CHECK(path_opened.value() ==
                     ValueOrDie(Path::FromString(
                         L"/home/edge-test-user/.edge/expand/unexistent")));
             }},
    });

class Execute : public CompositeTransformation {
 public:
  Execute(std::wstring command) : command_(std::move(command)) {}

  std::wstring Serialize() const override { return L"Execute();"; }

  futures::Value<Output> Apply(Input input) const override {
    return RunCppCommandShell(command_, input.editor)
        .Transform([command_size = command_.size()](gc::Root<vm::Value> value) {
          Output output;
          if (value.ptr()->IsString()) {
            output.Push(transformation::Insert{
                .contents_to_insert = LineSequence::WithLine(
                    MakeNonNullShared<Line>(value.ptr()->get_string()))});
          }
          return futures::Past(Success(std::move(output)));
        })
        .ConsumeErrors([](Error) { return futures::Past(Output()); });
  }

 private:
  const std::wstring command_;
};

class ExpandTransformation : public CompositeTransformation {
 public:
  std::wstring Serialize() const override { return L"ExpandTransformation();"; }

  futures::Value<Output> Apply(Input input) const override {
    using transformation::ModifiersAndComposite;
    auto output = std::make_shared<Output>();
    if (input.position.column.IsZero())
      return futures::Past(std::move(*output));

    auto line = input.buffer.LineAt(input.position.line);
    auto c = line->get(input.position.column.previous());
    futures::Value<std::unique_ptr<CompositeTransformation>>
        transformation_future = futures::Past(nullptr);
    switch (c) {
      case 'r': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output->Push(DeleteLastCharacters(1 + symbol.size()));
        std::visit(overload{IgnoreErrors{},
                            [&](Path path) {
                              transformation_future =
                                  futures::Past(std::make_unique<ReadAndInsert>(
                                      path, OpenFileIfFound));
                            }},
                   Path::FromString(symbol));
      } break;
      case '/': {
        auto path = GetToken(input, buffer_variables::path_characters);
        output->Push(DeleteLastCharacters(1));
        transformation_future = futures::Past(
            std::make_unique<PredictorTransformation>(FilePredictor, path));
      } break;
      case ' ': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output->Push(DeleteLastCharacters(1));
        futures::Value<Predictor> predictor_future =
            futures::Past(SyntaxBasedPredictor);
        if (ValueOrError<Path> path = Path::FromString(
                input.buffer.Read(buffer_variables::dictionary));
            !IsError(path)) {
          predictor_future =
              OpenFileIfFound(
                  OpenFileOptions{
                      .editor_state = input.buffer.editor(),
                      .path = ValueOrDie(std::move(path)),
                      .insertion_type = BuffersList::AddBufferType::kIgnore,
                      .use_search_paths = false})
                  .Transform([](gc::Root<OpenBuffer> dictionary) {
                    return Success(ComposePredictors(
                        DictionaryPredictor(std::move(dictionary)),
                        SyntaxBasedPredictor));
                  })
                  .ConsumeErrors([](Error) -> futures::Value<Predictor> {
                    return futures::Past(SyntaxBasedPredictor);
                  });
        }
        transformation_future =
            std::move(predictor_future)
                .Transform([symbol](Predictor predictor) {
                  return std::make_unique<PredictorTransformation>(predictor,
                                                                   symbol);
                });
      } break;
      case ':': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output->Push(DeleteLastCharacters(1 + symbol.size() + 1));
        transformation_future =
            futures::Past(std::make_unique<Execute>(symbol));
      } break;
      case '.': {
        auto query = GetToken(input, buffer_variables::path_characters);
        transformation_future =
            futures::Past(std::make_unique<InsertHistoryTransformation>(
                DeleteLastCharacters(query.size() + 1), query));
      }
    }
    return std::move(transformation_future)
        .Transform(
            [output = std::move(output)](
                std::unique_ptr<CompositeTransformation> transformation) {
              VisitPointer(
                  std::move(transformation),
                  [&output](NonNull<std::unique_ptr<CompositeTransformation>>
                                transformation) {
                    output->Push(ModifiersAndComposite{
                        .transformation = std::move(transformation)});
                  },
                  [] {});
              return futures::Past(std::move(*output));
            });
  }
};
}  // namespace

NonNull<std::unique_ptr<CompositeTransformation>> NewExpandTransformation() {
  return MakeNonNullUnique<ExpandTransformation>();
}
}  // namespace afc::editor
