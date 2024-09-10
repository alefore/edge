#include "src/transformation/stack.h"

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/language/container.h"
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

using ::operator<<;

namespace container = afc::language::container;
namespace gc = afc::language::gc;
using afc::infrastructure::screen::LineModifier;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;

namespace afc::editor {
namespace transformation {
namespace {
void ShowValue(OpenBuffer& buffer, OpenBuffer* delete_buffer,
               const vm::Value& value) {
  if (value.IsVoid()) return;
  std::ostringstream oss;
  oss << value;
  buffer.status().SetInformationText(LineBuilder{
      LazyString{L"Value: "} +
      LazyString{
          FromByteString(oss.str())}}.Build());
  if (delete_buffer != nullptr) {
    std::istringstream iss(oss.str());
    for (std::string line_str; std::getline(iss, line_str);) {
      delete_buffer->AppendToLastLine(
          LineBuilder{LazyString{FromByteString(line_str)}}.Build());
      delete_buffer->AppendRawLine(Line());
    }
  }
}

futures::Value<PossibleError> PreviewCppExpression(
    OpenBuffer& buffer, const LineSequence& expression_str) {
  FUTURES_ASSIGN_OR_RETURN(auto compilation_result,
                           buffer.CompileString(expression_str.ToLazyString()));
  auto [expression, environment] = std::move(compilation_result);
  buffer.status().Reset();
  return expression->purity().writes_external_outputs
             ? futures::Past(Success())
             : buffer.EvaluateExpression(std::move(expression), environment)
                   .Transform([&buffer](gc::Root<vm::Value> value) {
                     ShowValue(buffer, nullptr, value.ptr().value());
                     return Success();
                   })
                   .ConsumeErrors([&buffer](Error error) {
                     buffer.status().SetInformationText(
                         LineBuilder{LazyString{L"E: "} + error.read()}
                             .Build());
                     return futures::Past(EmptyValue());
                   })
                   .Transform(
                       [](EmptyValue) { return futures::Past(Success()); });
}

futures::Value<Result> HandleCommandCpp(Input input,
                                        Delete original_delete_transformation) {
  LineSequence contents =
      input.adapter.contents().ViewRange(*original_delete_transformation.range);
  if (input.mode == Input::Mode::kPreview) {
    auto delete_transformation =
        std::make_shared<Delete>(std::move(original_delete_transformation));
    delete_transformation->preview_modifiers = {LineModifier::kGreen,
                                                LineModifier::kUnderline};
    return PreviewCppExpression(input.buffer, contents)
        .ConsumeErrors([&input, delete_transformation](Error error) {
          delete_transformation->preview_modifiers = {LineModifier::kRed,
                                                      LineModifier::kUnderline};
          input.adapter.AddError(error);
          return futures::Past(EmptyValue());
        })
        .Transform([delete_transformation, input](EmptyValue) {
          return Apply(*delete_transformation,
                       input.NewChild(delete_transformation->range->begin()));
        });
  }
  return input.buffer.EvaluateString(contents.ToLazyString())
      .Transform([input](gc::Root<vm::Value> value) {
        ShowValue(input.buffer,
                  input.delete_buffer.has_value()
                      ? &input.delete_buffer->ptr().value()
                      : nullptr,
                  value.ptr().value());
        Result output(input.position);
        output.added_to_paste_buffer = true;
        return Success(std::move(output));
      })
      .ConsumeErrors([input](Error error) {
        Result output(input.position);
        error = AugmentError(LazyString{L"💣 Runtime error"}, std::move(error));
        input.buffer.status().Set(error);
        if (input.delete_buffer.has_value()) {
          input.delete_buffer->ptr()->AppendToLastLine(
              LineBuilder{error.read()}.Build());
          input.delete_buffer->ptr()->AppendRawLine(Line());
          output.added_to_paste_buffer = true;
        }
        return futures::Past(std::move(output));
      });
}

template <typename Iterator>
futures::Value<EmptyValue> ApplyStackDirectly(
    Iterator begin, Iterator end, Input& input,
    NonNull<std::shared_ptr<Log>> trace,
    NonNull<std::shared_ptr<Result>> output) {
  return futures::ForEach(
             begin, end,
             [output, &input,
              trace](const transformation::Variant& transformation) {
               trace->Append(LazyString{L"Transformation: "} +
                             LazyString{ToString(transformation)});
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
  std::optional<size_t> words = std::nullopt;
  std::optional<size_t> alnums = std::nullopt;
  std::optional<size_t> characters = std::nullopt;

  bool operator==(const ContentStats& other) const {
    return lines == other.lines && words == other.words &&
           alnums == other.alnums && characters == other.characters;
  }
};

LazyString ToString(const ContentStats& stats) {
  LazyString output;
  auto key = [&output](std::wstring s, std::optional<size_t> value) {
    if (value)
      output += LazyString{L" "} + LazyString{s} +
                LazyString{std::to_wstring(*value)};
  };
  key(L"🌳", stats.lines);
  key(L" 🍀", stats.words);
  key(L" 🍄", stats.alnums);
  key(L" 🌰", stats.characters);
  return output;
}

ContentStats AnalyzeContent(const LineSequence& contents,
                            const LineNumberDelta& lines_limit) {
  ContentStats output{.lines = contents.EndLine().read()};
  if (contents.size() <= lines_limit) {
    output.words = 0;
    output.alnums = 0;
    output.characters = 0;
    std::ranges::for_each(contents, [&output](const Line& line) {
      ColumnNumber i;
      output.characters = *output.characters + line.EndColumn().read();
      while (i < line.EndColumn()) {
        while (i < line.EndColumn() && !isalnum(line.get(i))) ++i;
        if (i < line.EndColumn()) ++*output.words;
        while (i < line.EndColumn() && isalnum(line.get(i))) {
          ++i;
          ++*output.alnums;
        }
      }
    });
  }
  VLOG(7) << "AnalyzeContent: Output: " << ToString(output);
  return output;
}

const bool analyze_content_tests_registration = tests::Register(
    L"transformation::Stack::AnalyzeContent",
    {{.name = L"Empty",
      .callback =
          [] {
            CHECK(AnalyzeContent(LineSequence(), LineNumberDelta(10)) ==
                  ContentStats(
                      {.lines = 0, .words = 0, .alnums = 0, .characters = 0}));
          }},
     {.name = L"SingleWord",
      .callback =
          [] {
            CHECK(AnalyzeContent(LineSequence::ForTests({L"foo"}),
                                 LineNumberDelta(10)) ==
                  ContentStats(
                      {.lines = 0, .words = 1, .alnums = 3, .characters = 3}));
          }},
     {.name = L"SingleLine",
      .callback =
          [] {
            CHECK(AnalyzeContent(LineSequence::ForTests({L"foo bar hey alejo"}),
                                 LineNumberDelta(10)) ==
                  ContentStats({.lines = 0,
                                .words = 4,
                                .alnums = 3 + 3 + 3 + 5,
                                .characters = 17}));
          }},
     {.name = L"SpacesSingleLine",
      .callback =
          [] {
            CHECK(AnalyzeContent(LineSequence::ForTests(
                                     {L"   foo    bar   hey   alejo   "}),
                                 LineNumberDelta(10)) ==
                  ContentStats({.lines = 0,
                                .words = 4,
                                .alnums = 3 + 3 + 3 + 5,
                                .characters = 30}));
          }},
     {.name = L"VariousEmptyLines",
      .callback =
          [] {
            CHECK(AnalyzeContent(LineSequence::ForTests(
                                     {L"", L"foo", L"", L"", L"", L"bar"}),
                                 LineNumberDelta(10)) ==
                  ContentStats({.lines = 5,
                                .words = 2,
                                .alnums = 3 + 3,
                                .characters = 3 + 3}));
          }},
     {.name = L"PastLimit", .callback = [] {
        CHECK(AnalyzeContent(
                  LineSequence::ForTests({L"", L"foo", L"", L"", L"", L"bar"}),
                  LineNumberDelta(3)) ==
              ContentStats({.lines = 5,
                            .words = std::nullopt,
                            .alnums = std::nullopt,
                            .characters = std::nullopt}));
      }}});
}  // namespace

futures::Value<Result> ApplyBase(const Stack& parameters, Input input) {
  NonNull<std::shared_ptr<Result>> output =
      MakeNonNullShared<Result>(input.position);
  NonNull<std::shared_ptr<Stack>> copy = MakeNonNullShared<Stack>(parameters);
  NonNull<std::shared_ptr<Log>> trace =
      input.buffer.log().NewChild(LazyString{L"ApplyBase(Stack)"});
  return ApplyStackDirectly(copy->stack.begin(), copy->stack.end(), input,
                            trace, output)
      .Transform([output, input, copy, trace](EmptyValue) {
        Range range{input.adapter.contents().AdjustLineColumn(
                        std::min(input.position, output->position)),
                    input.adapter.contents().AdjustLineColumn(
                        std::max(input.position, output->position))};
        Delete delete_transformation{
            .modifiers = {.direction = input.position < output->position
                                           ? Direction::kForwards
                                           : Direction::kBackwards},
            .range = range,
            .initiator = transformation::Delete::Initiator::kInternal};
        switch (copy->post_transformation_behavior) {
          case Stack::PostTransformationBehavior::kNone: {
            LineSequence contents = input.adapter.contents().ViewRange(range);
            input.buffer.status().Reset();
            DVLOG(5) << "Analyze contents for range: " << range;
            return PreviewCppExpression(input.buffer, contents)
                .ConsumeErrors(
                    [](Error) { return futures::Past(EmptyValue()); })
                .Transform([input, output, contents](EmptyValue) {
                  if (input.mode == Input::Mode::kPreview &&
                      input.buffer.status().text().empty()) {
                    input.buffer.status().SetInformationText(
                        LineBuilder(ToString(AnalyzeContent(
                                        contents,
                                        LineNumberDelta(input.buffer.Read(
                                            buffer_variables::
                                                analyze_content_lines_limit)))))
                            .Build());
                  }
                  return futures::Past(std::move(output.value()));
                });
          }
          case Stack::PostTransformationBehavior::kDeleteRegion:
            delete_transformation.initiator =
                transformation::Delete::Initiator::kUser;
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin()));
          case Stack::PostTransformationBehavior::kCopyRegion:
            delete_transformation.modifiers.text_delete_behavior =
                Modifiers::TextDeleteBehavior::kKeep;
            return Apply(delete_transformation,
                         input.NewChild(delete_transformation.range->begin()));
          case Stack::PostTransformationBehavior::kCommandSystem: {
            if (input.mode == Input::Mode::kPreview) {
              delete_transformation.preview_modifiers = {
                  LineModifier::kGreen, LineModifier::kUnderline};
              return Apply(delete_transformation,
                           input.NewChild(range.begin()));
            }
            LineSequence contents = input.adapter.contents().ViewRange(
                *delete_transformation.range);
            AddLineToHistory(input.buffer.editor(), HistoryFileCommands(),
                             contents.ToLazyString());
            std::wstring tmp_path = [contents] {
              char* tmp_path_bytes = strdup("/tmp/edge-commands-XXXXXX");
              // TODO(async, easy, 2023-08-30): Use file_system_driver.
              // TODO(easy, 2023-08-30): Check errors.
              int tmp_fd = mkstemp(tmp_path_bytes);
              std::wstring tmp_path_output =
                  FromByteString(std::string(tmp_path_bytes));
              free(tmp_path_bytes);
              std::string data = contents.ToLazyString().ToBytes();
              write(tmp_fd, data.c_str(), data.size());
              close(tmp_fd);
              return tmp_path_output;
            }();
            ForkCommand(
                input.buffer.editor(),
                ForkCommandOptions{
                    .command =
                        copy->shell.has_value()
                            ? copy->shell->read() + LazyString{L" $EDGE_INPUT"}
                            : input.buffer.ReadLazyString(
                                  buffer_variables::shell_command),
                    .environment = {{L"EDGE_INPUT", LazyString{tmp_path}},
                                    {L"EDGE_PARENT_BUFFER_PATH",
                                     input.buffer.ReadLazyString(
                                         buffer_variables::path)}},
                    .existing_buffer_behavior =
                        ForkCommandOptions::ExistingBufferBehavior::kIgnore});
            return futures::Past(std::move(output.value()));
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
                               ? range.end().column - range.begin().column
                               : range.end().column.ToDelta();
            if (!columns.IsZero())
              transformations.push_back(
                  {.modifiers = {.repetitions = columns.read()},
                   .transformation = transformation});
            auto final_position = output->position;
            auto sub_input = input.NewChild(range.begin());
            output->position = sub_input.position;
            return ApplyStackDirectly(transformations.begin(),
                                      transformations.end(), sub_input, trace,
                                      output)
                .Transform([output, final_position](EmptyValue) {
                  output->position = final_position;
                  return std::move(output.value());
                });
          }
          case Stack::PostTransformationBehavior::kCursorOnEachLine: {
            if (input.mode == Input::Mode::kPreview) {
              return futures::Past(std::move(output.value()));
            }
            struct Cursors cursors {
              .cursors = {},
              .active = LineColumn(delete_transformation.range->begin().line)
            };
            delete_transformation.range->ForEachLine(
                [&cursors](LineNumber line) {
                  cursors.cursors.insert(LineColumn(line));
                });
            return ApplyBase(cursors, input.NewChild(range.begin()));
          }
        }
        LOG(FATAL) << "Invalid post transformation behavior.";
        return futures::Past(std::move(output.value()));
      });
}

std::wstring ToStringBase(const Stack& stack) {
  return (LazyString{L"Stack("} +
          Concatenate(stack.stack | std::views::transform([](auto& v) {
                        return LazyString{ToString(v)};
                      }) |
                      Intersperse(LazyString{L", "})) +
          LazyString{L")"})
      .ToString();
}

void Stack::push_back(Variant transformation) {
  stack.push_back(std::move(transformation));
}

void Stack::push_front(Variant transformation) {
  stack.push_front(std::move(transformation));
}
}  // namespace transformation

transformation::Variant ComposeTransformation(transformation::Variant a,
                                              transformation::Variant b) {
  return transformation::Stack{{std::move(a), std::move(b)}};
}
}  // namespace afc::editor
