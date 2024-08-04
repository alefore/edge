#include "src/file_link_mode.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

extern "C" {
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <glog/logging.h>

#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/directory_listing.h"
#include "src/editor.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/execution.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/error/view.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/run_command_handler.h"
#include "src/search_handler.h"
#include "src/server.h"
#include "src/tests/tests.h"
#include "src/vm/callbacks.h"
#include "src/vm/value.h"

namespace execution = afc::infrastructure::execution;
namespace gc = afc::language::gc;

using afc::concurrent::ThreadPoolWithWorkQueue;
using afc::concurrent::WorkQueue;
using afc::infrastructure::FileDescriptor;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::ProcessId;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::IfObj;
using afc::language::IgnoreErrors;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::PossibleError;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::VisitOptional;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor {
namespace {

futures::Value<PossibleError> GenerateContents(
    NonNull<std::shared_ptr<struct stat>> stat_buffer,
    NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver,
    OpenBuffer& target) {
  CHECK(target.disk_state() == OpenBuffer::DiskState::kCurrent);
  FUTURES_ASSIGN_OR_RETURN(
      Path path, Path::New(target.ReadLazyString(buffer_variables::path)));
  LOG(INFO) << "GenerateContents: " << path;
  return file_system_driver->Stat(path).Transform(
      [stat_buffer, file_system_driver, &target,
       path](std::optional<struct stat> stat_results) {
        if (!stat_results.has_value()) {
          return futures::Past(Success());
        }
        stat_buffer.value() = stat_results.value();

        if (!S_ISDIR(stat_buffer->st_mode))
          return target.SetInputFromPath(path);
        return GenerateDirectoryListing(path, target).Transform([](EmptyValue) {
          return futures::Past(Success());
        });
      });
}

void HandleVisit(const struct stat& stat_buffer, const OpenBuffer& buffer) {
  const std::wstring path = buffer.Read(buffer_variables::path);
  if (stat_buffer.st_mtime == 0) {
    LOG(INFO) << "Skipping file change check.";
    return;
  }

  LOG(INFO) << "Checking if file has changed: " << path;
  const std::string path_raw = ToByteString(path);
  struct stat current_stat_buffer;
  if (stat(path_raw.c_str(), &current_stat_buffer) == -1) {
    return;
  }
  if (current_stat_buffer.st_mtime <= stat_buffer.st_mtime) {
    return;
  }
  if (S_ISDIR(stat_buffer.st_mode)) {
    buffer.status().SetInformationText(
        Line(L"🌷Directory changed in disk since last read."));
  } else {
    buffer.status().InsertError(
        Error(L"🌷File changed in disk since last read."));
  }
}

futures::Value<PossibleError> Save(
    EditorState*, NonNull<std::shared_ptr<struct stat>> stat_buffer,
    OpenBuffer::Options::HandleSaveOptions options) {
  auto& buffer = options.buffer;
  FUTURES_ASSIGN_OR_RETURN(
      Path immediate_path,
      AugmentError(
          LazyString{L"Buffer can't be saved: Invalid “path” variable"},
          Path::New(buffer.ReadLazyString(buffer_variables::path))));

  futures::ValueOrError<Path> path_future = futures::Past(immediate_path);

  if (S_ISDIR(stat_buffer->st_mode)) {
    return options.save_type == OpenBuffer::Options::SaveType::kBackup
               ? futures::Past(Success())
               : futures::Past(PossibleError(
                     Error(L"Buffer can't be saved: Buffer is a directory.")));
  }

  switch (options.save_type) {
    case OpenBuffer::Options::SaveType::kMainFile:
      break;
    case OpenBuffer::Options::SaveType::kBackup:
      path_future = OnError(buffer.GetEdgeStateDirectory(), [](Error error) {
                      return futures::Past(AugmentError(
                          LazyString{L"Unable to backup buffer"}, error));
                    }).Transform([](Path state_directory) {
        return Success(
            Path::Join(state_directory, PathComponent::FromString(L"backup")));
      });
  }

  return std::move(path_future)
      .Transform([stat_buffer, options, buffer = buffer.NewRoot()](Path path) {
        return SaveContentsToFile(path, buffer.ptr()->contents().snapshot(),
                                  buffer.ptr()->editor().thread_pool(),
                                  buffer.ptr()->file_system_driver())
            .Transform(
                [buffer](EmptyValue) { return buffer.ptr()->PersistState(); })
            .Transform([stat_buffer, options, buffer, path](EmptyValue) {
              switch (options.save_type) {
                case OpenBuffer::Options::SaveType::kMainFile:
                  buffer.ptr()->status().SetInformationText(
                      LineBuilder(LazyString{L"🖫 Saved: "} +
                                  LazyString{path.read()})
                          .Build());
                  // TODO(easy): Move this to the caller, for symmetry with
                  // kBackup case.
                  // TODO: Since the save is async, what if the contents have
                  // changed in the meantime?
                  buffer.ptr()->SetDiskState(OpenBuffer::DiskState::kCurrent);
                  for (const auto& dir : buffer.ptr()->editor().edge_path()) {
                    buffer.ptr()->EvaluateFile(Path::Join(
                        dir, ValueOrDie(Path::New(
                                 LazyString{L"/hooks/buffer-save.cc"}))));
                  }
                  if (buffer.ptr()->Read(
                          buffer_variables::trigger_reload_on_buffer_write)) {
                    std::ranges::for_each(
                        buffer.ptr()->editor().buffer_registry().buffers() |
                            gc::view::Value |
                            std::views::filter([](OpenBuffer& reload_buffer) {
                              return reload_buffer.Read(
                                  buffer_variables::reload_on_buffer_write);
                            }),
                        [&path](OpenBuffer& reload_buffer) {
                          LOG(INFO)
                              << "Write of " << path << " triggers reload: "
                              << reload_buffer.Read(buffer_variables::name);
                          reload_buffer.Reload();
                        });
                  }
                  stat(ToByteString(path.read().ToString()).c_str(),
                       &stat_buffer.value());
                  break;
                case OpenBuffer::Options::SaveType::kBackup:
                  break;
              }
              return futures::Past(Success());
            });
      });
}

futures::Value<PossibleError> SaveContentsToOpenFile(
    ThreadPoolWithWorkQueue& thread_pool, Path original_path, Path path,
    FileDescriptor fd, LineSequence contents) {
  return thread_pool.Run([contents, original_path, path, fd]() {
    LOG(INFO) << original_path
              << ": SaveContentsToOpenFile: writing contents: " << path;
    // TODO: It'd be significant more efficient to do fewer (bigger) writes.
    std::optional<PossibleError> error;
    contents.EveryLine([&](LineNumber position, const Line& line) {
      std::string str = (position == LineNumber(0) ? "" : "\n") +
                        ToByteString(line.ToString());
      if (write(fd.read(), str.c_str(), str.size()) == -1) {
        Error write_error(path.read().ToString() + L": write failed: " +
                          std::to_wstring(fd.read()) + L": " +
                          FromByteString(strerror(errno)));
        LOG(INFO) << original_path
                  << ": SaveContentsToOpenFile: Error: " << write_error;
        error = std::move(write_error);
        return false;
      }
      return true;
    });
    LOG(INFO) << original_path
              << ": SaveContentsToOpenFile: Writing done: " << path;
    return error.value_or(Success());
  });
}
}  // namespace

// Caller must ensure that file_system_driver survives until the future is
// notified.
futures::Value<PossibleError> SaveContentsToFile(
    const Path& path, LineSequence contents,
    ThreadPoolWithWorkQueue& thread_pool,
    FileSystemDriver& file_system_driver) {
  Path tmp_path = Path::Join(
      ValueOrDie(path.Dirname()),
      ValueOrDie(PathComponent::New(ValueOrDie(path.Basename()).read() +
                                    LazyString{L".tmp"})));
  return futures::OnError(
             file_system_driver.Stat(path),
             [](Error error) {
               LOG(INFO)
                   << "Ignoring stat error; maybe a new file is being created: "
                   << error;
               struct stat value;
               value.st_mode =
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
               return futures::Past(Success(value));
             })
      .Transform([path, &file_system_driver, tmp_path](struct stat stat_value) {
        return OnError(
            file_system_driver.Open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC,
                                    stat_value.st_mode),
            [tmp_path](Error error) {
              LOG(INFO) << tmp_path << ": Error opening file: " << error;
              return futures::Past(error);
            });
      })
      .Transform([&thread_pool, path, contents = std::move(contents), tmp_path,
                  &file_system_driver](FileDescriptor fd) {
        CHECK_NE(fd.read(), -1);
        return OnError(SaveContentsToOpenFile(thread_pool, path, tmp_path, fd,
                                              contents),
                       [&file_system_driver, fd](Error error) {
                         file_system_driver.Close(fd);
                         return futures::Past(error);
                       })
            .Transform([&file_system_driver, fd](EmptyValue) {
              return file_system_driver.Close(fd);
            })
            .Transform([path, &file_system_driver, tmp_path](EmptyValue) {
              return file_system_driver.Rename(tmp_path, path);
            });
      });
}

namespace {
futures::Value<gc::Root<OpenBuffer>> GetSearchPathsBuffer(
    EditorState& editor_state, const Path& edge_path) {
  BufferName buffer_name(L"- search paths");
  auto it = editor_state.buffers()->find(buffer_name);
  futures::Value<gc::Root<OpenBuffer>> output =
      it != editor_state.buffers()->end()
          ? futures::Past(it->second)
          : OpenOrCreateFile(
                OpenFileOptions{
                    .editor_state = editor_state,
                    .name = buffer_name,
                    .path = Path::Join(
                        edge_path,
                        ValueOrDie(Path::New(LazyString{L"/search_paths"}))),
                    .insertion_type = BuffersList::AddBufferType::kIgnore,
                    .use_search_paths = false})
                .Transform([&editor_state](gc::Root<OpenBuffer> buffer) {
                  buffer.ptr()->Set(buffer_variables::save_on_close, true);
                  buffer.ptr()->Set(
                      buffer_variables::trigger_reload_on_buffer_write, false);
                  buffer.ptr()->Set(buffer_variables::show_in_buffers_list,
                                    false);
                  if (!editor_state.has_current_buffer()) {
                    editor_state.set_current_buffer(
                        buffer, CommandArgumentModeApplyMode::kFinal);
                  }
                  return buffer;
                });

  return std::move(output).Transform([](gc::Root<OpenBuffer> buffer) {
    return buffer.ptr()->WaitForEndOfFile().Transform(
        [buffer](EmptyValue) { return buffer; });
  });
}
}  // namespace

futures::Value<std::vector<Path>> GetSearchPaths(EditorState& editor_state) {
  auto search_paths = MakeNonNullShared<std::vector<Path>>(
      std::vector<Path>({Path::LocalDirectory()}));

  auto paths = editor_state.edge_path();
  return futures::ForEachWithCopy(
             paths.begin(), paths.end(),
             [&editor_state, search_paths](Path edge_path) {
               return GetSearchPathsBuffer(editor_state, edge_path)
                   .Transform([&editor_state, search_paths,
                               edge_path](gc::Root<OpenBuffer> buffer) {
                     std::ranges::copy(
                         buffer.ptr()->contents().snapshot() |
                             std::views::transform([](const Line& line) {
                               return Path::New(line.contents());
                             }) |
                             language::view::SkipErrors |
                             std::views::transform(
                                 [&editor_state](const Path& path) {
                                   return editor_state.expand_path(path);
                                 }),
                         std::back_inserter(search_paths.value()));
                     return futures::IterationControlCommand::kContinue;
                   });
             })
      .Transform([search_paths](futures::IterationControlCommand) mutable {
        return std::move(search_paths.value());
      });
}

template <typename T>
bool EndsIn(std::list<T> suffix, std::list<T> candidate) {
  if (candidate.size() < suffix.size()) return false;
  auto candidate_it = candidate.begin();
  std::advance(candidate_it, candidate.size() - suffix.size());
  for (const auto& t : suffix) {
    if (*candidate_it != t) return false;
    ++candidate_it;
  }
  return true;
}
const bool ends_in_registration = tests::Register(
    L"EndsIn",
    {
        {.name = L"EmptyBoth", .callback = [] { CHECK(EndsIn<int>({}, {})); }},
        {.name = L"EmptySuffix",
         .callback = [] { CHECK(EndsIn<int>({}, {1, 2, 3})); }},
        {.name = L"EmptyCandidate",
         .callback = [] { CHECK(!EndsIn<int>({1, 2, 3}, {})); }},
        {.name = L"ShortNonEmptyCandidate",
         .callback = [] { CHECK(!EndsIn<int>({1, 2, 3}, {1, 2})); }},
        {.name = L"IdenticalNonEmpty",
         .callback = [] { CHECK(EndsIn<int>({1, 2, 3}, {1, 2, 3})); }},
        {.name = L"EndsInLongerCandidate",
         .callback = [] { CHECK(EndsIn<int>({4, 5, 6}, {1, 2, 3, 4, 5, 6})); }},
        {.name = L"NotEndsInLongerCandidate",
         .callback =
             [] { CHECK(!EndsIn<int>({4, 5, 6}, {1, 2, 3, 4, 0, 6})); }},
    });

futures::ValueOrError<ResolvePathOutput<gc::Root<OpenBuffer>>>
FindAlreadyOpenBuffer(EditorState& editor_state, std::optional<Path> path) {
  ResolvePathOptions<gc::Root<OpenBuffer>> resolve_path_options{
      .home_directory = editor_state.home_directory(),
      .validator = [&editor_state](const Path& path_to_validate)
          -> futures::ValueOrError<gc::Root<OpenBuffer>> {
        return futures::Past(path_to_validate.DirectorySplit())
            .Transform([&](std::list<PathComponent> path_components)
                           -> futures::ValueOrError<gc::Root<OpenBuffer>> {
              for (gc::Root<OpenBuffer> buffer :
                   editor_state.buffer_registry().buffers()) {
                auto buffer_path = Path::New(
                    buffer.ptr()->ReadLazyString(buffer_variables::path));
                if (IsError(buffer_path)) continue;
                ValueOrError<std::list<PathComponent>> buffer_components =
                    std::get<Path>(buffer_path).DirectorySplit();
                if (IsError(buffer_components)) continue;
                if (EndsIn(path_components, std::get<0>(buffer_components))) {
                  return futures::Past(buffer);
                }
              }
              return futures::Past(Error(L"Unable to find buffer"));
            });
      }};
  if (path.has_value()) {
    // TODO(trivial, 2024-08-04): Change `path` to LazyString and remove this
    // conversion.
    resolve_path_options.path =
        editor_state.expand_path(path.value()).read().ToString();
  }

  return ResolvePath(resolve_path_options)
      .Transform([](ResolvePathOutput<gc::Root<OpenBuffer>> input)
                     -> futures::ValueOrError<
                         ResolvePathOutput<gc::Root<OpenBuffer>>> {
        if (input.position.has_value()) {
          input.validator_output.ptr()->set_position(input.position.value());
        }
        // TODO: Apply pattern.
        return futures::Past(Success(std::move(input)));
      });
}

gc::Root<OpenBuffer> CreateBuffer(
    const OpenFileOptions& options,
    std::optional<ResolvePathOutput<EmptyValue>> resolve_path_output) {
  EditorState& editor_state = options.editor_state;
  const std::optional<Path> buffer_options_path =
      std::invoke([&resolve_path_output, &options] -> std::optional<Path> {
        if (resolve_path_output.has_value()) return resolve_path_output->path;
        if (options.path.has_value())
          return options.editor_state.expand_path(options.path.value());
        return std::nullopt;
      });

  auto buffer_options =
      MakeNonNullShared<OpenBuffer::Options>(OpenBuffer::Options{
          .editor = options.editor_state,
          .name = options.name.has_value()
                      ? options.name.value()
                      : std::invoke([&editor_state, &buffer_options_path] {
                          return buffer_options_path.has_value()
                                     ? BufferName{BufferFileId{
                                           buffer_options_path.value()}}
                                     : editor_state.buffer_registry()
                                           .NewAnonymousBufferName();
                        }),
          .path = buffer_options_path,
          .survival_behavior =
              OpenBuffer::Options::SurvivalBehavior::kAllowSilentDeletion});

  NonNull<std::shared_ptr<struct stat>> stat_buffer;
  auto file_system_driver =
      MakeNonNullShared<FileSystemDriver>(editor_state.thread_pool());

  buffer_options->generate_contents = [stat_buffer,
                                       file_system_driver](OpenBuffer& target) {
    return GenerateContents(stat_buffer, file_system_driver, target);
  };
  buffer_options->handle_visit = [stat_buffer](OpenBuffer& buffer) {
    HandleVisit(stat_buffer.value(), buffer);
  };
  if (options.path.has_value())
    buffer_options->handle_save =
        [&editor_state = options.editor_state,
         stat_buffer](OpenBuffer::Options::HandleSaveOptions save_options)
        -> futures::Value<PossibleError> {
      gc::WeakPtr<OpenBuffer> buffer_weak =
          save_options.buffer.NewRoot().ptr().ToWeakPtr();
      return futures::OnError(
          Save(&editor_state, stat_buffer, std::move(save_options)),
          [buffer_weak](Error error) {
            VisitPointer(
                buffer_weak.Lock(),
                [&error](gc::Root<OpenBuffer> buffer) {
                  buffer.ptr()->status().Set(
                      AugmentError(LazyString{L"🖫 Save failed"}, error));
                },
                [] {});
            return futures::Past(error);
          });
    };
  else
    buffer_options->handle_save = [](OpenBuffer::Options::HandleSaveOptions) {
      return futures::Past(Success());
    };
  buffer_options->log_supplier =
      [&editor_state = options.editor_state](Path edge_state_directory) {
        FileSystemDriver driver(editor_state.thread_pool());
        return NewFileLog(driver,
                          Path::Join(edge_state_directory,
                                     PathComponent::FromString(L".edge_log")));
      };

  gc::Root<OpenBuffer> buffer = VisitOptional(
      [&](Path path) {
        return options.editor_state.buffer_registry().MaybeAdd(
            BufferFileId{path}, [&] {
              gc::Root<OpenBuffer> output =
                  OpenBuffer::New(buffer_options.value());
              output.ptr()->Set(buffer_variables::persist_state, true);
              output.ptr()->Reload();
              return output;
            });
      },
      [&] {
        gc::Root<OpenBuffer> output = OpenBuffer::New(buffer_options.value());
        output.ptr()->Set(buffer_variables::persist_state, true);
        output.ptr()->Reload();
        options.editor_state.buffer_registry().Add(output.ptr()->name(),
                                                   output.ptr().ToWeakPtr());
        return output;
      },
      buffer_options->path);
  buffer.ptr()->ResetMode();

  if (resolve_path_output.has_value() &&
      resolve_path_output->position.has_value()) {
    buffer.ptr()->set_position(*resolve_path_output->position);
  }

  options.editor_state.AddBuffer(buffer, options.insertion_type);

  if (resolve_path_output.has_value() &&
      resolve_path_output->pattern.has_value() &&
      !resolve_path_output->pattern->IsEmpty()) {
    SearchOptions search_options =
        SearchOptions{.starting_position = buffer.ptr()->position(),
                      .search_query = resolve_path_output->pattern.value()};
    std::visit(
        overload{[&](LineColumn position) {
                   buffer.ptr()->set_position(position);
                   editor_state.PushCurrentPosition();
                 },
                 [&buffer](Error error) {
                   buffer.ptr()->status().SetInformationText(
                       Line(error.read()));
                 }},
        GetNextMatch(options.editor_state.modifiers().direction, search_options,
                     buffer.ptr()->contents().snapshot()));
  }
  return buffer;
}

futures::ValueOrError<gc::Root<OpenBuffer>> OpenFileIfFound(
    const OpenFileOptions& options) {
  return OnError(
      FindAlreadyOpenBuffer(options.editor_state, options.path)
          .Transform([options](ResolvePathOutput<gc::Root<OpenBuffer>> input) {
            options.editor_state.AddBuffer(input.validator_output,
                                           options.insertion_type);
            return futures::Past(Success(input.validator_output));
          }),
      [options](Error) {
        return (options.use_search_paths ? GetSearchPaths(options.editor_state)
                                         : futures::Past(std::vector<Path>()))
            .Transform([options](std::vector<Path> search_paths)
                           -> futures::ValueOrError<gc::Root<OpenBuffer>> {
              search_paths.insert(search_paths.begin(),
                                  options.initial_search_paths.begin(),
                                  options.initial_search_paths.end());
              return ResolvePath(
                         ResolvePathOptions<EmptyValue>{
                             .path =
                                 options.path.has_value()
                                     ? options.editor_state
                                           .expand_path(options.path.value())
                                           .read()
                                           .ToString()
                                     : L"",
                             .search_paths = std::move(search_paths),
                             .home_directory =
                                 options.editor_state.home_directory(),
                             .validator = std::bind_front(
                                 ResolvePathOptions<EmptyValue>::CanStatPath,
                                 MakeNonNullShared<FileSystemDriver>(
                                     options.editor_state.thread_pool()),
                                 options.stat_validator)})
                  .Transform([options](ResolvePathOutput<EmptyValue> input) {
                    return futures::Past(Success(CreateBuffer(options, input)));
                  });
            });
      });
}

futures::Value<gc::Root<OpenBuffer>> OpenOrCreateFile(
    const OpenFileOptions& options) {
  return OpenFileIfFound(options).ConsumeErrors([options](Error) {
    return futures::Past(CreateBuffer(options, std::nullopt));
  });
}

futures::Value<gc::Root<OpenBuffer>> OpenAnonymousBuffer(
    EditorState& editor_state) {
  return OpenOrCreateFile(
             OpenFileOptions{
                 .editor_state = editor_state,
                 .path = std::nullopt,
                 .insertion_type = BuffersList::AddBufferType::kIgnore,
                 .use_search_paths = false})
      .Transform([](gc::Root<OpenBuffer> buffer_root) {
        // Wait until we've fully evaluated buffer-reload.cc on the buffer.
        return buffer_root.ptr()->WaitForEndOfFile().Transform(
            [buffer_root](EmptyValue) { return buffer_root; });
      });
}

namespace {
using ::operator<<;
using afc::infrastructure::execution::ExecutionEnvironment;
using afc::infrastructure::execution::ExecutionEnvironmentOptions;

class TestDriver {
  NonNull<std::unique_ptr<EditorState>> editor_ = EditorForTests();

  std::vector<LazyString> paths_to_unlink_ = {};

  bool stop_ = false;
  ExecutionEnvironment execution_environment_ =
      ExecutionEnvironment(ExecutionEnvironmentOptions{
          .stop_check = [&]() { return stop_; },
          .get_next_alarm = [&] { return editor_->WorkQueueNextExecution(); },
          .on_signals = [] { LOG(FATAL) << "Unexpected signals received."; },
          .on_iteration =
              [&](execution::IterationHandler& handle) {
                editor_->ExecutionIteration(handle);
              }});

 public:
  ~TestDriver() {
    for (const LazyString& path : paths_to_unlink_)
      CHECK_NE(unlink(ToByteString(path.ToString()).c_str()), -1);
  }

  LazyString NewTmpFile(const LineSequence& contents) {
    char* path = strdup("/tmp/edge-tests-buffersave-simplesave-XXXXXX");
    int tmp_fd = mkstemp(path);
    CHECK(tmp_fd != -1) << path << ": " << strerror(errno);
    LazyString path_str = LazyString{FromByteString(path)};
    paths_to_unlink_.push_back(path_str);
    free(path);

    std::ranges::for_each(
        contents | std::views::transform([](const Line& line) {
          return line.contents();
        }) | Intersperse(LazyString{L"\n"}),
        [tmp_fd](LazyString line) {
          std::string line_str = ToByteString(line.ToString());
          write(tmp_fd, line_str.c_str(), line_str.size());
        });
    close(tmp_fd);

    return path_str;
  }

  futures::Value<ValueOrError<gc::Root<OpenBuffer>>> OpenAndReadPath(
      LazyString path, std::optional<LineSequence> expected_content) {
    return OpenFileIfFound(OpenFileOptions{.editor_state = editor_.value(),
                                           .path = ValueOrDie(Path::New(path)),
                                           .use_search_paths = true})
        .Transform([expected_content](gc::Root<OpenBuffer> buffer) {
          return buffer.ptr()->WaitForEndOfFile().Transform(
              [expected_content, buffer](EmptyValue) {
                buffer.ptr()->contents().ForEach([](std::wstring line) {
                  LOG(INFO) << "Read line: " << line;
                });
                if (expected_content.has_value()) {
                  LOG(INFO)
                      << "Validating, length: " << expected_content->size();
                  CHECK_EQ(buffer.ptr()->lines_size(),
                           expected_content->size());
                  CHECK(buffer.ptr()->contents().snapshot().EveryLine(
                      [expected_content](LineNumber i, const Line& line) {
                        CHECK_EQ(
                            ToByteString(line.contents().ToString()),
                            ToByteString(
                                expected_content->at(i).contents().ToString()));
                        return true;
                      }));
                }
                return futures::Past(Success(buffer));
              });
        });
  }

  PossibleError WaitFor(futures::Value<PossibleError> value) {
    while (!value.has_value()) editor_->ExecutePendingWork();
    return value.Get().value();
  }

  void Stop() {
    CHECK(!stop_);
    stop_ = true;
  }

  void Run() { execution_environment_.Run(); };
};

const bool buffer_positions_tests_registration = tests::Register(
    L"BufferSave",
    {{.name = L"Load", .callback = [] {
        TestDriver driver;
        LineSequence contents =
            LineSequence::ForTests({L"Alejandro", L"Forero"});
        driver.OpenAndReadPath(driver.NewTmpFile(contents), contents)
            .Transform([&](gc::Root<OpenBuffer> buffer) {
              buffer.ptr()->Close();
              driver.Stop();
              return Success();
            });
        driver.Run();
      }}});

}  // namespace

}  // namespace afc::editor
