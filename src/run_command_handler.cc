#include "src/run_command_handler.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>

extern "C" {
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/line_prompt_mode.h"
#include "src/time.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {

using namespace afc::editor;
using std::cerr;
using std::to_string;

struct CommandData {
  time_t time_start = 0;
  time_t time_end = 0;
};

map<wstring, wstring> LoadEnvironmentVariables(
    const vector<Path>& path, const wstring& full_command,
    map<wstring, wstring> environment) {
  static const wstring whitespace = L"\t ";
  size_t start = full_command.find_first_not_of(whitespace);
  if (start == full_command.npos) {
    return environment;
  }
  size_t end = full_command.find_first_of(whitespace, start);
  if (end == full_command.npos || end <= start) {
    return environment;
  }
  wstring command = full_command.substr(start, end - start);
  auto command_component = PathComponent::FromString(command);
  if (command_component.IsError()) return environment;
  auto environment_local_path =
      Path::Join(PathComponent::FromString(L"commands").value(),
                 Path::Join(command_component.value(),
                            PathComponent::FromString(L"environment").value()));
  for (auto dir : path) {
    Path full_path = Path::Join(dir, environment_local_path);
    std::ifstream infile(ToByteString(full_path.ToString()));
    if (!infile.is_open()) {
      continue;
    }
    std::string line;
    while (std::getline(infile, line)) {
      if (line == "") {
        continue;
      }
      size_t equals = line.find('=');
      if (equals == line.npos) {
        continue;
      }
      environment.insert(make_pair(FromByteString(line.substr(0, equals)),
                                   FromByteString(line.substr(equals + 1))));
    }
  }
  return environment;
}

futures::Value<PossibleError> GenerateContents(
    EditorState* editor_state, std::map<wstring, wstring> environment,
    CommandData* data, OpenBuffer* target) {
  int pipefd_out[2];
  int pipefd_err[2];
  static const int parent_fd = 0;
  static const int child_fd = 1;
  time(&data->time_start);
  if (target->Read(buffer_variables::pts)) {
    int master_fd = posix_openpt(O_RDWR);
    if (master_fd == -1) {
      cerr << "posix_openpt failed: " << string(strerror(errno));
      exit(EX_OSERR);
    }
    if (grantpt(master_fd) == -1) {
      cerr << "grantpt failed: " << string(strerror(errno));
      exit(EX_OSERR);
    }
    if (unlockpt(master_fd) == -1) {
      cerr << "unlockpt failed: " << string(strerror(errno));
      exit(EX_OSERR);
    }
    pipefd_out[parent_fd] = master_fd;
    char* pts_path = ptsname(master_fd);
    target->Set(buffer_variables::pts_path, FromByteString(pts_path));
    pipefd_out[child_fd] = open(pts_path, O_RDWR);
    if (pipefd_out[child_fd] == -1) {
      cerr << "open failed: " << pts_path << ": " << string(strerror(errno));
      exit(EX_OSERR);
    }
    pipefd_err[parent_fd] = -1;
    pipefd_err[child_fd] = -1;
  } else if (socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd_out) == -1 ||
             socketpair(PF_LOCAL, SOCK_STREAM, 0, pipefd_err) == -1) {
    LOG(FATAL) << "socketpair failed: " << strerror(errno);
    exit(EX_OSERR);
  }

  pid_t child_pid = fork();
  if (child_pid == -1) {
    auto error = PossibleError(
        Error(L"fork failed: " + FromByteString(strerror(errno))));
    target->status()->SetWarningText(error.error().description);
    return futures::Past(error);
  }
  if (child_pid == 0) {
    LOG(INFO) << "I am the children. Life is beautiful!";

    close(pipefd_out[parent_fd]);
    if (pipefd_err[parent_fd] != -1) close(pipefd_err[parent_fd]);

    if (setsid() == -1) {
      cerr << "setsid failed: " << string(strerror(errno));
      exit(1);
    }

    if (dup2(pipefd_out[child_fd], 0) == -1 ||
        dup2(pipefd_out[child_fd], 1) == -1 ||
        dup2(pipefd_err[child_fd] == -1 ? pipefd_out[child_fd]
                                        : pipefd_err[child_fd],
             2) == -1) {
      LOG(FATAL) << "dup2 failed!";
    }
    if (pipefd_out[child_fd] != -1 && pipefd_out[child_fd] != 0 &&
        pipefd_out[child_fd] != 1 && pipefd_out[child_fd] != 2) {
      close(pipefd_out[child_fd]);
    }
    if (pipefd_err[child_fd] != -1 && pipefd_err[child_fd] != 0 &&
        pipefd_err[child_fd] != 1 && pipefd_err[child_fd] != 2) {
      close(pipefd_err[child_fd]);
    }

    auto children_path = target->Read(buffer_variables::children_path);
    if (!children_path.empty() &&
        chdir(ToByteString(children_path).c_str()) == -1) {
      LOG(FATAL) << children_path
                 << ": chdir failed: " << string(strerror(errno));
    }

    // Copy variables from the current environment (environ(7)).
    for (size_t index = 0; environ[index] != nullptr; index++) {
      wstring entry = FromByteString(environ[index]);
      size_t eq = entry.find_first_of(L"=");
      if (eq == wstring::npos) {
        environment.insert({entry, L""});
      } else {
        environment.insert({entry.substr(0, eq), entry.substr(eq + 1)});
      }
    }
    environment[L"TERM"] = L"screen";
    environment = LoadEnvironmentVariables(
        editor_state->edge_path(), target->Read(buffer_variables::command),
        environment);

    char** envp =
        static_cast<char**>(calloc(environment.size() + 1, sizeof(char*)));
    size_t position = 0;
    for (const auto& it : environment) {
      string str = ToByteString(it.first) + "=" + ToByteString(it.second);
      CHECK_LT(position, environment.size());
      envp[position++] = strdup(str.c_str());
    }
    envp[position++] = nullptr;
    CHECK_EQ(position, environment.size() + 1);

    char* argv[] = {
        strdup("sh"), strdup("-c"),
        strdup(ToByteString(target->Read(buffer_variables::command)).c_str()),
        nullptr};
    int status = execve("/bin/sh", argv, envp);
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : EX_OSERR);
  }
  close(pipefd_out[child_fd]);
  if (pipefd_err[child_fd] != -1) close(pipefd_err[child_fd]);

  LOG(INFO) << "Setting input files: " << pipefd_out[parent_fd] << ", "
            << pipefd_err[parent_fd];
  target->SetInputFiles(pipefd_out[parent_fd], pipefd_err[parent_fd],
                        target->Read(buffer_variables::pts), child_pid);
  target->AddEndOfFileObserver([editor_state, data, target]() {
    LOG(INFO) << "End of file notification.";
    CHECK(target->child_exit_status().has_value());
    int success = WIFEXITED(target->child_exit_status().value()) &&
                  WEXITSTATUS(target->child_exit_status().value()) == 0;
    double frequency =
        target->Read(success ? buffer_variables::beep_frequency_success
                             : buffer_variables::beep_frequency_failure);
    if (frequency > 0.0001) {
      GenerateBeep(editor_state->audio_player(), frequency);
    }
    time(&data->time_end);
  });
  return futures::Past(Success());
}

