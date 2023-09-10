#include "src/transformation/stack.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/log.h"
#include "src/run_command_handler.h"
#include "src/tests/tests.h"
#include "src/transformation/composite.h"
#include "src/transformation/input.h"
#include "src/transformation/switch_case.h"
#include "src/transformation/vm.h"

namespace afc::editor {
namespace transformation {
using ::operator<<;

using infrastructure::screen::LineModifier;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::NonNull;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::lazy_string::Append;
using language::lazy_string::ColumnNumber;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::LineNumberDelta;
using language::text::LineSequence;
using language::text::MutableLineSequence;
using language::text::Range;

namespace gc = language::gc;
namespace {
void ShowValue(OpenBuffer& buffer, OpenBuffer* delete_buffer,
               const vm::Value& value) {
  if (value.IsVoid()) return;
  std::ostringstream oss;
  oss << value;
  buffer.status().SetInformationText(Append(
      NewLazyString(L"Value: "), NewLazyString(FromByteString(oss.str()))));
  if (delete_buffer != nullptr) {
    std::istringstream iss(oss.str());
    for (std::string line_str; std::getline(iss, line_str);) {
      delete_buffer->AppendToLastLine(
          LineBuilder(NewLazyString(FromByteString(line_str))).Build());
      delete_buffer->AppendRawLine(MakeNonNullShared<Line>());
    }
  }
}

futures::Value<PossibleError> PreviewCppExpression(
    OpenBuffer& buffer, const LineSequence& expression_str) {
  FUTURES_ASSIGN_OR_RETURN(auto compilation_result,
                           buffer.CompileString(expression_str.ToString()));
  auto [expression, environment] = std::move(compilation_result);
  buffer.status().Reset();
  switch (expression->purity()) {
    case vm::PurityType::kReader:
    case vm::PurityType::kPure:
      return buffer.EvaluateExpression(expression.value(), environment)
          .Transform([&buffer](gc::Root<vm::Value> value) {
            ShowValue(buffer, nullptr, value.ptr().value());
            return Success();
          })
          .ConsumeErrors([&buffer](Error error) {
            buffer.status().SetInformationText(
                Append(NewLazyString(L"E: "), NewLazyString(error.read())));
            return futures::Past(EmptyValue());
          })
          .Transform([](EmptyValue) { return futures::Past(Success()); });
    case vm::PurityType::kUnknown:
      break;
  }
  return futures::Past(Success());
}

futures::Value<Result> HandleCommandCpp(Input input,
                                        Delete original_delete_transformation) {
  NonNull<std::unique_ptr<MutableLineSequence>> contents =
      input.buffer.contents().copy();
  contents->FilterToRange(*original_delete_transformation.range);
  if (input.mode == Input::Mode::kPreview) {
    auto delete_transformation =
        std::make_shared<Delete>(std::move(original_delete_transformation));
    delete_transformation->preview_modifiers = {LineModifier::kGreen,
                                                LineModifier::kUnderline};
    return PreviewCppExpression(input.buffer, contents->snapshot())
        .ConsumeErrors(
            [&buffer = input.buffer, delete_transformation](Error error) {
              delete_transformation->preview_modifiers = {
                  LineModifier::kRed, LineModifier::kUnderline};
              buffer.status().SetInformationText(NewLazyString(error.read()));
              return futures::Past(EmptyValue());
            })
        .Transform([delete_transformation, input](EmptyValue) {
          return Apply(*delete_transformation,
                       input.NewChild(delete_transformation->range->begin));
        });
  }
  return input.buffer.EvaluateString(contents->snapshot().ToString())
      .Transform([input](gc::Root<vm::Value> value) {
        ShowValue(input.buffer, input.delete_buffer, value.ptr().value());
        Result output(input.position);
        output.added_to_paste_buffer = true;
        return Success(std::move(output));
      })
      .ConsumeErrors([input](Error error) {
        Result output(input.position);
        error = AugmentError(L"💣 Runtime error", std::move(error));
        input.buffer.status().Set(error);
        if (input.delete_buffer != nullptr) {
          input.delete_buffer->AppendToLastLine(
              LineBuilder(NewLazyString(error.read())).Build());
          input.delete_buffer->AppendRawLine(MakeNonNullShared<Line>());
          output.added_to_paste_buffer = true;
        }
        return futures::Past(std::move(output));
      });
}

template <typename Iterator>
futures::Value<EmptyValue> ApplyStackDirectly(
    Iterator begin, Iterator end, Input& input,
    NonNull<std::shared_ptr<Log>> trace, std::shared_ptr<Result> output) {
  return futures::ForEach(
             begin, end,
             [output, &input,
              trace](const transformation::Variant& transformation) {
               trace->Append(L"Transformation: " + ToString(transformation));
               return Apply(transformation, input.NewChild(output->position))
                   .Transform([output](Result result) {
                     output->MergeFrom(std::move(result));
                     return output->success
                                ? futures::IterationControlCommand::kContinue
                                : futures::IterationControlCommand::kStop;
                   });
             })
      .Transform([](futures::IterationControlCommand) { return EmptyValue(); });
}

void FlattenInto(std::list<Variant>& output, Variant& input) {
  if (Stack* input_stack = std::get_if<Stack>(&input);
      input_stack != nullptr && (input_stack->post_transformation_behavior ==
                                     Stack::PostTransformationBehavior::kNone ||
                                 input_stack->stack.empty())) {
    for (auto& sub_input : input_stack->stack) {
      FlattenInto(output, sub_input);
    }
  } else {
    output.push_back(std::move(input));
  }
}
}  // namespace

Variant OptimizeBase(Stack stack) {
  if (stack.post_transformation_behavior !=
      Stack::PostTransformationBehavior::kNone)
    return stack;
  for (auto& t : stack.stack) {
    t = Optimize(std::move(t));
  }

  std::list<Variant> new_stack;
  for (auto& element : stack.stack) {
    FlattenInto(new_stack, element);
  }
  stack.stack = std::move(new_stack);

  VLOG(5) << "Removing consecutive calls to SetPosition or Cursors.";
  for (auto it = stack.stack.begin();
       it != stack.stack.end() && std::next(it) != stack.stack.end();) {
    auto next_it = std::next(it);
    SetPosition* current = std::get_if<SetPosition>(&*it);
    if (current != nullptr) {
      if (SetPosition* next = std::get_if<SetPosition>(&*next_it);
          next != nullptr) {
        if (!next->line.has_value()) next->line = current->line;
        stack.stack.erase(it);
      } else if (std::get_if<Cursors>(&*next_it)) {
        stack.stack.erase(it);
      }
    }
    it = next_it;
  }

  if (stack.stack.empty()) return stack;
  if (stack.stack.size() == 1) return stack.stack.front();
  return stack;
}

namespace {
struct ContentStats {
  size_t lines = 0;
  size_t words = 0;
  size_t alnums = 0;
  size_t characters = 0;

