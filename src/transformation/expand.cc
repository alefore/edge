#include "src/transformation/expand.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
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

namespace gc = afc::language::gc;

using afc::futures::OnError;
using afc::infrastructure::Path;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindLastNotOf;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::SortedLineSequence;
using afc::language::text::SortedLineSequenceUniqueLines;

namespace afc::editor {
namespace {

LazyString GetToken(const CompositeTransformation::Input& input,
                    EdgeVariable<LazyString>* characters_variable) {
  if (input.position.column < ColumnNumber(2)) return LazyString();
  const ColumnNumber end = input.position.column.previous().previous();
  const LazyString line =
      input.buffer.contents().snapshot().at(input.position.line).contents();

  std::wstring chars_str = input.buffer.Read(characters_variable);
  ColumnNumber symbol_start = VisitOptional(
      [](ColumnNumber index_before_symbol) {
        return index_before_symbol.next();
      },
      [] { return ColumnNumber(0); },
      FindLastNotOf(
          line.Substring(ColumnNumber{}, end.ToDelta()),
          std::unordered_set<wchar_t>(chars_str.begin(), chars_str.end())));

  return line.Substring(symbol_start,
                        end - symbol_start + ColumnNumberDelta(1));
}

transformation::Delete DeleteLastCharacters(ColumnNumberDelta characters) {
  CHECK_GT(characters, ColumnNumberDelta());
  return transformation::Delete{
      .modifiers = {.direction = Direction::kBackwards,
                    .repetitions = characters.read(),
                    .paste_buffer_behavior =
                        Modifiers::PasteBufferBehavior::kDoNothing},
      .initiator = transformation::Delete::Initiator::kInternal};
}

class PredictorTransformation : public CompositeTransformation {
 public:
  PredictorTransformation(Predictor predictor, LazyString text)
      : predictor_(std::move(predictor)), text_(std::move(text)) {
    CHECK_GT(text_.size(), ColumnNumberDelta());
  }

  std::wstring Serialize() const override {
    return L"PredictorTransformation();";
  }

  futures::Value<Output> Apply(Input input) const override {
    return Predict(
               predictor_,
               PredictorInput{
                   .editor = input.buffer.editor(),
                   .input = text_,
                   .input_column = ColumnNumber() + text_.size(),
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
              ColumnNumberDelta(results->common_prefix.value().size()) <
                  text.size()) {
            CHECK_LE(results->predictor_output.longest_prefix, text.size());
            LazyString prefix = text.Substring(
                ColumnNumber(0), results->predictor_output.longest_prefix);
            if (!prefix.size().IsZero()) {
              VLOG(5) << "Setting buffer status.";
              buffer.status().SetInformationText(LineBuilder{
                  LazyString{
                      L"No matches found. Longest prefix with matches: \""} +
                  prefix + LazyString{L"\""}}.Build());
            }
            return Output();
          }

          Output output;
          CHECK_GT(text.size(), ColumnNumberDelta());
          output.Push(DeleteLastCharacters(text.size()));
          output.Push(transformation::Insert{
              .contents_to_insert = LineSequence::WithLine(
                  Line(results.value().common_prefix.value()))});
          return output;
        });
  }