wstring DurationToString(size_t duration) {
  static const std::vector<std::pair<size_t, wstring>> time_units = {
      {60, L"s"}, {60, L"m"}, {24, L"h"}, {99999999, L"d"}};
  size_t factor = 1;
  for (auto& entry : time_units) {
    if (duration < factor * entry.first) {
      return std::to_wstring(duration / factor) + entry.second;
    }
    factor *= entry.first;
  }
  return L"very-long";
}

std::map<wstring, wstring> Flags(const CommandData& data,
                                 const OpenBuffer& buffer) {
  time_t now;
  time(&now);

  std::map<wstring, wstring> output;
  if (buffer.child_pid() != -1) {
    output.insert({L" …", L""});
  } else if (buffer.child_exit_status().has_value()) {
    if (!WIFEXITED(buffer.child_exit_status().value())) {
      output.insert({L"💀", L""});
    } else if (WEXITSTATUS(buffer.child_exit_status().value()) == 0) {
      output.insert({L" 🏁", L""});
    } else {
      output.insert({L" 💥", L""});
    }
    if (now > data.time_end) {
      output.insert({DurationToString(now - data.time_end), L""});
    }
  }

  if (now > data.time_start && data.time_start > 0) {
    time_t end = (buffer.child_pid() != -1 || data.time_end < data.time_start)
                     ? now
                     : data.time_end;
    output[L"⏲ "] = DurationToString(end - data.time_start);
  }

  auto update = buffer.last_progress_update();
  if (buffer.child_pid() != -1 && update.tv_sec != 0) {
    auto error_input = buffer.fd_error();
    double lines_read_rate =
        (buffer.fd_error() == nullptr ? 0
                                      : buffer.fd_error()->lines_read_rate()) +
        (buffer.fd() == nullptr ? 0 : buffer.fd()->lines_read_rate());
    double seconds_since_input =
        buffer.fd() == nullptr
            ? -1
            : GetElapsedSecondsSince(buffer.fd()->last_input_received());
    VLOG(5) << buffer.Read(buffer_variables::name)
            << "Lines read rate: " << lines_read_rate;
    if (lines_read_rate > 5) {
      output[L"🤖"] = L"🗫";
    } else if (lines_read_rate > 2) {
      output[L"🤖"] = L"🗪";
    } else if (error_input != nullptr &&
               GetElapsedSecondsSince(error_input->last_input_received()) < 5) {
      output[L"🤖"] = L"🗯";
    } else if (seconds_since_input > 60 * 2) {
      output[L"🤖"] = L"💤";
    } else if (seconds_since_input > 60) {
      output[L"🤖"] = L"z";
    } else if (seconds_since_input > 5) {
      output[L"🤖"] = L"";
    } else if (seconds_since_input >= 0) {
      output[L"🤖"] = L"🗩";
    }
    output.insert({DurationToString(now - update.tv_sec), L""});
  }
  return output;
}