  bool operator==(const ContentStats& other) const {
    return lines == other.lines && words == other.words &&
           alnums == other.alnums && characters == other.characters;
  }
};

std::wstring ToString(const ContentStats& stats) {
  return L"L:" + std::to_wstring(stats.lines) + L" W:" +
         std::to_wstring(stats.words) + L" A:" + std::to_wstring(stats.alnums) +
         L" C:" + std::to_wstring(stats.characters);
}

ContentStats AnalyzeContent(const LineSequence& contents) {
  ContentStats output{.lines = contents.EndLine().read() + 1};
  contents.ForEach([&output](const NonNull<std::shared_ptr<const Line>>& line) {
    ColumnNumber i;
    output.characters += line->EndColumn().read();
    while (i < line->EndColumn()) {
      while (i < line->EndColumn() && !isalnum(line->get(i))) ++i;
      if (i < line->EndColumn()) ++output.words;
      while (i < line->EndColumn() && isalnum(line->get(i))) {
        ++i;
        ++output.alnums;
      }
    }
  });
  VLOG(7) << "AnalyzeContent: Output: " << ToString(output);
  return output;
}

const bool analyze_content_tests_registration = tests::Register(
    L"transformation::Stack::AnalyzeContent",
    {{.name = L"Empty",
      .callback =
          [] {
            CHECK(AnalyzeContent(LineSequence()) ==
                  ContentStats(
                      {.lines = 1, .words = 0, .alnums = 0, .characters = 0}));
          }},
     {.name = L"SingleWord",
      .callback =
          [] {
            CHECK(AnalyzeContent(MutableLineSequence::WithLine(
                                     MakeNonNullShared<Line>(Line(L"foo")))
                                     .snapshot()) ==
                  ContentStats(
                      {.lines = 1, .words = 1, .alnums = 3, .characters = 3}));
          }},
     {.name = L"SingleLine",
      .callback =
          [] {
            CHECK(AnalyzeContent(
                      MutableLineSequence::WithLine(
                          MakeNonNullShared<Line>(Line(L"foo bar hey alejo")))
                          .snapshot()) == ContentStats({.lines = 1,
                                                        .words = 4,
                                                        .alnums = 3 + 3 + 3 + 5,
                                                        .characters = 17}));
          }},
     {.name = L"SpacesSingleLine",
      .callback =
          [] {
            CHECK(AnalyzeContent(MutableLineSequence::WithLine(
                                     MakeNonNullShared<Line>(Line(
                                         L"   foo    bar   hey   alejo   ")))
                                     .snapshot()) ==
                  ContentStats({.lines = 1,
                                .words = 4,
                                .alnums = 3 + 3 + 3 + 5,
                                .characters = 30}));
          }},
     {.name = L"VariousEmptyLines", .callback = [] {
        MutableLineSequence contents;
        contents.append_back({MakeNonNullShared<const Line>(L"foo"),
                              {},
                              {},
                              {},
                              MakeNonNullShared<const Line>(L"bar")});
        CHECK(AnalyzeContent(contents.snapshot()) ==
              ContentStats({.lines = 6,
                            .words = 2,
                            .alnums = 3 + 3,
                            .characters = 3 + 3}));
      }}});
}  // namespace

futures::Value<Result> ApplyBase(const Stack& parameters, Input input) {
  auto output = std::make_shared<Result>(input.position);
  auto copy = std::make_shared<Stack>(parameters);
  NonNull<std::shared_ptr<Log>> trace =
      input.buffer.log().NewChild(L"ApplyBase(Stack)");
  return ApplyStackDirectly(copy->stack.begin(), copy->stack.end(), input,
                            trace, output)
      .Transform([output, input, copy, trace](EmptyValue) {
        Range range{std::min(std::min(input.position, output->position),
                             input.buffer.end_position()),
                    std::min(std::max(input.position, output->position),
                             input.buffer.end_position())};
        Delete delete_transformation{
            .modifiers = {.direction = input.position < output->position
                                           ? Direction::kForwards
                                           : Direction::kBackwards},
            .range = range,
            .initiator = transformation::Delete::Initiator::kInternal};
        switch (copy->post_transformation_behavior) {
          case Stack::PostTransformationBehavior::kNone: {
            NonNull<std::shared_ptr<MutableLineSequence>> contents =
                input.buffer.contents().copy();
            contents->FilterToRange(range);
            input.buffer.status().Reset();
            DVLOG(5) << "Analyze contents for range: " << range;
            return PreviewCppExpression(input.buffer, contents->snapshot())
                .ConsumeErrors(
                    [](Error) { return futures::Past(EmptyValue()); })
                .Transform([input, output, contents](EmptyValue) {
                  if (input.mode == Input::Mode::kPreview &&
                      input.buffer.status().text().empty() &&
                      contents->EndLine() <
                          LineNumber(input.buffer.Read(
                              buffer_variables::analyze_content_lines_limit))) {
                    // TODO(easy, 2023-09-08): Change ToString to return a lazy
                    // string.
                    input.buffer.status().SetInformationText(Append(
                        NewLazyString(L"Selection: "),
                        NewLazyString(
                            ToString(AnalyzeContent(contents->snapshot())))));
                  }
                  return futures::Past(std::move(*output));
                });
          }
          case Stack::PostTransformationBehavior::kDeleteRegion:
            delete_transformation.initiator =
                transformation::Delete::Initiator::kUser;
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin));
          case Stack::PostTransformationBehavior::kCopyRegion:
            delete_transformation.modifiers.text_delete_behavior =
                Modifiers::TextDeleteBehavior::kKeep;
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin));
          case Stack::PostTransformationBehavior::kCommandSystem: {
            if (input.mode == Input::Mode::kPreview) {
              delete_transformation.preview_modifiers = {
                  LineModifier::kGreen, LineModifier::kUnderline};
              return Apply(delete_transformation, input.NewChild(range.begin));
            }
            NonNull<std::unique_ptr<MutableLineSequence>> contents =
                input.buffer.contents().copy();
            contents->FilterToRange(*delete_transformation.range);
            // TODO(trivial, 2023-09-10): Avoid call to ToString? Add method
            // that returns a LazyString and use it.
            AddLineToHistory(input.buffer.editor(), HistoryFileCommands(),
                             NewLazyString(contents->snapshot().ToString()));
            std::wstring tmp_path = [&contents] {
              char* tmp_path_bytes = strdup("/tmp/edge-commands-XXXXXX");
              // TODO(async, easy, 2023-08-30): Use file_system_driver.
              // TODO(easy, 2023-08-30): Check errors.
              int tmp_fd = mkstemp(tmp_path_bytes);
              std::wstring tmp_path =
                  FromByteString(std::string(tmp_path_bytes));
              free(tmp_path_bytes);
              std::string data = ToByteString(contents->snapshot().ToString());
              write(tmp_fd, data.c_str(), data.size());
              close(tmp_fd);
              return tmp_path;
            }();
            ForkCommand(
                input.buffer.editor(),
                ForkCommandOptions{
                    .command = copy->shell.has_value()
                                   ? copy->shell->read() + L" $EDGE_INPUT"
                                   : input.buffer.Read(
                                         buffer_variables::shell_command),
                    .environment = {{L"EDGE_INPUT", tmp_path},
                                    {L"EDGE_PARENT_BUFFER_PATH",
                                     input.buffer.Read(
                                         buffer_variables::path)}},
                    .existing_buffer_behavior =
                        ForkCommandOptions::ExistingBufferBehavior::kIgnore});
            return futures::Past(std::move(*output));
          }
          case Stack::PostTransformationBehavior::kCommandCpp:
            return HandleCommandCpp(std::move(input), delete_transformation);
          case Stack::PostTransformationBehavior::kCapitalsSwitch: {
            NonNull<std::shared_ptr<SwitchCaseTransformation>> transformation;
            std::vector<ModifiersAndComposite> transformations;
            if (range.lines() > LineNumberDelta(1))
              transformations.push_back(
                  {.modifiers =
                       {.structure = Structure::kLine,
                        .repetitions =
                            (range.lines() - LineNumberDelta(1)).read(),
                        .boundary_end = Modifiers::LIMIT_NEIGHBOR},
                   .transformation = transformation});
            auto columns = range.lines() <= LineNumberDelta(1)
                               ? range.end.column - range.begin.column
                               : range.end.column.ToDelta();
            if (!columns.IsZero())
              transformations.push_back(
                  {.modifiers = {.repetitions = columns.read()},
                   .transformation = transformation});
            auto final_position = output->position;
            auto sub_input = input.NewChild(range.begin);
            output->position = sub_input.position;
            return ApplyStackDirectly(transformations.begin(),
                                      transformations.end(), sub_input, trace,
                                      output)
                .Transform([output, final_position](EmptyValue) {
                  output->position = final_position;
                  return std::move(*output);
                });
          }
          case Stack::PostTransformationBehavior::kCursorOnEachLine: {
            if (input.mode == Input::Mode::kPreview) {
              return futures::Past(std::move(*output));
            }
            struct Cursors cursors {
              .cursors = {},
              .active = LineColumn(delete_transformation.range->begin.line)
            };
            delete_transformation.range->ForEachLine(
                [&cursors](LineNumber line) {
                  cursors.cursors.insert(LineColumn(line));
                });
            cursors.cursors.insert(
                LineColumn(delete_transformation.range->end.line));
            return ApplyBase(cursors, input.NewChild(range.begin));
          }
        }
        LOG(FATAL) << "Invalid post transformation behavior.";
        return futures::Past(std::move(*output));
      });
}

std::wstring ToStringBase(const Stack& stack) {
  std::wstring output = L"Stack(";
  std::wstring separator;
  for (auto& v : stack.stack) {
    output += separator + ToString(v);
    separator = L", ";
  }
  output += L")";
  return output;
}

void Stack::PushBack(Variant transformation) {
  stack.push_back(std::move(transformation));
}

void Stack::PushFront(Variant transformation) {
  stack.push_front(std::move(transformation));
}
}  // namespace transformation

transformation::Variant ComposeTransformation(transformation::Variant a,
                                              transformation::Variant b) {
  return transformation::Stack{{std::move(a), std::move(b)}};
}
}  // namespace afc::editor
