#include "src/navigation_buffer.h"

#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/screen/screen.h"
#include "src/insert_mode.h"
#include "src/language/container.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/parse_tree.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::Path;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumberDelta;
using afc::language::text::MutableLineSequence;
using afc::language::text::OutgoingLink;
using afc::math::numbers::Number;

namespace afc::editor {
namespace {

const vm::Identifier kDepthSymbol{
    NonEmptySingleLine{SingleLine{LazyString{L"navigation_buffer_depth"}}}};

// Modifles line_options.contents, appending to it from input.
void AddContents(const OpenBuffer& source, const Line& input,
                 LineBuilder* line_options) {
  LazyString trim = TrimLeft(input.contents().read(),
                             container::MaterializeUnorderedSet(source.Read(
                                 buffer_variables::line_prefix_characters)));
  CHECK_LE(trim.size(), input.contents().size());
  auto characters_trimmed =
      ColumnNumberDelta(input.contents().size() - trim.size());
  auto initial_length = line_options->EndColumn().ToDelta();
  // TODO(trivial, 2024-09-10): Avoid explicit SingleLine wrapping for trim:
  line_options->set_contents(
      SingleLine{line_options->contents()}.Append(SingleLine{trim}));
  for (auto& m : input.modifiers()) {
    if (m.first >= ColumnNumber(0) + characters_trimmed) {
      line_options->set_modifiers(m.first + initial_length - characters_trimmed,
                                  m.second);
    }
  }
}

void AppendLine(OpenBuffer& source, SingleLine padding, LineColumn position,
                OpenBuffer& target) {
  LineBuilder options;
  options.set_contents(padding);
  options.SetOutgoingLink(OutgoingLink{
      .path = Path{ToLazyString(source.name())}, .line_column = position});
  AddContents(source, *source.LineAt(position.line), &options);
  target.AppendRawLine(std::move(options).Build());
}

void DisplayTree(OpenBuffer& source, size_t depth_left, const ParseTree& tree,
                 SingleLine padding, OpenBuffer& target) {
  for (size_t i = 0; i < tree.children().size(); i++) {
    auto& child = tree.children()[i];
    if (child.range().begin().line + LineNumberDelta(1) ==
            child.range().end().line ||
        depth_left == 0 || child.children().empty()) {
      LineBuilder options;
      options.set_contents(SingleLine{padding});
      AddContents(source, *source.LineAt(child.range().begin().line), &options);
      // TODO(trivial, 2024-09-10): Avoid the need to wrap SingleLine here.
      options.set_contents(SingleLine{options.contents()}.Append(
          child.range().begin().line + LineNumberDelta(1) <
                  child.range().end().line
              ? SingleLine{LazyString{L" ... "}}
              : SingleLine{LazyString{L" "}}));
      if (i + 1 >= tree.children().size() ||
          child.range().end().line !=
              tree.children()[i + 1].range().begin().line) {
        AddContents(source, *source.LineAt(child.range().end().line), &options);
      }
      options.SetOutgoingLink(
          OutgoingLink{.path = Path{ToLazyString(source.name())},
                       .line_column = child.range().begin()});

      target.AppendRawLine(std::move(options).Build());
      continue;
    }

    AppendLine(source, padding, child.range().begin(), target);
    if (depth_left > 0) {
      DisplayTree(source, depth_left - 1, child,
                  SingleLine{LazyString{L"  "}} + padding, target);
    }
    if (i + 1 >= tree.children().size() ||
        child.range().end().line !=
            tree.children()[i + 1].range().begin().line) {
      AppendLine(source, padding, child.range().end(), target);
    }
  }
}

futures::Value<PossibleError> GenerateContents(
    EditorState& editor_state, gc::WeakPtr<OpenBuffer> source_weak,
    OpenBuffer& target) {
  for (const auto& dir : editor_state.edge_path()) {
    target.EvaluateFile(Path::Join(
        dir,
        ValueOrDie(Path::New(LazyString{L"hooks/navigation-buffer-reload.cc"}),
                   L"navigation buffer: GenerateContents")));
  }
  std::optional<gc::Root<OpenBuffer>> source = source_weak.Lock();
  if (!source.has_value()) {
    target.AppendToLastLine(
        SINGLE_LINE_CONSTANT(L"Source buffer no longer loaded."));
    return futures::Past(Success());
  }

  auto tree = source->ptr()->simplified_parse_tree();
  target.AppendToLastLine(
      vm::EscapedString(source->ptr()->Read(buffer_variables::name))
          .EscapedRepresentation());
  static const vm::Namespace kEmptyNamespace;
  size_t depth = 3ul;
  if (std::optional<gc::Root<vm::Value>> depth_value =
          target.environment()->Lookup(editor_state.gc_pool(), kEmptyNamespace,
                                       kDepthSymbol, vm::types::Number{});
      depth_value.has_value()) {
    FUTURES_ASSIGN_OR_RETURN(depth, depth_value->ptr()->get_number().ToSizeT());
  }
  DisplayTree(source->ptr().value(), depth, tree.value(), SingleLine{}, target);
  return futures::Past(Success());
}

class NavigationBufferCommand : public Command {
 public:
  NavigationBufferCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{L"displays a navigation view of the current buffer"};
  }
  CommandCategory Category() const override {
    return CommandCategory::kNavigate();
  }

  void ProcessInput(ExtendedChar) override {
    std::optional<gc::Root<OpenBuffer>> source = editor_state_.current_buffer();
    if (!source.has_value()) {
      editor_state_.status().InsertError(
          Error{LazyString{L"NavigationBuffer needs an existing buffer."}});
      return;
    }

    // TODO(trivial, 2024-08-28): Declare a new buffer name?
    BufferName name{LazyString{L"Navigation: "} +
                    ToLazyString(source->ptr()->name())};
    gc::Root<OpenBuffer> buffer_root =
        editor_state_.FindOrBuildBuffer(name, [&] {
          gc::WeakPtr<OpenBuffer> source_weak = source->ptr().ToWeakPtr();
          gc::Root<OpenBuffer> output = OpenBuffer::New(
              {.editor = editor_state_,
               .name = name,
               .generate_contents = std::bind_front(
                   GenerateContents, std::ref(editor_state_), source_weak)});
          OpenBuffer& buffer = output.ptr().value();

          buffer.Set(buffer_variables::show_in_buffers_list, false);
          buffer.Set(buffer_variables::push_positions_to_history, false);
          buffer.Set(buffer_variables::allow_dirty_delete, true);
          buffer.environment()->Define(
              kDepthSymbol, vm::Value::NewNumber(editor_state_.gc_pool(),
                                                 Number::FromInt64(3)));
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

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};
}  // namespace

gc::Root<Command> NewNavigationBufferCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<NavigationBufferCommand>(editor_state));
}

}  // namespace afc::editor
