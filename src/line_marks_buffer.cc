#include "src/line_marks_buffer.h"

#include "src/command_argument_mode.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::infrastructure::ExtendedChar;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor {
namespace {

LineSequence ShowMarksForBuffer(BufferName name, const LineMarks& marks) {
  MutableLineSequence output;
  output.push_back(L"## Target: " + name.read());
  struct MarkView {
    bool expired;
    LineColumn target;
    NonNull<std::shared_ptr<LazyString>> text;
  };
  std::map<BufferName, std::vector<MarkView>> marks_by_source;
  for (const std::pair<const LineColumn, LineMarks::Mark>& data :
       marks.GetMarksForTargetBuffer(name)) {
    marks_by_source[data.second.source_buffer].push_back(MarkView{
        .expired = false, .target = data.first, .text = NewLazyString(L"...")});
  }
  for (const std::pair<const LineColumn, LineMarks::ExpiredMark>& data :
       marks.GetExpiredMarksForTargetBuffer(name))
    marks_by_source[data.second.source_buffer].push_back(
        MarkView{.expired = true,
                 .target = data.first,
                 .text = data.second.source_line_content});
  for (std::pair<const BufferName, std::vector<MarkView>> data :
       std::move(marks_by_source)) {
    output.push_back(L"");
    output.push_back(L"### Source: " + data.first.read());
    output.append_back(
        std::move(data.second) |
        std::views::transform(
            [](MarkView mark) -> NonNull<std::shared_ptr<Line>> {
              return MakeNonNullShared<Line>(
                  LineBuilder(Append(NewLazyString(L"* "), mark.text)).Build());
            }));
  }
  return output.snapshot();
}

futures::Value<PossibleError> GenerateContents(const EditorState& editor,
                                               OpenBuffer& buffer) {
  LOG(INFO) << "LineMarksBuffer: Generate contents";
  MutableLineSequence output =
      MutableLineSequence::WithLine(MakeNonNullShared<Line>(L"# Marks"));

  output.push_back(L"");

  const LineMarks& marks = editor.line_marks();
  for (const LineSequence& buffer_data :
       marks.GetMarkTargets() |
           std::views::transform([&](const BufferName& name) {
             return ShowMarksForBuffer(name, marks);
           }))
    output.insert(output.EndLine(), std::move(buffer_data), std::nullopt);
  buffer.InsertInPosition(output.snapshot(), buffer.contents().range().end(),
                          std::nullopt);
  return futures::Past(EmptyValue());
}

class Impl : public Command {
  EditorState& editor_;

 public:
  Impl(EditorState& editor) : editor_(editor) {}

  std::wstring Description() const override { return L"Shows Line Marks."; }
  std::wstring Category() const override { return L"Editor"; }

  void ProcessInput(ExtendedChar) override {
    BufferName name(L"Marks");
    gc::Root<OpenBuffer> buffer_root =
        editor_.FindOrBuildBuffer(name, [this, &name] {
          LOG(INFO) << "Building Marks Buffer.";
          gc::Root<OpenBuffer> output = OpenBuffer::New(OpenBuffer::Options{
              .editor = editor_,
              .name = name,
              .generate_contents =
                  std::bind_front(GenerateContents, std::ref(editor_))});
          OpenBuffer& buffer = output.ptr().value();
          buffer.Set(buffer_variables::push_positions_to_history, false);
          buffer.Set(buffer_variables::allow_dirty_delete, true);
          buffer.Set(buffer_variables::reload_on_enter, true);
          buffer.Reload();
          editor_.StartHandlingInterrupts();
          buffer.ResetMode();
          return output;
        });
    LOG(INFO) << "Installing Marks Buffer.";
    editor_.AddBuffer(buffer_root, BuffersList::AddBufferType::kVisit);
    editor_.set_current_buffer(buffer_root,
                               CommandArgumentModeApplyMode::kFinal);
    editor_.status().Reset();
    editor_.PushCurrentPosition();
    editor_.ResetRepetitions();
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }
};
}  // namespace

language::gc::Root<Command> NewLineMarksBufferCommand(
    EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(MakeNonNullUnique<Impl>(editor_state));
}
}  // namespace afc::editor
