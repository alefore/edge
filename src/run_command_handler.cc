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

#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/futures/delete_notification.h"
#include "src/infrastructure/file_descriptor_reader.h"
#include "src/infrastructure/time.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/token_predictor.h"
#include "src/vm/constant_expression.h"
#include "src/vm/environment.h"
#include "src/vm/escape.h"
#include "src/vm/function_call.h"
#include "src/vm/value.h"

using afc::futures::DeleteNotification;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::GetElapsedSecondsSince;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::ProcessId;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::vm::EscapedString;

namespace afc {
using language::NonNull;

namespace gc = language::gc;
namespace editor {
namespace {

using std::cerr;
using std::to_string;
using std::wstring;

struct CommandData {
  time_t time_start = 0;
  time_t time_end = 0;
};

std::map<std::wstring, LazyString> LoadEnvironmentVariables(
    const std::vector<Path>& path, const LazyString& full_command,
    std::map<std::wstring, LazyString> environment) {
  static const std::unordered_set whitespace = {L'\t', L' '};
  std::optional<ColumnNumber> start = FindFirstNotOf(full_command, whitespace);
  if (start == std::nullopt) return environment;
  std::optional<ColumnNumber> end =
      FindFirstOf(full_command.Substring(*start), whitespace);
  if (end == std::nullopt) return environment;
  std::visit(
      overload{IgnoreErrors{},
               [&path, &environment](PathComponent command_component) {
                 auto environment_local_path = Path::Join(
                     PathComponent::FromString(L"commands"),
                     Path::Join(command_component,
                                PathComponent::FromString(L"environment")));
                 for (auto dir : path) {
                   Path full_path = Path::Join(dir, environment_local_path);
                   std::ifstream infile(full_path.read().ToBytes());
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
                         LazyString{FromByteString(line.substr(equals + 1))}));
                   }
                 }
               }},
      PathComponent::New(full_command.Substring(*start, end->ToDelta())));
  return environment;
}