void RunCommand(const BufferName& name, const wstring& input,
                map<wstring, wstring> environment, EditorState* editor_state,
                std::optional<Path> children_path) {
  auto buffer = editor_state->current_buffer();
  if (input.empty()) {
    if (buffer != nullptr) {
      buffer->ResetMode();
      buffer->status()->Reset();
    }
    editor_state->status()->Reset();
    return;
  }

  ForkCommandOptions options;
  options.command = input;
  options.name = name;
  options.insertion_type =
      buffer == nullptr ||
              !buffer->Read(buffer_variables::commands_background_mode)
          ? BuffersList::AddBufferType::kVisit
          : BuffersList::AddBufferType::kIgnore;
  options.children_path = children_path;
  options.environment = std::move(environment);
  ForkCommand(editor_state, options);
}

futures::Value<EmptyValue> RunCommandHandler(
    const wstring& input, EditorState* editor_state, size_t i, size_t n,
    std::optional<Path> children_path) {
  map<wstring, wstring> environment = {{L"EDGE_RUN", std::to_wstring(i)},
                                       {L"EDGE_RUNS", std::to_wstring(n)}};
  wstring name =
      (children_path.has_value() ? children_path->ToString() : L"") + L"$";
  if (n > 1) {
    for (auto& it : environment) {
      name += L" " + it.first + L"=" + it.second;
    }
  }
  auto buffer = editor_state->current_buffer();
  if (buffer != nullptr) {
    environment[L"EDGE_SOURCE_BUFFER_PATH"] =
        buffer->Read(buffer_variables::path);
  }
  name += L" " + input;
  RunCommand(BufferName(name), input, environment, editor_state, children_path);
  return futures::Past(EmptyValue());
}

ValueOrError<Path> GetChildrenPath(EditorState* editor_state) {
  if (auto buffer = editor_state->current_buffer(); buffer != nullptr) {
    return AugmentErrors(
        L"Getting children path of buffer",
        Path::FromString(buffer->Read(buffer_variables::children_path)));
  }
  return Error(L"Editor doesn't have a current buffer.");
}

class ForkEditorCommand : public Command {
 private:
  // Holds information about the current state of the prompt.
  struct PromptState {
    const std::shared_ptr<OpenBuffer> original_buffer;
    std::optional<std::wstring> base_command;
    std::unique_ptr<const afc::vm::Value> context_command_callback;
  };

