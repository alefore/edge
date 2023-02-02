#include "src/run_command_handler.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

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
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/file_descriptor_reader.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/time.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/vm/public/constant_expression.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/escape.h"
#include "src/vm/public/function_call.h"
#include "src/vm/public/value.h"

namespace afc {
using language::NonNull;

namespace gc = language::gc;
namespace editor {
namespace {
using futures::DeleteNotification;
using infrastructure::FileDescriptor;
using infrastructure::GetElapsedSecondsSince;
using infrastructure::Path;
using infrastructure::PathComponent;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::IgnoreErrors;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::overload;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;
using language::VisitPointer;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using vm::EscapedString;

using std::cerr;
using std::to_string;
using std::wstring;

struct CommandData {
  time_t time_start = 0;
  time_t time_end = 0;
};

std::map<std::wstring, std::wstring> LoadEnvironmentVariables(
    const std::vector<Path>& path, const std::wstring& full_command,
    std::map<std::wstring, std::wstring> environment) {
  static const std::wstring whitespace = L"\t ";
  size_t start = full_command.find_first_not_of(whitespace);
  if (start == full_command.npos) {
    return environment;
  }
  size_t end = full_command.find_first_of(whitespace, start);
  if (end == full_command.npos || end <= start) {
    return environment;
  }
  std::wstring command = full_command.substr(start, end - start);
  std::visit(overload{IgnoreErrors{},
                      [&](PathComponent command_component) {
                        auto environment_local_path = Path::Join(
                            ValueOrDie(PathComponent::FromString(L"commands")),
                            Path::Join(command_component,
                                       ValueOrDie(PathComponent::FromString(
                                           L"environment"))));
                        for (auto dir : path) {
                          Path full_path =
                              Path::Join(dir, environment_local_path);
                          std::ifstream infile(ToByteString(full_path.read()));
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
                            environment.insert(make_pair(
                                FromByteString(line.substr(0, equals)),
                                FromByteString(line.substr(equals + 1))));
                          }
                        }
                      }},
             PathComponent::FromString(command));
  return environment;
}

futures::Value<PossibleError> GenerateContents(
    EditorState& editor_state, std::map<std::wstring, std::wstring> environment,
    NonNull<std::shared_ptr<CommandData>> data, OpenBuffer& target) {
  int pipefd_out[2];
  int pipefd_err[2];
  static const int parent_fd = 0;
  static const int child_fd = 1;
  time(&data->time_start);
  if (target.Read(buffer_variables::pts)) {
    int master_fd = posix_openpt(O_RDWR);
    if (master_fd == -1) {
      cerr << "posix_openpt failed: " << std::string(strerror(errno));
      exit(EX_OSERR);
    }
    if (grantpt(master_fd) == -1) {
      cerr << "grantpt failed: " << std::string(strerror(errno));
      exit(EX_OSERR);
    }
    if (unlockpt(master_fd) == -1) {
      cerr << "unlockpt failed: " << std::string(strerror(errno));
      exit(EX_OSERR);
    }
    pipefd_out[parent_fd] = master_fd;
    char* pts_path = ptsname(master_fd);
    target.Set(buffer_variables::pts_path, FromByteString(pts_path));
    pipefd_out[child_fd] = open(pts_path, O_RDWR);
    if (pipefd_out[child_fd] == -1) {
      cerr << "open failed: " << pts_path << ": "
           << std::string(strerror(errno));
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
    Error error(L"fork failed: " + FromByteString(strerror(errno)));
    target.status().Set(error);
    return futures::Past(error);
  }
  if (child_pid == 0) {
    LOG(INFO) << "I am the children. Life is beautiful!";

    close(pipefd_out[parent_fd]);
    if (pipefd_err[parent_fd] != -1) close(pipefd_err[parent_fd]);

    if (setsid() == -1) {
      cerr << "setsid failed: " << std::string(strerror(errno));
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

    auto children_path = target.Read(buffer_variables::children_path);
    if (!children_path.empty() &&
        chdir(ToByteString(children_path).c_str()) == -1) {
      LOG(FATAL) << children_path
                 << ": chdir failed: " << std::string(strerror(errno));
    }

    // Copy variables from the current environment (environ(7)).
    for (size_t index = 0; environ[index] != nullptr; index++) {
      std::wstring entry = FromByteString(environ[index]);
      size_t eq = entry.find_first_of(L"=");
      if (eq == std::wstring::npos) {
        environment.insert({entry, L""});
      } else {
        environment.insert({entry.substr(0, eq), entry.substr(eq + 1)});
      }
    }
    environment[L"TERM"] = L"screen";
    environment = LoadEnvironmentVariables(
        editor_state.edge_path(), target.Read(buffer_variables::command),
        environment);

    char** envp =
        static_cast<char**>(calloc(environment.size() + 1, sizeof(char*)));
    size_t position = 0;
    for (const auto& it : environment) {
      std::string str = ToByteString(it.first) + "=" + ToByteString(it.second);
      CHECK_LT(position, environment.size());
      envp[position++] = strdup(str.c_str());
    }
    envp[position++] = nullptr;
    CHECK_EQ(position, environment.size() + 1);

    char* argv[] = {
        strdup("sh"), strdup("-c"),
        strdup(ToByteString(target.Read(buffer_variables::command)).c_str()),
        nullptr};
    int status = execve("/bin/sh", argv, envp);
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : EX_OSERR);
  }
  close(pipefd_out[child_fd]);
  if (pipefd_err[child_fd] != -1) close(pipefd_err[child_fd]);

  LOG(INFO) << "Setting input files: " << pipefd_out[parent_fd] << ", "
            << pipefd_err[parent_fd];
  target.SetInputFiles(FileDescriptor(pipefd_out[parent_fd]),
                       FileDescriptor(pipefd_err[parent_fd]),
                       target.Read(buffer_variables::pts), child_pid);
  target.WaitForEndOfFile().Transform(
      [&editor_state, data, &target](EmptyValue) {
        LOG(INFO) << "End of file notification.";
        if (editor_state.buffer_tree().GetBufferIndex(target).has_value()) {
          CHECK(target.child_exit_status().has_value());
          int success = WIFEXITED(target.child_exit_status().value()) &&
                        WEXITSTATUS(target.child_exit_status().value()) == 0;
          const audio::Frequency frequency(
              target.Read(success ? buffer_variables::beep_frequency_success
                                  : buffer_variables::beep_frequency_failure));
          if (audio::Frequency(0.0001) < frequency) {
            audio::BeepFrequencies(
                editor_state.audio_player(), 0.1,
                std::vector<audio::Frequency>(success ? 1 : 2, frequency));
          }
        }
        time(&data->time_end);
        return Success();
      });
  return futures::Past(Success());
}

wstring DurationToString(size_t duration) {
  static const std::vector<std::pair<size_t, std::wstring>> time_units = {
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

std::map<std::wstring, std::wstring> Flags(const CommandData& data,
                                           const OpenBuffer& buffer) {
  time_t now;
  time(&now);

  std::map<std::wstring, std::wstring> output;
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

void RunCommand(const BufferName& name,
                std::map<std::wstring, std::wstring> environment,
                EditorState& editor_state, std::optional<Path> children_path,
                NonNull<std::shared_ptr<LazyString>> input) {
  auto buffer = editor_state.current_buffer();
  if (input->size().IsZero()) {
    if (buffer.has_value()) {
      buffer->ptr()->ResetMode();
      buffer->ptr()->status().Reset();
    }
    editor_state.status().Reset();
    return;
  }

  ForkCommandOptions options;
  // TODO(easy, 2022-06-05): Avoid call to ToString.
  options.command = input->ToString();
  options.name = name;
  options.insertion_type =
      buffer.has_value() &&
              buffer->ptr()->Read(buffer_variables::commands_background_mode)
          ? BuffersList::AddBufferType::kIgnore
          : BuffersList::AddBufferType::kVisit;
  options.children_path = children_path;
  options.environment = std::move(environment);
  ForkCommand(editor_state, options);
}

futures::Value<EmptyValue> RunCommandHandler(
    EditorState& editor_state, size_t i, size_t n,
    std::optional<Path> children_path,
    NonNull<std::shared_ptr<LazyString>> input) {
  std::map<std::wstring, std::wstring> environment = {
      {L"EDGE_RUN", std::to_wstring(i)}, {L"EDGE_RUNS", std::to_wstring(n)}};
  std::wstring name =
      (children_path.has_value() ? children_path->read() : L"") + L"$";
  if (n > 1) {
    for (auto& it : environment) {
      name += L" " + it.first + L"=" + it.second;
    }
  }
  auto buffer = editor_state.current_buffer();
  if (buffer.has_value()) {
    environment[L"EDGE_SOURCE_BUFFER_PATH"] =
        buffer->ptr()->Read(buffer_variables::path);
  }
  name += L" " + EscapedString::FromString(input).EscapedRepresentation();
  RunCommand(BufferName(name), environment, editor_state, children_path, input);
  return futures::Past(EmptyValue());
}

ValueOrError<Path> GetChildrenPath(EditorState& editor_state) {
  if (auto buffer = editor_state.current_buffer(); buffer.has_value()) {
    return AugmentErrors(
        L"Getting children path of buffer",
        Path::FromString(buffer->ptr()->Read(buffer_variables::children_path)));
  }
  return Error(L"Editor doesn't have a current buffer.");
}

class ForkEditorCommand : public Command {
 private:
  // Holds information about the current state of the prompt.
  struct PromptState {
    const gc::Root<OpenBuffer> original_buffer;
    std::optional<std::wstring> base_command;
    std::optional<gc::Root<afc::vm::Value>> context_command_callback;
  };

 public:
  ForkEditorCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  std::wstring Description() const override {
    return L"Prompts for a command and creates a new buffer running it.";
  }
  std::wstring Category() const override { return L"Buffers"; }

  void ProcessInput(wint_t) override {
    gc::Pool& pool = editor_state_.gc_pool();
    if (editor_state_.structure() == StructureChar()) {
      std::optional<gc::Root<OpenBuffer>> original_buffer =
          editor_state_.current_buffer();
      // TODO(easy, 2022-05-16): Why is this safe?
      CHECK(original_buffer.has_value());
      static const vm::Namespace kEmptyNamespace;
      NonNull<std::shared_ptr<PromptState>> prompt_state =
          MakeNonNullShared<PromptState>(PromptState{
              .original_buffer = *original_buffer,
              .base_command = std::nullopt,
              .context_command_callback =
                  original_buffer->ptr()->environment()->Lookup(
                      pool, kEmptyNamespace, L"GetShellPromptContextProgram",
                      vm::types::Function{
                          .output = vm::Type{vm::types::String{}},
                          .inputs = {vm::types::String{}}})});

      ValueOrError<Path> children_path = GetChildrenPath(editor_state_);
      Prompt(PromptOptions{
          .editor_state = editor_state_,
          .prompt =
              std::visit(overload{[](Error) -> std::wstring { return L""; },
                                  [](Path path) { return path.read(); }},
                         children_path) +
              L"$ ",
          .history_file = HistoryFileCommands(),
          .colorize_options_provider =
              prompt_state->context_command_callback.has_value()
                  ? ([prompt_state](
                         const NonNull<std::shared_ptr<LazyString>>& line,
                         NonNull<std::unique_ptr<ProgressChannel>>,
                         DeleteNotification::Value) {
                      return PromptChange(prompt_state.value(), line);
                    })
                  : PromptOptions::ColorizeFunction(nullptr),
          .handler = std::bind_front(RunCommandHandler, std::ref(editor_state_),
                                     0, 1, OptionalFrom(children_path))});
    } else if (editor_state_.structure() == StructureLine()) {
      std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state_.current_buffer();
      if (!buffer.has_value()) {
        return;
      }
      VisitPointer(
          buffer->ptr()->current_line(),
          [&](NonNull<std::shared_ptr<const Line>> current_line) {
            std::visit(
                overload{
                    [&](EscapedString line) {
                      std::optional<Path> children_path =
                          OptionalFrom(GetChildrenPath(editor_state_));
                      for (size_t i = 0;
                           i < editor_state_.repetitions().value_or(1); ++i) {
                        RunCommandHandler(
                            editor_state_, i,
                            editor_state_.repetitions().value_or(1),
                            children_path, line.OriginalString());
                      }
                    },
                    [&](Error error) { editor_state_.status().Set(error); }},
                EscapedString::Parse(current_line->contents()));
          },
          [] {});
    } else {
      std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state_.current_buffer();
      (buffer.has_value() ? buffer->ptr()->status() : editor_state_.status())
          .SetWarningText(L"Oops, that structure is not handled.");
    }
    editor_state_.ResetStructure();
  }

 private:
  static futures::Value<ColorizePromptOptions> PromptChange(
      PromptState& prompt_state,
      const NonNull<std::shared_ptr<LazyString>>& line) {
    CHECK(prompt_state.context_command_callback.has_value());
    EditorState& editor = prompt_state.original_buffer.ptr()->editor();
    language::gc::Pool& pool = editor.gc_pool();
    CHECK(editor.status().GetType() == Status::Type::kPrompt);
    std::vector<NonNull<std::unique_ptr<vm::Expression>>> arguments;
    arguments.push_back(vm::NewConstantExpression(
        vm::Value::NewString(pool, line->ToString())));
    NonNull<std::unique_ptr<vm::Expression>> expression = vm::NewFunctionCall(
        vm::NewConstantExpression(*prompt_state.context_command_callback),
        std::move(arguments));
    if (expression->Types().empty()) {
      prompt_state.base_command = std::nullopt;
      prompt_state.original_buffer.ptr()->status().SetWarningText(
          L"Unable to compile (type mismatch).");
      return futures::Past(ColorizePromptOptions{.context = std::nullopt});
    }
    return prompt_state.original_buffer.ptr()
        ->EvaluateExpression(
            expression.value(),
            prompt_state.original_buffer.ptr()->environment().ToRoot())
        .Transform([&prompt_state, &editor](gc::Root<vm::Value> value)
                       -> ValueOrError<ColorizePromptOptions> {
          const std::wstring& base_command = value.ptr()->get_string();
          if (prompt_state.base_command == base_command) {
            return ColorizePromptOptions{};
          }

          if (base_command.empty()) {
            prompt_state.base_command = std::nullopt;
            return ColorizePromptOptions{.context = std::nullopt};
          }

          prompt_state.base_command = base_command;
          ForkCommandOptions options;
          options.command = base_command;
          options.name = BufferName(L"- help: " + base_command);
          options.insertion_type = BuffersList::AddBufferType::kIgnore;
          auto help_buffer_root = ForkCommand(editor, options);
          OpenBuffer& help_buffer = help_buffer_root.ptr().value();
          help_buffer.Set(buffer_variables::follow_end_of_file, false);
          help_buffer.Set(buffer_variables::show_in_buffers_list, false);
          help_buffer.set_position({});
          return ColorizePromptOptions{.context = help_buffer_root};
        })
        .ConsumeErrors(
            [](Error) { return futures::Past(ColorizePromptOptions{}); });
  }

  EditorState& editor_state_;
};

}  // namespace
}  // namespace editor
namespace vm {
template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<editor::ForkCommandOptions>>>::object_type_name =
    types::ObjectName(L"ForkCommandOptions");
}  // namespace vm
namespace editor {
/* static */
void ForkCommandOptions::Register(gc::Pool& pool,
                                  vm::Environment& environment) {
  using vm::ObjectType;
  using vm::Value;
  using vm::VMTypeMapper;
  gc::Root<ObjectType> fork_command_options = ObjectType::New(
      pool,
      VMTypeMapper<
          NonNull<std::shared_ptr<ForkCommandOptions>>>::object_type_name);

  environment.Define(L"ForkCommandOptions",
                     NewCallback(pool, vm::PurityType::kPure,
                                 MakeNonNullShared<ForkCommandOptions>));

  fork_command_options.ptr()->AddField(
      L"set_command",
      NewCallback(
          pool, vm::PurityType::kUnknown,
          std::function<void(NonNull<std::shared_ptr<ForkCommandOptions>>,
                             std::wstring)>(
              [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
                 std::wstring command) {
                options->command = std::move(command);
              }))
          .ptr());

  fork_command_options.ptr()->AddField(
      L"set_name",
      NewCallback(
          pool, vm::PurityType::kUnknown,
          std::function<void(NonNull<std::shared_ptr<ForkCommandOptions>>,
                             std::wstring)>(
              [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
                 std::wstring name) {
                options->name = BufferName(std::move(name));
              }))
          .ptr());

  fork_command_options.ptr()->AddField(
      L"set_insertion_type",
      NewCallback(
          pool, vm::PurityType::kUnknown,
          std::function<void(NonNull<std::shared_ptr<ForkCommandOptions>>,
                             std::wstring)>(
              [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
                 std::wstring insertion_type) {
                if (insertion_type == L"visit") {
                  options->insertion_type = BuffersList::AddBufferType::kVisit;
                } else if (insertion_type == L"only_list") {
                  options->insertion_type =
                      BuffersList::AddBufferType::kOnlyList;
                } else if (insertion_type == L"ignore") {
                  options->insertion_type = BuffersList::AddBufferType::kIgnore;
                }
              }))
          .ptr());

  fork_command_options.ptr()->AddField(
      L"set_children_path",
      NewCallback(
          pool, vm::PurityType::kUnknown,
          std::function<void(NonNull<std::shared_ptr<ForkCommandOptions>>,
                             std::wstring)>(
              [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
                 std::wstring children_path) {
                options->children_path =
                    OptionalFrom(Path::FromString(std::move(children_path)));
              }))
          .ptr());

  environment.DefineType(fork_command_options.ptr());
}

gc::Root<OpenBuffer> ForkCommand(EditorState& editor_state,
                                 const ForkCommandOptions& options) {
  BufferName name = options.name.value_or(BufferName(L"$ " + options.command));
  if (auto it = editor_state.buffers()->find(name);
      it != editor_state.buffers()->end()) {
    gc::Root<OpenBuffer> buffer = it->second;
    buffer.ptr()->ResetMode();
    buffer.ptr()->Reload();
    buffer.ptr()->set_current_position_line(LineNumber(0));
    editor_state.AddBuffer(buffer, options.insertion_type);
    return buffer;
  }

  NonNull<std::shared_ptr<CommandData>> command_data;
  gc::Root<OpenBuffer> buffer = OpenBuffer::New(OpenBuffer::Options{
      .editor = editor_state,
      .name = name,
      .generate_contents =
          std::bind_front(GenerateContents, std::ref(editor_state),
                          options.environment, command_data),
      .describe_status = [command_data](const OpenBuffer& buffer_arg) {
        return Flags(command_data.value(), buffer_arg);
      }});
  buffer.ptr()->Set(
      buffer_variables::children_path,
      options.children_path.has_value() ? options.children_path->read() : L"");
  buffer.ptr()->Set(buffer_variables::command, options.command);
  buffer.ptr()->Reload();

  editor_state.buffers()->insert_or_assign(name, buffer);
  editor_state.AddBuffer(buffer, options.insertion_type);
  return buffer;
}

NonNull<std::unique_ptr<Command>> NewForkCommand(EditorState& editor_state) {
  return MakeNonNullUnique<ForkEditorCommand>(editor_state);
}

futures::Value<EmptyValue> RunCommandHandler(
    EditorState& editor_state, std::map<std::wstring, std::wstring> environment,
    NonNull<std::shared_ptr<LazyString>> input) {
  // TODO(easy, 2022-06-05): Avoid call to ToString.
  RunCommand(BufferName(L"$ " + input->ToString()), environment, editor_state,
             OptionalFrom(GetChildrenPath(editor_state)), input);
  return futures::Past(EmptyValue());
}

futures::Value<EmptyValue> RunMultipleCommandsHandler(
    EditorState& editor_state, NonNull<std::shared_ptr<LazyString>> input) {
  return editor_state
      .ForEachActiveBuffer([&editor_state, input](OpenBuffer& buffer) {
        buffer.contents().ForEach([&editor_state, input](wstring arg) {
          std::map<std::wstring, std::wstring> environment = {{L"ARG", arg}};
          RunCommand(BufferName(L"$ " + input->ToString() + L" " + arg),
                     environment, editor_state,
                     OptionalFrom(GetChildrenPath(editor_state)), input);
        });
        return futures::Past(EmptyValue());
      })
      .Transform([&editor_state](EmptyValue) {
        editor_state.status().Reset();
        return EmptyValue();
      });
}

}  // namespace editor
}  // namespace afc
