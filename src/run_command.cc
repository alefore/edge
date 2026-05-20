#include "src/run_command.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

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

#include "src/buffer.h"
#include "src/buffer_registry.h"
#include "src/editor.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/execution.h"
#include "src/infrastructure/time_human.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/tests/factory.h"

namespace gc = afc::language::gc;

using namespace afc::infrastructure;
using namespace afc::infrastructure::execution;
using namespace afc::infrastructure::screen;
using namespace afc::language;
using namespace afc::language::lazy_string;
using namespace afc::language::text;

using afc::futures::DeleteNotification;
using afc::vm::EscapedString;

namespace afc::editor {
namespace {
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
  VisitValue(PathComponent::New(full_command.Substring(*start, end->ToDelta())),
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
             });
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
      std::cerr << "posix_openpt failed: " << std::string(strerror(errno));
      exit(EX_OSERR);
    }
    if (grantpt(master_fd) == -1) {
      std::cerr << "grantpt failed: " << std::string(strerror(errno));
      exit(EX_OSERR);
    }
    if (unlockpt(master_fd) == -1) {
      std::cerr << "unlockpt failed: " << std::string(strerror(errno));
      exit(EX_OSERR);
    }
    pipefd_out[parent_fd] = master_fd;
    char* pts_path = ptsname(master_fd);
    target.Set(buffer_variables::pts_path,
               LazyString{FromByteString(pts_path)});
    pipefd_out[child_fd] = open(pts_path, O_RDWR);
    if (pipefd_out[child_fd] == -1) {
      std::cerr << "open failed: " << pts_path << ": "
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
    return error;
  }
  if (child_pid == ProcessId(0)) {
    LOG(INFO) << "I am the children. Life is beautiful!";

    close(pipefd_out[parent_fd]);
    if (pipefd_err[parent_fd] != -1) close(pipefd_err[parent_fd]);

    if (setsid() == -1) {
      std::cerr << "setsid failed: " << std::string(strerror(errno));
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
  target.child_process_tracker().set_pid(child_pid);
  return target
      .SetInputFiles(OptionalFrom(FileDescriptor::New(pipefd_out[parent_fd])),
                     OptionalFrom(FileDescriptor::New(pipefd_err[parent_fd])),
                     target.Read(buffer_variables::pts))
      .Transform([&editor_state, data, &target](EmptyValue) {
        LOG(INFO) << "End of file notification.";
        if (editor_state.buffer_registry()
                .GetListedBufferIndex(target)
                .has_value()) {
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
}  // namespace

gc::Root<OpenBuffer> RunCommand(EditorState& editor_state,
                                const RunCommandOptions& options) {
  BufferName name = options.name.value_or(CommandBufferName{options.command});
  if (options.existing_buffer_behavior ==
      RunCommandOptions::ExistingBufferBehavior::Reuse) {
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

namespace {
TEST_GROUP(RunCommandSurvives,
           [](BuffersList::AddBufferType insertion_type) -> bool {
             NonNull<std::unique_ptr<EditorState>> editor =
                 EditorForTests(std::nullopt);
             std::optional<gc::Root<OpenBuffer>> buffer = RunCommand(
                 editor.value(),
                 RunCommandOptions{.command = L"sleep 60",
                                   .insertion_type = insertion_type});
             gc::WeakPtr<OpenBuffer> buffer_weak = buffer->ptr().ToWeakPtr();
             CHECK(buffer_weak.Lock());
             buffer = std::nullopt;
             size_t iteration = 0;
             ExecutionEnvironment(
                 ExecutionEnvironmentOptions{
                     .stop_check =
                         [&] {
                           const static size_t kRequiredIterations = 10;
                           return iteration > kRequiredIterations ||
                                  !buffer_weak.Lock();
                         },
                     .get_next_alarm =
                         [&] {
                           Time limit = AddSeconds(Now(), 0.1);
                           if (std::optional<Time> work_queue_time =
                                   editor->WorkQueueNextExecution();
                               work_queue_time.has_value())
                             return std::min(limit, work_queue_time.value());
                           return limit;
                         },
                     .on_signals = [] {},
                     .on_iteration =
                         [&](afc::infrastructure::execution::IterationHandler&
                                 handler) {
                           LOG(INFO) << "Iteration: " << iteration;
                           editor->ExecutionIteration(handler);
                           editor->gc_pool().FullCollect();
                           editor->gc_pool().BlockUntilDone();
                           iteration++;
                           CHECK_LT(iteration, 1000ul);
                         }})
                 .Run();
             LOG(INFO) << "About to return: " << buffer_weak.Lock().has_value()
                       << editor->gc_pool();
             return buffer_weak.Lock().has_value();
           })
    .Add(L"Ignore", BuffersList::AddBufferType::Ignore, false)
    .Add(L"Visit", BuffersList::AddBufferType::Visit, true);
}  // namespace

}  // namespace afc::editor