 public:
  wstring Description() const override {
    return L"Prompts for a command and creates a new buffer running it.";
  }
  wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->structure() == StructureChar()) {
      auto original_buffer = editor_state->current_buffer();
      auto prompt_state = std::make_shared<PromptState>(PromptState{
          .original_buffer = original_buffer,
          .base_command = std::nullopt,
          .context_command_callback = original_buffer->environment()->Lookup(
              Environment::Namespace(), L"GetShellPromptContextProgram",
              VMType::Function({VMType::String(), VMType::String()}))});

      PromptOptions options;
      options.editor_state = editor_state;
      auto children_path = GetChildrenPath(editor_state);
      options.prompt =
          (children_path.IsError() ? L"" : children_path.value().ToString()) +
          L"$ ";
      options.history_file = L"commands";
      if (prompt_state->context_command_callback != nullptr) {
        options.colorize_options_provider =
            [prompt_state](const std::shared_ptr<LazyString>& line,
                           std::unique_ptr<ProgressChannel>,
                           std::shared_ptr<Notification>) {
              return PromptChange(prompt_state.get(), line);
            };
      }
      options.handler = [children_path](const wstring& name,
                                        EditorState* editor_state) {
        return RunCommandHandler(name, editor_state, 0, 1,
                                 children_path.AsOptional());
      };
      Prompt(options);
    } else if (editor_state->structure() == StructureLine()) {
      auto buffer = editor_state->current_buffer();
      if (buffer == nullptr || buffer->current_line() == nullptr) {
        return;
      }
      auto children_path = GetChildrenPath(editor_state);
      auto line = buffer->current_line()->ToString();
      for (size_t i = 0; i < editor_state->repetitions().value_or(1); ++i) {
        RunCommandHandler(line, editor_state, i,
                          editor_state->repetitions().value_or(1),
                          children_path.AsOptional());
      }
    } else {
      auto buffer = editor_state->current_buffer();
      (buffer == nullptr ? editor_state->status() : buffer->status())
          ->SetWarningText(L"Oops, that structure is not handled.");
    }
    editor_state->ResetStructure();
  }

 private:
  static futures::Value<ColorizePromptOptions> PromptChange(
      PromptState* prompt_state, const std::shared_ptr<LazyString>& line) {
    CHECK(prompt_state != nullptr);
    CHECK(prompt_state->context_command_callback);
    auto editor = prompt_state->original_buffer->editor();
    CHECK(editor->status()->GetType() == Status::Type::kPrompt);
    std::vector<std::unique_ptr<Expression>> arguments;
    arguments.push_back(
        vm::NewConstantExpression(vm::Value::NewString(line->ToString())));
    std::shared_ptr<Expression> expression = vm::NewFunctionCall(
        vm::NewConstantExpression(
            std::make_unique<Value>(*prompt_state->context_command_callback)),
        std::move(arguments));
    if (expression->Types().empty()) {
      prompt_state->base_command = std::nullopt;
      prompt_state->original_buffer->status()->SetWarningText(
          L"Unable to compile (type mismatch).");
      return futures::Past(ColorizePromptOptions{.context = nullptr});
    }
    return prompt_state->original_buffer
        ->EvaluateExpression(expression.get(),
                             prompt_state->original_buffer->environment())
        .Transform([prompt_state, editor](std::unique_ptr<Value> value) {
          CHECK(value != nullptr);
          CHECK(value->IsString());
          auto base_command = value->str;
          if (prompt_state->base_command == base_command) {
            return Success(ColorizePromptOptions{});
          }

          if (base_command.empty()) {
            prompt_state->base_command = std::nullopt;
            return Success(ColorizePromptOptions{.context = nullptr});
          }

          prompt_state->base_command = base_command;
          ForkCommandOptions options;
          options.command = base_command;
          options.name = BufferName(L"- help: " + base_command);
          options.insertion_type = BuffersList::AddBufferType::kIgnore;
          auto help_buffer = ForkCommand(editor, options);
          help_buffer->Set(buffer_variables::follow_end_of_file, false);
          help_buffer->Set(buffer_variables::show_in_buffers_list, false);
          help_buffer->set_position({});
          return Success(ColorizePromptOptions{.context = help_buffer});
        })
        .ConsumeErrors(
            [](Error) { return futures::Past(ColorizePromptOptions{}); });
  }
};

}  // namespace
}  // namespace editor
namespace vm {
/* static */ editor::ForkCommandOptions*
VMTypeMapper<editor::ForkCommandOptions*>::get(Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"ForkCommandOptions");
  CHECK(value->user_value != nullptr);
  return static_cast<editor::ForkCommandOptions*>(value->user_value.get());
}

/* static */ Value::Ptr VMTypeMapper<editor::ForkCommandOptions*>::New(
    editor::ForkCommandOptions* value) {
  CHECK(value != nullptr);
  return Value::NewObject(L"ForkCommandOptions",
                          std::shared_ptr<void>(value, [](void* v) {
                            delete static_cast<editor::ForkCommandOptions*>(v);
                          }));
}

