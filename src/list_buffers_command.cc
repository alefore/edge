#include "src/list_buffers_command.h"

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/insert_mode.h"
#include "src/lazy_string_append.h"
#include "src/line_prompt_mode.h"
#include "src/screen.h"
#include "src/screen_vm.h"
#include "src/send_end_of_file_command.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

namespace {

pair<size_t, size_t> LinesToShow(const OpenBuffer& buffer, size_t lines) {
  lines = min(lines, buffer.contents()->size());
  VLOG(5) << buffer.Read(buffer_variables::name())
          << ": Context lines to show: " << lines;
  if (lines == 0) {
    auto last = buffer.lines_size();
    return make_pair(last, last);
  }
  size_t start = min(buffer.current_position_line(), buffer.contents()->size());
  start -= min(
      start,
      max(lines / 2, lines - min(lines, buffer.contents()->size() - start)));
  size_t stop = min(buffer.lines_size(), start + lines);
  CHECK_LE(start, stop);

  // Scroll back if there's a bunch of empty lines.
  while (start > 0 && buffer.LineAt(stop - 1)->empty()) {
    --stop;
    --start;
  }
  CHECK_LE(start, stop);
  return {start, stop};
}

void AdjustLastLine(OpenBuffer* target, std::shared_ptr<OpenBuffer> buffer) {
  target->contents()->back()->environment()->Define(
      L"buffer", Value::NewObject(L"Buffer", buffer));
}

void GenerateContents(EditorState* editor_state, OpenBuffer* target) {
  target->ClearContents(BufferContents::CursorsBehavior::kUnmodified);
  bool show_in_buffers_list =
      target->Read(buffer_variables::show_in_buffers_list());

  size_t screen_lines = 0;
  auto screen_value =
      target->environment()->Lookup(L"screen", GetScreenVmType());
  if (screen_value != nullptr && screen_value->type == VMType::OBJECT_TYPE &&
      screen_value->user_value != nullptr) {
    auto screen = static_cast<Screen*>(screen_value->user_value.get());
    const size_t reserved_lines = 1;  // For the status.
    screen_lines =
        static_cast<size_t>(max(size_t(0), screen->lines() - reserved_lines));
  }

  vector<std::shared_ptr<OpenBuffer>> buffers_to_show;
  for (const auto& it : *editor_state->buffers()) {
    if (!show_in_buffers_list &&
        !it.second->Read(buffer_variables::show_in_buffers_list())) {
      LOG(INFO) << "Skipping buffer (!show_in_buffers_list).";
      continue;
    }
    if (it.second.get() == target) {
      LOG(INFO) << "Skipping current buffer.";
      continue;
    }
    buffers_to_show.push_back(it.second);
  }

  sort(buffers_to_show.begin(), buffers_to_show.end(),
       [](const std::shared_ptr<OpenBuffer>& a,
          const std::shared_ptr<OpenBuffer>& b) {
         return a->last_visit() > b->last_visit();
       });

  // How many context lines should we show for each buffer? Includes one line
  // for the name of the buffer.
  std::unordered_map<OpenBuffer*, size_t> lines_to_show;
  size_t sum_lines_to_show = 0;
  size_t buffers_with_context = 0;
  for (const auto& buffer : buffers_to_show) {
    size_t value =
        1 +
        static_cast<size_t>(max(
            buffer->Read(buffer_variables::buffer_list_context_lines()), 0));
    lines_to_show[buffer.get()] = value;
    sum_lines_to_show += value;
    buffers_with_context += value > 1 ? 1 : 0;
  }
  if (screen_lines > sum_lines_to_show && buffers_with_context) {
    VLOG(4) << "Expanding buffers with context to show the screen. "
            << "buffers_with_context: " << buffers_with_context
            << ", sum_lines_to_show: " << sum_lines_to_show
            << ", screen_lines: " << screen_lines;
    size_t free_lines = screen_lines - sum_lines_to_show;
    size_t lines_per_buffer = free_lines / buffers_with_context;
    size_t extra_lines = free_lines - lines_per_buffer * buffers_with_context;
    for (auto& it : lines_to_show) {
      if (it.second > 1) {
        it.second += free_lines / buffers_with_context;
        if (extra_lines > 0) {
          it.second++;
          extra_lines--;
        }
      }
    }
    CHECK_EQ(extra_lines, size_t(0));
  }
  for (const auto& buffer : buffers_to_show) {
    size_t context_lines_var = lines_to_show[buffer.get()] - 1;
    auto context = LinesToShow(*buffer, context_lines_var);
    std::shared_ptr<LazyString> name =
        NewLazyString(buffer->Read(buffer_variables::name()));
    if (context.first != context.second) {
      name = StringAppend(NewLazyString(L"╭──"), name);
      size_t width = target->Read(buffer_variables::line_width());
      if (width > name->size()) {
        name = StringAppend(
            name,
            NewLazyString(wstring(width - (name->size() + 1), L'─') + L"╮"));
      }
    }
    if (target->contents()->size() == 1 &&
        target->contents()->at(0)->size() == 0) {
      target->AppendToLastLine(std::move(name));
    } else {
      target->AppendLine(std::move(name));
    }
    AdjustLastLine(target, buffer);

    size_t index = 0;
    while (index < context_lines_var) {
      Line::Options options;
      options.contents =
          NewLazyString(index + 1 == context_lines_var ? L"╰ " : L"│ ");
      options.modifiers.resize(options.contents->size());
      if (context.first < context.second) {
        auto line = buffer->LineAt(context.first);
        options.contents = StringAppend(options.contents, line->contents());
        for (const auto& m : line->modifiers()) {
          options.modifiers.push_back(m);
        }
        context.first++;
      }
      CHECK_EQ(options.contents->size(), options.modifiers.size());
      target->AppendRawLine(std::make_shared<Line>(options));
      AdjustLastLine(target, buffer);
      ++index;
    }
  }
  editor_state->ScheduleRedraw();
}

class ListBuffersCommand : public Command {
 public:
  wstring Description() const override { return L"lists all open buffers"; }
  wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    auto it = editor_state->buffers()->insert(
        make_pair(OpenBuffer::kBuffersName, nullptr));
    if (it.second) {
      OpenBuffer::Options options;
      options.editor_state = editor_state;
      options.name = OpenBuffer::kBuffersName;
      options.generate_contents = [editor_state](OpenBuffer* target) {
        GenerateContents(editor_state, target);
      };
      auto buffer = std::make_shared<OpenBuffer>(std::move(options));
      buffer->Set(buffer_variables::reload_on_enter(), true);
      buffer->Set(buffer_variables::atomic_lines(), true);
      buffer->Set(buffer_variables::reload_on_display(), true);
      buffer->Set(buffer_variables::show_in_buffers_list(), false);
      buffer->Set(buffer_variables::push_positions_to_history(), false);
      buffer->Set(buffer_variables::allow_dirty_delete(), true);
      buffer->Set(buffer_variables::wrap_long_lines(), false);
      it.first->second = std::move(buffer);
      editor_state->StartHandlingInterrupts();
    }
    editor_state->set_current_buffer(it.first->second);
    editor_state->ResetStatus();
    it.first->second->Reload();
    editor_state->PushCurrentPosition();
    editor_state->ScheduleRedraw();
    it.first->second->ResetMode();
    editor_state->ResetRepetitions();
  }
};

}  // namespace

std::unique_ptr<Command> NewListBuffersCommand() {
  return std::make_unique<ListBuffersCommand>();
}

}  // namespace editor
}  // namespace afc