futures::Value<PossibleError> GenerateContents(
    EditorState& editor_state, std::map<std::wstring, LazyString> environment,
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
    target.Set(buffer_variables::pts_path,
               LazyString{FromByteString(pts_path)});
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

  ProcessId child_pid = ProcessId(fork());
  if (child_pid == ProcessId(-1)) {
    Error error{LazyString{L"fork failed: "} +
                LazyString{FromByteString(strerror(errno))}};
    target.status().Set(error);
    return futures::Past(error);
  }
  if (child_pid == ProcessId(0)) {
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

    if (LazyString children_path = target.Read(buffer_variables::children_path);
        !children_path.empty() && chdir(children_path.ToBytes().c_str()) == -1)
      LOG(FATAL) << children_path
                 << ": chdir failed: " << std::string(strerror(errno));

    // Copy variables from the current environment (environ(7)).
    for (size_t index = 0; environ[index] != nullptr; index++) {
      std::wstring entry = FromByteString(environ[index]);
      size_t eq = entry.find_first_of(L"=");
      if (eq == std::wstring::npos) {
        environment.insert({entry, LazyString{}});
      } else {
        environment.insert(
            {entry.substr(0, eq), LazyString{entry.substr(eq + 1)}});
      }
    }
    environment[L"TERM"] = LazyString{L"screen"};
    environment = LoadEnvironmentVariables(
        editor_state.edge_path(), target.Read(buffer_variables::command),
        environment);

    char** envp =
        static_cast<char**>(calloc(environment.size() + 1, sizeof(char*)));
    size_t position = 0;
    for (const std::pair<const std::wstring, LazyString>& entry : environment) {
      std::string str =
          ToByteString(entry.first) + "=" + entry.second.ToBytes();
      CHECK_LT(position, environment.size());
      envp[position++] = strdup(str.c_str());
    }
    envp[position++] = nullptr;
    CHECK_EQ(position, environment.size() + 1);

    char* argv[] = {
        strdup("sh"), strdup("-c"),
        strdup(target.Read(buffer_variables::command).ToBytes().c_str()),
        nullptr};
    int status = execve("/bin/sh", argv, envp);
    exit(WIFEXITED(status) ? WEXITSTATUS(status) : EX_OSERR);
  }
  close(pipefd_out[child_fd]);
  if (pipefd_err[child_fd] != -1) close(pipefd_err[child_fd]);

  LOG(INFO) << "Setting input files: " << pipefd_out[parent_fd] << ", "
            << pipefd_err[parent_fd];
  return target
      .SetInputFiles(OptionalFrom(FileDescriptor::New(pipefd_out[parent_fd])),
                     OptionalFrom(FileDescriptor::New(pipefd_err[parent_fd])),
                     target.Read(buffer_variables::pts), child_pid)
      .Transform([&editor_state, data, &target](EmptyValue) {
        LOG(INFO) << "End of file notification.";
        if (editor_state.buffer_tree().GetBufferIndex(target).has_value()) {
          namespace audio = infrastructure::audio;

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
}

NonEmptySingleLine DurationToString(size_t duration) {
  static const std::vector<std::pair<size_t, NonEmptySingleLine>> time_units = {
      {60, NonEmptySingleLine{SingleLine::Char<L's'>()}},
      {60, NonEmptySingleLine{SingleLine::Char<L'm'>()}},
      {24, NonEmptySingleLine{SingleLine::Char<L'h'>()}},
      {99999999, NonEmptySingleLine{SingleLine::Char<L'd'>()}}};
  size_t factor = 1;
  for (auto& entry : time_units) {
    if (duration < factor * entry.first) {
      return NonEmptySingleLine(duration / factor) + entry.second;
    }
    factor *= entry.first;
  }
  return NonEmptySingleLine{SINGLE_LINE_CONSTANT(L"very-long")};
}

std::map<BufferFlagKey, BufferFlagValue> Flags(const CommandData& data,
                                               const OpenBuffer& buffer) {
  time_t now;
  time(&now);

  std::map<BufferFlagKey, BufferFlagValue> output;
  if (buffer.child_pid().has_value()) {
    output.insert(
        {BufferFlagKey{SINGLE_LINE_CONSTANT(L" …")}, BufferFlagValue{}});
  } else if (buffer.child_exit_status().has_value()) {
    if (!WIFEXITED(buffer.child_exit_status().value())) {
      output.insert(
          {BufferFlagKey{SingleLine::Char<L'💀'>()}, BufferFlagValue{}});
    } else if (WEXITSTATUS(buffer.child_exit_status().value()) == 0) {
      output.insert(
          {BufferFlagKey{SINGLE_LINE_CONSTANT(L" 🏁")}, BufferFlagValue{}});
    } else {
      output.insert(
          {BufferFlagKey{SINGLE_LINE_CONSTANT(L" 💥")}, BufferFlagValue{}});
    }
    if (now > data.time_end)
      output.insert(
          {BufferFlagKey{DurationToString(now - data.time_end).read()},
           BufferFlagValue{}});
  }

  if (now > data.time_start && data.time_start > 0) {
    time_t end =
        (buffer.child_pid().has_value() || data.time_end < data.time_start)
            ? now
            : data.time_end;
    output[BufferFlagKey{SINGLE_LINE_CONSTANT(L"⏲ ")}] =
        BufferFlagValue{DurationToString(end - data.time_start).read()};
  }

  auto update = buffer.last_progress_update();
  if (buffer.child_pid().has_value() && update.tv_sec != 0) {
    auto error_input = buffer.fd_error();
    double lines_read_rate = buffer.lines_read_rate();
    double seconds_since_input =
        buffer.fd() == nullptr
            ? -1
            : GetElapsedSecondsSince(buffer.fd()->last_input_received());
    VLOG(5) << buffer.Read(buffer_variables::name)
            << "Lines read rate: " << lines_read_rate;
    if (lines_read_rate > 5) {
      output[BufferFlagKey{SingleLine::Char<L'🤖'>()}] =
          BufferFlagValue{SingleLine::Char<L'🗫'>()};
    } else if (lines_read_rate > 2) {
      output[BufferFlagKey{SingleLine::Char<L'🤖'>()}] =
          BufferFlagValue{SingleLine::Char<L'🗪'>()};
    } else if (error_input != nullptr &&
               GetElapsedSecondsSince(error_input->last_input_received()) < 5) {
      output[BufferFlagKey{SingleLine::Char<L'🤖'>()}] =
          BufferFlagValue{SingleLine::Char<L'🗯'>()};
    } else if (seconds_since_input > 60 * 2) {
      output[BufferFlagKey{SingleLine::Char<L'🤖'>()}] =
          BufferFlagValue{SingleLine::Char<L'💤'>()};
    } else if (seconds_since_input > 60) {
      output[BufferFlagKey{SingleLine::Char<L'🤖'>()}] =
          BufferFlagValue{SingleLine::Char<L'z'>()};
    } else if (seconds_since_input > 5) {
      output[BufferFlagKey{SingleLine::Char<L'🤖'>()}] = BufferFlagValue{};
    } else if (seconds_since_input >= 0) {
      output[BufferFlagKey{SingleLine::Char<L'🤖'>()}] =
          BufferFlagValue{SingleLine::Char<L'🗩'>()};
    }
    output.insert({BufferFlagKey{DurationToString(now - update.tv_sec).read()},
                   BufferFlagValue{}});
  }
  return output;
}

void RunCommand(const CommandBufferName& name,
                std::map<std::wstring, LazyString> environment,
                EditorState& editor_state, std::optional<Path> children_path,
                LazyString input) {
  auto buffer = editor_state.current_buffer();
  if (input.size().IsZero()) {
    if (buffer.has_value()) {
      buffer->ptr()->ResetMode();
      buffer->ptr()->status().Reset();
    }
    editor_state.status().Reset();
    return;
  }

  ForkCommand(editor_state,
              ForkCommandOptions{
                  .command = input,
                  .name = name,
                  .environment = std::move(environment),
                  .insertion_type =
                      buffer.has_value() &&
                              buffer->ptr()->Read(
                                  buffer_variables::commands_background_mode)
                          ? BuffersList::AddBufferType::kIgnore
                          : BuffersList::AddBufferType::kVisit,
                  .children_path = children_path,
              });
}

// Input must already be unescaped (e.g., contain `\n` rather than `\\n`).
futures::Value<EmptyValue> RunCommandHandler(EditorState& editor_state,
                                             size_t i, size_t n,
                                             std::optional<Path> children_path,
                                             LazyString input) {
  std::map<std::wstring, LazyString> environment = {
      {L"EDGE_RUN", LazyString{std::to_wstring(i)}},
      {L"EDGE_RUNS", LazyString{std::to_wstring(n)}}};
  LazyString name =
      (children_path.has_value() ? children_path->read() : LazyString{}) +
      LazyString{L"$"};
  if (n > 1) {
    name += Concatenate(environment | std::views::transform([](auto it) {
                          return LazyString{L" "} + LazyString{it.first} +
                                 LazyString{L"="} + it.second;
                        }));
  }
  auto buffer = editor_state.current_buffer();
  if (buffer.has_value()) {
    environment[L"EDGE_SOURCE_BUFFER_PATH"] =
        buffer->ptr()->Read(buffer_variables::path);
  }
  name += LazyString{L" "} +
          EscapedString::FromString(input).EscapedRepresentation().read();
  RunCommand(CommandBufferName{name}, environment, editor_state, children_path,
             input);
  return futures::Past(EmptyValue());
}

ValueOrError<Path> GetChildrenPath(EditorState& editor_state) {
  if (auto buffer = editor_state.current_buffer(); buffer.has_value()) {
    return AugmentError(
        LazyString{L"Getting children path of buffer"},
        Path::New(buffer->ptr()->Read(buffer_variables::children_path)));
  }
  return Error{LazyString{L"Editor doesn't have a current buffer."}};
}

class ForkEditorCommand : public Command {
 private:
  // Holds information about the current state of the prompt.
  struct PromptState {
    const gc::Root<OpenBuffer> original_buffer;
    std::optional<LazyString> base_command;
    std::optional<gc::Root<afc::vm::Value>> context_command_callback;
  };

 public:
  ForkEditorCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{
        L"Prompts for a command and creates a new buffer running it."};
  }
  CommandCategory Category() const override {
    return CommandCategory{LazyString{L"Buffers"}};
  }

  void ProcessInput(ExtendedChar) override {
    gc::Pool& pool = editor_state_.gc_pool();
    if (editor_state_.structure() == Structure::kChar) {
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
                      pool, kEmptyNamespace,
                      vm::Identifier{NonEmptySingleLine{SingleLine{

                          LazyString{L"GetShellPromptContextProgram"}}}},
                      vm::types::Function{
                          .output = vm::Type{vm::types::String{}},
                          .inputs = {vm::types::String{}}})});

      ValueOrError<Path> children_path = GetChildrenPath(editor_state_);
      LineBuilder prompt;
      std::visit(
          overload{IgnoreErrors{},
                   [&prompt](Path path) {
                     prompt.AppendString(
                         LineSequence::BreakLines(path.read()).FoldLines());
                   }},
          children_path);
      prompt.AppendString(SINGLE_LINE_CONSTANT(L"$ "),
                          LineModifierSet{LineModifier::kGreen});
      Prompt(PromptOptions{
          .editor_state = editor_state_,
          .prompt = std::move(prompt).Build(),
          .history_file = HistoryFileCommands(),
          .colorize_options_provider =
              prompt_state->context_command_callback.has_value()
                  ? ([prompt_state](const SingleLine& line,
                                    NonNull<std::unique_ptr<ProgressChannel>>,
                                    DeleteNotification::Value) {
                      // TODO(trivial, 2024-09-13): Remove call to read():
                      return PromptChange(prompt_state.value(), line.read());
                    })
                  : PromptOptions::ColorizeFunction(nullptr),
          .handler =
              [&editor = editor_state_, children_path](SingleLine input) {
                return RunCommandHandler(
                    editor, 0, 1, OptionalFrom(children_path), input.read());
              },
          .predictor = TokenPredictor(FilePredictor)});
    } else if (editor_state_.structure() == Structure::kLine) {
      std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state_.current_buffer();
      if (!buffer.has_value()) {
        return;
      }
      VisitPointer(
          buffer->ptr()->OptionalCurrentLine(),
          [&](const Line& current_line) {
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
                EscapedString::Parse(current_line.contents().read()));
          },
          [] {});
    } else {
      std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state_.current_buffer();
      (buffer.has_value() ? buffer->ptr()->status() : editor_state_.status())
          .InsertError(
              Error{LazyString{L"Oops, that structure is not handled."}});
    }
    editor_state_.ResetStructure();
  }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

 private:
  static futures::Value<ColorizePromptOptions> PromptChange(
      PromptState& prompt_state, const LazyString& line) {
    CHECK(prompt_state.context_command_callback.has_value());
    EditorState& editor = prompt_state.original_buffer.ptr()->editor();
    language::gc::Pool& pool = editor.gc_pool();
    CHECK(editor.status().GetType() == Status::Type::kPrompt);
    NonNull<std::unique_ptr<vm::Expression>> context_command_expression =
        vm::NewFunctionCall(
            vm::NewConstantExpression(*prompt_state.context_command_callback),
            {vm::NewConstantExpression(vm::Value::NewString(pool, line))});
    if (context_command_expression->Types().empty()) {
      prompt_state.base_command = std::nullopt;
      prompt_state.original_buffer.ptr()->status().InsertError(
          Error{LazyString{L"Unable to compile (type mismatch)."}});
      return futures::Past(ColorizePromptOptions{
          .context = ColorizePromptOptions::ContextClear()});
    }
    return prompt_state.original_buffer.ptr()
        ->EvaluateExpression(
            std::move(context_command_expression),
            prompt_state.original_buffer.ptr()->environment().ToRoot())
        .Transform([&prompt_state,
                    &editor](gc::Root<vm::Value> context_command_output)
                       -> ValueOrError<ColorizePromptOptions> {
          LazyString base_command = context_command_output.ptr()->get_string();
          if (prompt_state.base_command == base_command)
            return ColorizePromptOptions{};
          if (base_command.empty()) {
            prompt_state.base_command = std::nullopt;
            return ColorizePromptOptions{
                .context = ColorizePromptOptions::ContextClear()};
          }

          prompt_state.base_command = base_command;
          ForkCommandOptions options;
          options.command = base_command;
          options.name = BufferName{LazyString{L"- preview: "} + base_command};
          options.insertion_type = BuffersList::AddBufferType::kIgnore;
          gc::Root<OpenBuffer> help_buffer_root = ForkCommand(editor, options);
          OpenBuffer& help_buffer = help_buffer_root.ptr().value();
          help_buffer.Set(buffer_variables::follow_end_of_file, false);
          help_buffer.Set(buffer_variables::show_in_buffers_list, false);
          help_buffer.Set(buffer_variables::allow_dirty_delete, true);
          help_buffer.set_position({});
          return ColorizePromptOptions{.context =
                                           ColorizePromptOptions::ContextBuffer{
                                               .buffer = help_buffer_root}};
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
    types::ObjectName{
        Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ForkCommandOptions")}};
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

  environment.Define(vm::Identifier{NonEmptySingleLine{
                         SingleLine{LazyString{L"ForkCommandOptions"}}}},
                     NewCallback(pool, vm::kPurityTypePure,
                                 MakeNonNullShared<ForkCommandOptions>));

  fork_command_options.ptr()->AddField(
      vm::Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"set_command"}}}},
      NewCallback(pool, vm::kPurityTypeUnknown,
                  [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
                     LazyString value) { options->command = std::move(value); })
          .ptr());

  fork_command_options.ptr()->AddField(
      vm::Identifier{NonEmptySingleLine{SingleLine{LazyString{L"set_name"}}}},
      NewCallback(pool, vm::kPurityTypeUnknown,
                  [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
                     LazyString value) {
                    options->name = CommandBufferName{std::move(value)};
                  })
          .ptr());

  fork_command_options.ptr()->AddField(
      vm::Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"set_insertion_type"}}}},
      NewCallback(
          pool, vm::kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
             std::wstring value) {
            if (value == L"visit") {
              options->insertion_type = BuffersList::AddBufferType::kVisit;
            } else if (value == L"only_list") {
              options->insertion_type = BuffersList::AddBufferType::kOnlyList;
            } else if (value == L"ignore") {
              options->insertion_type = BuffersList::AddBufferType::kIgnore;
            }
          })
          .ptr());

  fork_command_options.ptr()->AddField(
      vm::Identifier{
          NonEmptySingleLine{SingleLine{LazyString{L"set_children_path"}}}},
      NewCallback(pool, vm::kPurityTypeUnknown,
                  [](NonNull<std::shared_ptr<ForkCommandOptions>> options,
                     LazyString value) {
                    options->children_path =
                        OptionalFrom(Path::New(std::move(value)));
                  })
          .ptr());

  environment.DefineType(fork_command_options.ptr());
}

