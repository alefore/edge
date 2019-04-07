#include "src/navigation_buffer.h"

#include "buffer_variables.h"
#include "char_buffer.h"
#include "command.h"
#include "dirname.h"
#include "editor.h"
#include "file_link_mode.h"
#include "insert_mode.h"
#include "lazy_string_append.h"
#include "lazy_string_trim.h"
#include "line_prompt_mode.h"
#include "screen.h"
#include "send_end_of_file_command.h"
#include "wstring.h"

namespace afc {
namespace editor {
namespace {
const wstring kDepthSymbol = L"navigation_buffer_depth";

class NavigationBuffer : public OpenBuffer {
 public:
  NavigationBuffer(EditorState* editor_state, wstring name,
                   std::shared_ptr<OpenBuffer> source)
      : OpenBuffer(editor_state, std::move(name)), source_(source) {
    editor_state->StartHandlingInterrupts();
    Set(buffer_variables::show_in_buffers_list(), false);
    Set(buffer_variables::push_positions_to_history(), false);
    Set(buffer_variables::allow_dirty_delete(), true);
    environment()->Define(kDepthSymbol, Value::NewInteger(3));
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    target->ClearContents(editor_state);
    for (const auto& dir : editor_state->edge_path()) {
      EvaluateFile(editor_state,
                   PathJoin(dir, L"hooks/navigation-buffer-reload.cc"));
    }
    std::shared_ptr<OpenBuffer> source = source_.lock();
    if (source == nullptr) {
      target->AppendToLastLine(
          editor_state, NewCopyString(L"Source buffer no longer loaded."));
      return;
    }

    auto tree = source->simplified_parse_tree();
    if (tree == nullptr) {
      target->AppendToLastLine(editor_state,
                               NewCopyString(L"Target has no tree."));
      return;
    }

    target->AppendToLastLine(editor_state, NewCopyString(source->name()));
    auto depth_value =
        target->environment()->Lookup(kDepthSymbol, VMType::Integer());
    int depth =
        depth_value == nullptr ? 3 : size_t(max(0, depth_value->integer));
    DisplayTree(editor_state, source, depth, *tree, EmptyString(), target);
  }

 private:
  void DisplayTree(EditorState* editor_state,
                   const std::shared_ptr<OpenBuffer>& source, size_t depth_left,
                   const ParseTree& tree, std::shared_ptr<LazyString> padding,
                   OpenBuffer* target) {
    for (size_t i = 0; i < tree.children.size(); i++) {
      auto& child = tree.children[i];
      if (child.range.begin.line + 1 == child.range.end.line ||
          depth_left == 0 || child.children.empty()) {
        Line::Options options;
        options.contents = padding;
        options.modifiers.resize(padding->size());
        AddContents(source, *source->LineAt(child.range.begin.line), &options);
        if (child.range.begin.line + 1 < child.range.end.line) {
          options.contents =
              StringAppend(options.contents, NewCopyString(L" ... "));
        } else {
          options.contents =
              StringAppend(options.contents, NewCopyString(L" "));
        }
        if (i + 1 >= tree.children.size() ||
            child.range.end.line != tree.children[i + 1].range.begin.line) {
          AddContents(source, *source->LineAt(child.range.end.line), &options);
        }
        options.modifiers.resize(options.contents->size());
        target->AppendRawLine(editor_state, std::make_shared<Line>(options));
        AdjustLastLine(target, source, child.range.begin);
        continue;
      }

      AppendLine(editor_state, source, padding, child.range.begin, target);
      if (depth_left > 0) {
        DisplayTree(editor_state, source, depth_left - 1, child,
                    StringAppend(NewCopyString(L"  "), padding), target);
      }
      if (i + 1 >= tree.children.size() ||
          child.range.end.line != tree.children[i + 1].range.begin.line) {
        AppendLine(editor_state, source, padding, child.range.end, target);
      }
    }
  }

  void AppendLine(EditorState* editor_state,
                  const std::shared_ptr<OpenBuffer>& source,
                  std::shared_ptr<LazyString> padding, LineColumn position,
                  OpenBuffer* target) {
    Line::Options options;
    options.contents = padding;
    options.modifiers.resize(padding->size());
    AddContents(source, *source->LineAt(position.line), &options);
    target->AppendRawLine(editor_state, std::make_shared<Line>(options));
    AdjustLastLine(target, source, position);
  }

  // Modifles line_options.contents, appending to it from input.
  void AddContents(const std::shared_ptr<OpenBuffer>& source, const Line& input,
                   Line::Options* line_options) {
    auto trim = StringTrimLeft(
        input.contents(),
        source->Read(buffer_variables::line_prefix_characters()));
    CHECK_LE(trim->size(), input.contents()->size());
    size_t characters_trimmed = input.contents()->size() - trim->size();
    size_t initial_length = line_options->contents->size();
    line_options->contents = StringAppend(line_options->contents, trim);
    line_options->modifiers.resize(line_options->contents->size());
    for (size_t i = 0; i < input.modifiers().size(); i++) {
      if (i >= characters_trimmed) {
        line_options->modifiers[initial_length + i - characters_trimmed] =
            input.modifiers()[i];
      }
    }
  }

  void AdjustLastLine(OpenBuffer* buffer, std::shared_ptr<OpenBuffer> link_to,
                      LineColumn position) {
    auto line_environment = buffer->contents()->back()->environment();
    line_environment->Define(L"buffer", Value::NewObject(L"Buffer", link_to));
    line_environment->Define(
        L"buffer_position",
        Value::NewObject(L"LineColumn",
                         std::make_shared<LineColumn>(position)));
  }

  const std::weak_ptr<OpenBuffer> source_;
};

class NavigationBufferCommand : public Command {
 public:
  wstring Description() const override {
    return L"displays a navigation view of the current buffer";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    if (!editor_state->has_current_buffer()) {
      editor_state->SetWarningStatus(
          L"NavigationBuffer needs an existing buffer.");
      return;
    }
    auto source = editor_state->current_buffer()->second;
    CHECK(source != nullptr);
    if (source->simplified_parse_tree() == nullptr) {
      editor_state->SetStatus(L"Current buffer has no tree.");
      return;
    }

    auto name = L"Navigation: " + source->name();
    auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
    editor_state->set_current_buffer(it.first);
    if (it.second) {
      it.first->second = std::make_shared<NavigationBuffer>(editor_state, name,
                                                            std::move(source));
      it.first->second->Set(buffer_variables::reload_on_enter(), true);
    }
    editor_state->ResetStatus();
    it.first->second->Reload(editor_state);
    editor_state->PushCurrentPosition();
    editor_state->ScheduleRedraw();
    it.first->second->ResetMode();
    editor_state->ResetRepetitions();
  }
};
}  // namespace

std::unique_ptr<Command> NewNavigationBufferCommand() {
  return std::make_unique<NavigationBufferCommand>();
}

}  // namespace editor
}  // namespace afc
