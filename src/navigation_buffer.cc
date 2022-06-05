#include "src/navigation_buffer.h"

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/insert_mode.h"
#include "src/language/wstring.h"
#include "src/lazy_string_append.h"
#include "src/lazy_string_trim.h"
#include "src/line_prompt_mode.h"
#include "src/parse_tree.h"
#include "src/screen.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
namespace {
using infrastructure::Path;
using language::MakeNonNullShared;
using language::PossibleError;
using language::Success;

namespace gc = language::gc;

const std::wstring kDepthSymbol = L"navigation_buffer_depth";

// Modifles line_options.contents, appending to it from input.
void AddContents(const OpenBuffer& source, const Line& input,
                 Line::Options* line_options) {
  auto trim = StringTrimLeft(
      input.contents(), source.Read(buffer_variables::line_prefix_characters));
  CHECK_LE(trim->size(), input.contents()->size());
  auto characters_trimmed =
      ColumnNumberDelta(input.contents()->size() - trim->size());
  auto initial_length = line_options->EndColumn().ToDelta();
  line_options->contents = StringAppend(line_options->contents, trim);
  for (auto& m : input.modifiers()) {
    if (m.first >= ColumnNumber(0) + characters_trimmed) {
      line_options->modifiers[m.first + initial_length - characters_trimmed] =
          m.second;
    }
  }
}

void AppendLine(OpenBuffer& source,
                NonNull<std::shared_ptr<LazyString>> padding,
                LineColumn position, OpenBuffer& target) {
  Line::Options options;
  options.contents = padding;
  options.SetBufferLineColumn(
      Line::BufferLineColumn{source.NewRoot().ptr().ToWeakPtr(), position});
  AddContents(source, *source.LineAt(position.line), &options);
  target.AppendRawLine(MakeNonNullShared<Line>(options));
}

void DisplayTree(OpenBuffer& source, size_t depth_left, const ParseTree& tree,
                 NonNull<std::shared_ptr<LazyString>> padding,
                 OpenBuffer& target) {
  for (size_t i = 0; i < tree.children().size(); i++) {
    auto& child = tree.children()[i];
    if (child.range().begin.line + LineNumberDelta(1) ==
            child.range().end.line ||
        depth_left == 0 || child.children().empty()) {
      Line::Options options;
      options.contents = padding;
      AddContents(source, *source.LineAt(child.range().begin.line), &options);
      if (child.range().begin.line + LineNumberDelta(1) <
          child.range().end.line) {
        options.contents =
            StringAppend(std::move(options.contents), NewLazyString(L" ... "));
      } else {
        options.contents =
            StringAppend(std::move(options.contents), NewLazyString(L" "));
      }
      if (i + 1 >= tree.children().size() ||
          child.range().end.line != tree.children()[i + 1].range().begin.line) {
        AddContents(source, *source.LineAt(child.range().end.line), &options);
      }
      options.SetBufferLineColumn(Line::BufferLineColumn{
          source.NewRoot().ptr().ToWeakPtr(), child.range().begin});

      target.AppendRawLine(MakeNonNullShared<Line>(options));
      continue;
    }

    AppendLine(source, padding, child.range().begin, target);
    if (depth_left > 0) {
      DisplayTree(source, depth_left - 1, child,
                  StringAppend(NewLazyString(L"  "), padding), target);
    }
    if (i + 1 >= tree.children().size() ||
        child.range().end.line != tree.children()[i + 1].range().begin.line) {
      AppendLine(source, padding, child.range().end, target);
    }
  }
}

futures::Value<PossibleError> GenerateContents(
    EditorState& editor_state, gc::WeakPtr<OpenBuffer> source_weak,
    OpenBuffer& target) {
  target.ClearContents(BufferContents::CursorsBehavior::kUnmodified);
  for (const auto& dir : editor_state.edge_path()) {
    target.EvaluateFile(Path::Join(
        dir, ValueOrDie(Path::FromString(L"hooks/navigation-buffer-reload.cc"),
                        L"navigation buffer: GenerateContents")));
  }
  std::optional<gc::Root<OpenBuffer>> source = source_weak.Lock();
  if (!source.has_value()) {
    target.AppendToLastLine(NewLazyString(L"Source buffer no longer loaded."));
    return futures::Past(Success());
  }

  auto tree = source->ptr()->simplified_parse_tree();
  target.AppendToLastLine(
      NewLazyString(source->ptr()->Read(buffer_variables::name)));
  static const vm::Namespace kEmptyNamespace;
  std::optional<gc::Root<vm::Value>> depth_value = target.environment()->Lookup(
      editor_state.gc_pool(), kEmptyNamespace, kDepthSymbol, vm::VMType::Int());
  int depth = depth_value.has_value()
                  ? size_t(std::max(0, depth_value.value().ptr()->get_int()))
                  : 3;
  DisplayTree(source->ptr().value(), depth, tree.value(), EmptyString(),
              target);
  return futures::Past(Success());
}

class NavigationBufferCommand : public Command {
 public:
  NavigationBufferCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  std::wstring Description() const override {
    return L"displays a navigation view of the current buffer";
  }
  std::wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t) override {
    std::optional<gc::Root<OpenBuffer>> source = editor_state_.current_buffer();
    if (!source.has_value()) {
      editor_state_.status().SetWarningText(
          L"NavigationBuffer needs an existing buffer.");
      return;
    }

    BufferName name(L"Navigation: " + source->ptr()->name().read());
    gc::Root<OpenBuffer> buffer_root =
        editor_state_.FindOrBuildBuffer(name, [&] {
          gc::WeakPtr<OpenBuffer> source_weak = source->ptr().ToWeakPtr();
          gc::Root<OpenBuffer> output = OpenBuffer::New(
              {.editor = editor_state_,
               .name = name,
               .generate_contents = [&editor_state = editor_state_,
                                     source_weak](OpenBuffer& target) {
                 return GenerateContents(editor_state, source_weak, target);
               }});
          OpenBuffer& buffer = output.ptr().value();

          buffer.Set(buffer_variables::show_in_buffers_list, false);
          buffer.Set(buffer_variables::push_positions_to_history, false);
          buffer.Set(buffer_variables::allow_dirty_delete, true);
          buffer.environment()->Define(
              kDepthSymbol, vm::Value::NewInt(editor_state_.gc_pool(), 3));
          buffer.Set(buffer_variables::reload_on_enter, true);
          editor_state_.StartHandlingInterrupts();
          editor_state_.AddBuffer(buffer_root,
                                  BuffersList::AddBufferType::kVisit);
          buffer.ResetMode();
          return output;
        });
    editor_state_.set_current_buffer(buffer_root,
                                     CommandArgumentModeApplyMode::kFinal);
    editor_state_.status().Reset();
    editor_state_.PushCurrentPosition();
    editor_state_.ResetRepetitions();
  }

 private:
  EditorState& editor_state_;
};
}  // namespace

NonNull<std::unique_ptr<Command>> NewNavigationBufferCommand(
    EditorState& editor_state) {
  return MakeNonNullUnique<NavigationBufferCommand>(editor_state);
}

}  // namespace afc::editor