const VMType VMTypeMapper<editor::ForkCommandOptions*>::vmtype =
    VMType::ObjectType(L"ForkCommandOptions");
}  // namespace vm
namespace editor {
/* static */
void ForkCommandOptions::Register(vm::Environment* environment) {
  using vm::ObjectType;
  using vm::Value;
  using vm::VMType;
  auto fork_command_options =
      std::make_unique<ObjectType>(L"ForkCommandOptions");

  environment->Define(L"ForkCommandOptions",
                      NewCallback(std::function<ForkCommandOptions*()>(
                          []() { return new ForkCommandOptions(); })));

  fork_command_options->AddField(
      L"set_command",
      NewCallback(std::function<void(ForkCommandOptions*, wstring)>(
          [](ForkCommandOptions* options, wstring command) {
            CHECK(options != nullptr);
            options->command = std::move(command);
          })));

  fork_command_options->AddField(
      L"set_name",
      NewCallback(std::function<void(ForkCommandOptions*, wstring)>(
          [](ForkCommandOptions* options, wstring name) {
            CHECK(options != nullptr);
            options->name = BufferName(std::move(name));
          })));

  fork_command_options->AddField(
      L"set_insertion_type",
      NewCallback(std::function<void(ForkCommandOptions*, wstring)>(
          [](ForkCommandOptions* options, wstring insertion_type) {
            CHECK(options != nullptr);
            if (insertion_type == L"visit") {
              options->insertion_type = BuffersList::AddBufferType::kVisit;
            } else if (insertion_type == L"only_list") {
              options->insertion_type = BuffersList::AddBufferType::kOnlyList;
            } else if (insertion_type == L"ignore") {
              options->insertion_type = BuffersList::AddBufferType::kIgnore;
            }
          })));

  fork_command_options->AddField(
      L"set_children_path",
      NewCallback(std::function<void(ForkCommandOptions*, wstring)>(
          [](ForkCommandOptions* options, wstring children_path) {
            CHECK(options != nullptr);
            options->children_path =
                Path::FromString(std::move(children_path)).AsOptional();
          })));

  environment->DefineType(L"ForkCommandOptions",
                          std::move(fork_command_options));
}

std::shared_ptr<OpenBuffer> ForkCommand(EditorState* editor_state,
                                        const ForkCommandOptions& options) {
  CHECK(editor_state != nullptr);
  BufferName name = options.name.value_or(BufferName(L"$ " + options.command));
  auto it = editor_state->buffers()->insert(make_pair(name, nullptr));
  if (it.second) {
    auto command_data = std::make_shared<CommandData>();
    auto buffer = OpenBuffer::New(
        {.editor = *editor_state,
         .name = name,
         .generate_contents =
             [editor_state, environment = options.environment,
              command_data](OpenBuffer* target) {
               return GenerateContents(editor_state, environment,
                                       command_data.get(), target);
             },
         .describe_status =
             [command_data](const OpenBuffer& buffer) {
               return Flags(*command_data, buffer);
             }});
    buffer->Set(buffer_variables::children_path,
                options.children_path.has_value()
                    ? options.children_path->ToString()
                    : L"");
    buffer->Set(buffer_variables::command, options.command);
    it.first->second = std::move(buffer);
  } else {
    it.first->second->ResetMode();
  }

  editor_state->AddBuffer(it.first->second, options.insertion_type);

  it.first->second->Reload();
  it.first->second->set_current_position_line(LineNumber(0));
  return it.first->second;
}

std::unique_ptr<Command> NewForkCommand() {
  return std::make_unique<ForkEditorCommand>();
}

futures::Value<EmptyValue> RunCommandHandler(
    const wstring& input, EditorState* editor_state,
    map<wstring, wstring> environment) {
  RunCommand(BufferName(L"$ " + input), input, environment, editor_state,
             GetChildrenPath(editor_state).AsOptional());
  return futures::Past(EmptyValue());
}

futures::Value<EmptyValue> RunMultipleCommandsHandler(
    const wstring& input, EditorState* editor_state) {
  return editor_state
      ->ForEachActiveBuffer([editor_state,
                             input](const std::shared_ptr<OpenBuffer>& buffer) {
        buffer->contents()->ForEach([editor_state, input](wstring arg) {
          map<wstring, wstring> environment = {{L"ARG", arg}};
          RunCommand(BufferName(L"$ " + input + L" " + arg), input, environment,
                     editor_state, GetChildrenPath(editor_state).AsOptional());
        });
        return futures::Past(EmptyValue());
      })
      .Transform([editor_state](EmptyValue) {
        editor_state->status()->Reset();
        return EmptyValue();
      });
}

}  // namespace editor
}  // namespace afc