 private:
  const Predictor predictor_;
  const LazyString text_;
};

class InsertHistoryTransformation : public CompositeTransformation {
 public:
  InsertHistoryTransformation(transformation::Variant delete_transformation,
                              LazyString query)
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
              Error{LazyString{L"No matches: "} + search_options_.query});
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
        NonNull<std::unique_ptr<EditorState>> editor = EditorForTests();
        auto final_value =
            NewBufferForTests(editor.value())
                .ptr()
                ->ApplyToCursors(MakeNonNullShared<PredictorTransformation>(
                    [&](PredictorInput) -> futures::Value<PredictorOutput> {
                      return std::move(inner_future.value);
                    },
                    LazyString{L"foo"}));
        LOG(INFO) << "Notifying inner future";
        CHECK(!final_value.has_value());
        std::move(inner_future.consumer)(
            PredictorOutput{.longest_prefix = ColumnNumberDelta(2),
                            .contents = SortedLineSequenceUniqueLines(
                                SortedLineSequence(LineSequence()))});
        CHECK(final_value.has_value());
      }}});
}  // namespace

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
    auto full_path = Path::Join(
        edge_path_front,
        Path::Join(ValueOrDie(Path::New(LazyString{L"expand"})), path_));

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
               futures::Value<CompositeTransformation::Output> output =
                   ReadAndInsert(
                       ValueOrDie(Path::New(LazyString{L"unexistent"})),
                       [&](OpenFileOptions options) {
                         path_opened = options.path;
                         return futures::Past(
                             Error{LazyString{L"File does not exist."}});
                       })
                       .Apply(CompositeTransformation::Input{
                           .editor = buffer.ptr()->editor(),
                           .buffer = buffer.ptr().value()});
               CHECK(output.has_value());
               CHECK(path_opened.has_value());
               CHECK(path_opened.value() ==
                     ValueOrDie(Path::New(LazyString{
                         L"/home/edge-test-user/.edge/expand/unexistent"})));
             }},
    });

class Execute : public CompositeTransformation {
 public:
  Execute(LazyString command) : command_(std::move(command)) {}

  std::wstring Serialize() const override { return L"Execute();"; }

  futures::Value<Output> Apply(Input input) const override {
    return RunCppCommandShell(command_, input.editor)
        .Transform([](gc::Root<vm::Value> value) {
          Output output;
          if (value.ptr()->IsString()) {
            output.Push(transformation::Insert{
                .contents_to_insert =
                    LineSequence::WithLine(Line(value.ptr()->get_string()))});
          }
          return futures::Past(Success(std::move(output)));
        })
        .ConsumeErrors([](Error) { return futures::Past(Output()); });
  }

 private:
  const LazyString command_;
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
        LazyString symbol =
            GetToken(input, buffer_variables::symbol_characters);
        output->Push(
            DeleteLastCharacters(ColumnNumberDelta(1) + symbol.size()));
        std::visit(overload{IgnoreErrors{},
                            [&](Path path) {
                              transformation_future =
                                  futures::Past(std::make_unique<ReadAndInsert>(
                                      path, OpenFileIfFound));
                            }},
                   Path::New(symbol));
      } break;
      case '/':
        if (LazyString path =
                GetToken(input, buffer_variables::path_characters);
            !path.size().IsZero()) {
          output->Push(DeleteLastCharacters(ColumnNumberDelta(1)));
          transformation_future = futures::Past(
              std::make_unique<PredictorTransformation>(FilePredictor, path));
        }
        break;
      case ' ':
        if (LazyString symbol =
                GetToken(input, buffer_variables::symbol_characters);
            !symbol.size().IsZero()) {
          output->Push(DeleteLastCharacters(ColumnNumberDelta(1)));
          futures::Value<Predictor> predictor_future =
              futures::Past(SyntaxBasedPredictor);
          if (ValueOrError<Path> path = Path::New(
                  input.buffer.ReadLazyString(buffer_variables::dictionary));
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
        }
        break;
      case ':': {
        auto symbol = GetToken(input, buffer_variables::symbol_characters);
        output->Push(DeleteLastCharacters(ColumnNumberDelta(1) + symbol.size() +
                                          ColumnNumberDelta(1)));
        transformation_future =
            futures::Past(std::make_unique<Execute>(symbol));
      } break;
      case '.': {
        auto query = GetToken(input, buffer_variables::path_characters);
        transformation_future =
            futures::Past(std::make_unique<InsertHistoryTransformation>(
                DeleteLastCharacters(query.size() + ColumnNumberDelta(1)),
                query));
      }
    }
    return std::move(transformation_future)
        .Transform(
            [output = std::move(output)](
                std::unique_ptr<CompositeTransformation> transformation_input) {
              VisitPointer(
                  std::move(transformation_input),
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