gc::Root<OpenBuffer> ForkCommand(EditorState& editor_state,
                                 const ForkCommandOptions& options) {
  BufferName name = options.name.value_or(CommandBufferName{options.command});
  if (options.existing_buffer_behavior ==
      ForkCommandOptions::ExistingBufferBehavior::kReuse) {
    if (std::optional<gc::Root<OpenBuffer>> buffer =
            editor_state.buffer_registry().Find(name);
        buffer.has_value()) {
      buffer->ptr()->ResetMode();
      buffer->ptr()->Reload();
      buffer->ptr()->set_current_position_line(LineNumber(0));
      editor_state.AddBuffer(buffer.value(), options.insertion_type);
      return buffer.value();
    }
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
  buffer.ptr()->Set(buffer_variables::children_path,
                    options.children_path.has_value()
                        ? options.children_path->read()
                        : LazyString{});
  buffer.ptr()->Set(buffer_variables::command, options.command);
  buffer.ptr()->Reload();

  editor_state.AddBuffer(buffer, options.insertion_type);
  editor_state.buffer_registry().Add(name, buffer.ptr().ToWeakPtr());
  return buffer;
}

gc::Root<Command> NewForkCommand(EditorState& editor_state) {
  return editor_state.gc_pool().NewRoot(
      MakeNonNullUnique<ForkEditorCommand>(editor_state));
}

futures::Value<EmptyValue> RunCommandHandler(
    EditorState& editor_state, std::map<std::wstring, LazyString> environment,
    LazyString input, SingleLine name_suffix) {
  RunCommand(CommandBufferName{input + name_suffix}, environment, editor_state,
             OptionalFrom(GetChildrenPath(editor_state)), input);
  return futures::Past(EmptyValue());
}

futures::Value<EmptyValue> RunMultipleCommandsHandler(EditorState& editor_state,
                                                      SingleLine input) {
  return editor_state
      .ForEachActiveBuffer([&editor_state, input](OpenBuffer& buffer) {
        std::ranges::for_each(
            buffer.contents().snapshot(),
            [&editor_state, input](const Line& arg) {
              RunCommandHandler(editor_state,
                                std::map<std::wstring, LazyString>{
                                    {L"ARG", arg.contents().read()}},
                                input.read(),
                                SingleLine{LazyString{L" "}} + arg.contents());
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
