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

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command_argument_mode.h"
#include "src/directory_listing.h"
#include "src/editor.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/lazy_string_append.h"
#include "src/run_command_handler.h"
#include "src/search_handler.h"
#include "src/server.h"
#include "src/tests/tests.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/value.h"

namespace afc::editor {
using language::NonNull;
using language::VisitPointer;
namespace {
using concurrent::ThreadPool;
using concurrent::WorkQueue;
using infrastructure::FileDescriptor;
using infrastructure::FileSystemDriver;
using infrastructure::Path;
using infrastructure::PathComponent;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::IfObj;
using language::IgnoreErrors;
using language::overload;
using language::PossibleError;
using language::Success;
using language::ToByteString;
using language::ValueOrError;

namespace gc = language::gc;

futures::Value<PossibleError> GenerateContents(
    EditorState& editor_state, std::shared_ptr<struct stat> stat_buffer,
    std::shared_ptr<FileSystemDriver> file_system_driver, OpenBuffer& target) {
  CHECK(target.disk_state() == OpenBuffer::DiskState::kCurrent);
  FUTURES_ASSIGN_OR_RETURN(
      Path path, Path::FromString(target.Read(buffer_variables::path)));
  LOG(INFO) << "GenerateContents: " << path;
  return file_system_driver->Stat(path).Transform(
      [&editor_state, stat_buffer, file_system_driver, &target,
       path](std::optional<struct stat> stat_results) {
        if (stat_results.has_value() &&
            target.Read(buffer_variables::clear_on_reload)) {
          target.ClearContents(BufferContents::CursorsBehavior::kUnmodified);
          target.SetDiskState(OpenBuffer::DiskState::kCurrent);
        }
        if (!stat_results.has_value()) {
          return futures::Past(Success());
        }
        *stat_buffer = stat_results.value();

        if (!S_ISDIR(stat_buffer->st_mode)) {
          return file_system_driver->Open(path, O_RDONLY | O_NONBLOCK, 0)
              .Transform([&target](FileDescriptor fd) {
                target.SetInputFiles(fd, FileDescriptor(-1), false, -1);
                return Success();
              });
        }

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
        L"🌷Directory changed in disk since last read.");
  } else {
    buffer.status().SetWarningText(L"🌷File changed in disk since last read.");
  }
}

futures::Value<PossibleError> Save(
    EditorState*, struct stat* stat_buffer,
    OpenBuffer::Options::HandleSaveOptions options) {
  auto& buffer = options.buffer;
  FUTURES_ASSIGN_OR_RETURN(
      Path immediate_path,
      AugmentErrors(L"Buffer can't be saved: Invalid “path” variable",
                    Path::FromString(buffer.Read(buffer_variables::path))));

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
                      return futures::Past(
                          AugmentError(L"Unable to backup buffer", error));
                    }).Transform([](Path state_directory) {
        return Success(Path::Join(
            state_directory, ValueOrDie(PathComponent::FromString(L"backup"))));
      });
  }

  return path_future.Transform([stat_buffer, options,
                                buffer = buffer.NewRoot()](Path path) {
    return SaveContentsToFile(path, buffer.ptr()->contents().copy(),
                              buffer.ptr()->editor().thread_pool(),
                              buffer.ptr()->file_system_driver())
        .Transform(
            [buffer](EmptyValue) { return buffer.ptr()->PersistState(); })
        .Transform([stat_buffer, options, buffer, path](EmptyValue) {
          switch (options.save_type) {
            case OpenBuffer::Options::SaveType::kMainFile:
              buffer.ptr()->status().SetInformationText(L"🖫 Saved: " +
                                                        path.read());
              // TODO(easy): Move this to the caller, for symmetry with
              // kBackup case.
              // TODO: Since the save is async, what if the contents have
              // changed in the meantime?
              buffer.ptr()->SetDiskState(OpenBuffer::DiskState::kCurrent);
              for (const auto& dir : buffer.ptr()->editor().edge_path()) {
                buffer.ptr()->EvaluateFile(Path::Join(
                    dir,
                    ValueOrDie(Path::FromString(L"/hooks/buffer-save.cc"))));
              }
              if (buffer.ptr()->Read(
                      buffer_variables::trigger_reload_on_buffer_write)) {
                for (std::pair<BufferName, gc::Root<OpenBuffer>> entry :
                     *buffer.ptr()->editor().buffers()) {
                  OpenBuffer& reload_buffer = entry.second.ptr().value();
                  if (reload_buffer.Read(
                          buffer_variables::reload_on_buffer_write)) {
                    LOG(INFO) << "Write of " << path << " triggers reload: "
                              << reload_buffer.Read(buffer_variables::name);
                    reload_buffer.Reload();
                  }
                }
              }
              stat(ToByteString(path.read()).c_str(), stat_buffer);
              break;
            case OpenBuffer::Options::SaveType::kBackup:
              break;
          }
          return futures::Past(Success());
        });
  });
}

static futures::Value<bool> CanStatPath(
    std::shared_ptr<FileSystemDriver> file_system_driver, const Path& path) {
  VLOG(5) << "Considering path: " << path;
  futures::Future<bool> output;
  file_system_driver->Stat(path).SetConsumer(
      VisitCallback(overload{[path, consumer = output.consumer](Error error) {
                               VLOG(6) << path << ": stat failed: " << error;
                               consumer(false);
                             },
                             [path, consumer = output.consumer](struct stat) {
                               VLOG(4) << "Stat succeeded: " << path;
                               consumer(true);
                             }}));
  return std::move(output.value);
}

futures::Value<PossibleError> SaveContentsToOpenFile(
    ThreadPool& thread_pool, Path path, FileDescriptor fd,
    NonNull<std::shared_ptr<const BufferContents>> contents) {
  return thread_pool.Run([contents, path, fd]() {
    // TODO: It'd be significant more efficient to do fewer (bigger)
    // writes.
    std::optional<PossibleError> error;
    contents->EveryLine([&](LineNumber position, const Line& line) {
      std::string str = (position == LineNumber(0) ? "" : "\n") +
                        ToByteString(line.ToString());
      if (write(fd.read(), str.c_str(), str.size()) == -1) {
        error = Error(path.read() + L": write failed: " +
                      std::to_wstring(fd.read()) + L": " +
                      FromByteString(strerror(errno)));
        return false;
      }
      return true;
    });
    return error.value_or(Success());
  });
}
}  // namespace

// Caller must ensure that file_system_driver survives until the future is
// notified.
futures::Value<PossibleError> SaveContentsToFile(
    const Path& path, NonNull<std::unique_ptr<const BufferContents>> contents,
    ThreadPool& thread_pool, FileSystemDriver& file_system_driver) {
  Path tmp_path =
      Path::Join(ValueOrDie(path.Dirname()),
                 ValueOrDie(PathComponent::FromString(
                     ValueOrDie(path.Basename()).ToString() + L".tmp")));
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
        return file_system_driver.Open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC,
                                       stat_value.st_mode);
      })
      .Transform([&thread_pool, path,
                  contents = NonNull<std::shared_ptr<const BufferContents>>(
                      std::move(contents)),
                  tmp_path, &file_system_driver](FileDescriptor fd) {
        CHECK_NE(fd.read(), -1);
        return OnError(
                   SaveContentsToOpenFile(thread_pool, tmp_path, fd, contents),
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
                    .path = Path::Join(edge_path, ValueOrDie(Path::FromString(
                                                      L"/search_paths"))),
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

  return output.Transform([](gc::Root<OpenBuffer> buffer) {
    return buffer.ptr()->WaitForEndOfFile().Transform(
        [buffer](EmptyValue) { return buffer; });
  });
}
}  // namespace

futures::Value<EmptyValue> GetSearchPaths(EditorState& editor_state,
                                          std::vector<Path>* output) {
  output->push_back(Path::LocalDirectory());

  auto paths = editor_state.edge_path();
  return futures::ForEachWithCopy(
             paths.begin(), paths.end(),
             [&editor_state, output](Path edge_path) {
               return GetSearchPathsBuffer(editor_state, edge_path)
                   .Transform([&editor_state, output,
                               edge_path](gc::Root<OpenBuffer> buffer) {
                     buffer.ptr()->contents().ForEach(
                         [&editor_state, output](std::wstring line) {
                           std::visit(
                               overload{[](Error) {},
                                        [&](Path path) {
                                          output->push_back(
                                              editor_state.expand_path(path));
                                          LOG(INFO) << "Pushed search path: "
                                                    << output->back();
                                        }},
                               Path::FromString(line));
                         });
                     return futures::IterationControlCommand::kContinue;
                   });
             })
      .Transform([](futures::IterationControlCommand) { return EmptyValue(); });
}

/* static */
ResolvePathOptions ResolvePathOptions::New(
    EditorState& editor_state,
    std::shared_ptr<FileSystemDriver> file_system_driver) {
  auto output =
      NewWithEmptySearchPaths(editor_state, std::move(file_system_driver));
  GetSearchPaths(editor_state, &output.search_paths);
  return output;
}

/* static */ ResolvePathOptions ResolvePathOptions::NewWithEmptySearchPaths(
    EditorState& editor_state,
    std::shared_ptr<FileSystemDriver> file_system_driver) {
  return ResolvePathOptions(editor_state.home_directory(),
                            [file_system_driver](const Path& path) {
                              return CanStatPath(file_system_driver, path);
                            });
}

ResolvePathOptions::ResolvePathOptions(Path input_home_directory,
                                       Validator input_validator)
    : home_directory(std::move(input_home_directory)),
      validator(std::move(input_validator)) {}

futures::ValueOrError<ResolvePathOutput> ResolvePath(ResolvePathOptions input) {
  if (find(input.search_paths.begin(), input.search_paths.end(),
           Path::LocalDirectory()) == input.search_paths.end()) {
    input.search_paths.push_back(Path::LocalDirectory());
  }

  std::visit(
      overload{
          IgnoreErrors{},
          [&](Path path) {
            input.path =
                Path::ExpandHomeDirectory(input.home_directory, path).read();
          }},
      Path::FromString(input.path));
  if (!input.path.empty() && input.path[0] == L'/') {
    input.search_paths = {Path::Root()};
  }

  NonNull<std::shared_ptr<std::optional<ValueOrError<ResolvePathOutput>>>>
      output;
  using futures::IterationControlCommand;
  using futures::Past;
  return futures::ForEachWithCopy(
             input.search_paths.begin(), input.search_paths.end(),
             [input, output](Path search_path) {
               struct State {
                 const Path search_path;
                 size_t str_end;
               };
               auto state = std::make_shared<State>(
                   State{.search_path = std::move(search_path),
                         .str_end = input.path.size()});
               return futures::While([input, output, state]() {
                        if (state->str_end == input.path.npos ||
                            state->str_end == 0) {
                          return Past(IterationControlCommand::kStop);
                        }

                        auto input_path = Path::FromString(
                            input.path.substr(0, state->str_end));
                        if (IsError(input_path)) {
                          state->str_end =
                              input.path.find_last_of(':', state->str_end - 1);
                          return Past(IterationControlCommand::kContinue);
                        }
                        auto path_with_prefix = Path::Join(
                            state->search_path, ValueOrDie(input_path));
                        return input.validator(path_with_prefix)
                            .Transform([input, output, state,
                                        path_with_prefix](bool validator_output)
                                           -> IterationControlCommand {
                              if (!validator_output) {
                                state->str_end = input.path.find_last_of(
                                    ':', state->str_end - 1);
                                return IterationControlCommand::kContinue;
                              }
                              std::wstring output_pattern = L"";
                              std::optional<LineColumn> output_position;
                              for (size_t i = 0; i < 2; i++) {
                                while (state->str_end < input.path.size() &&
                                       ':' == input.path[state->str_end]) {
                                  state->str_end++;
                                }
                                if (state->str_end == input.path.size()) {
                                  break;
                                }
                                size_t next_str_end =
                                    input.path.find(':', state->str_end);
                                const std::wstring arg = input.path.substr(
                                    state->str_end, next_str_end);
                                if (i == 0 && arg.size() > 0 && arg[0] == '/') {
                                  output_pattern = arg.substr(1);
                                  break;
                                } else {
                                  size_t value;
                                  try {
                                    value = stoi(arg);
                                    if (value > 0) {
                                      value--;
                                    }
                                  } catch (const std::invalid_argument& ia) {
                                    LOG(INFO)
                                        << "stoi failed: invalid argument: "
                                        << arg;
                                    break;
                                  } catch (const std::out_of_range& ia) {
                                    LOG(INFO)
                                        << "stoi failed: out of range: " << arg;
                                    break;
                                  }
                                  if (!output_position.has_value()) {
                                    output_position = LineColumn();
                                  }
                                  if (i == 0) {
                                    output_position->line = LineNumber(value);
                                  } else {
                                    output_position->column =
                                        ColumnNumber(value);
                                  }
                                }
                                state->str_end = next_str_end;
                                if (state->str_end == input.path.npos) {
                                  break;
                                }
                              }
                              std::optional<Path> resolved_path =
                                  OptionalFrom(path_with_prefix.Resolve());
                              Path final_resolved_path =
                                  resolved_path.value_or(path_with_prefix);
                              output.value() =
                                  ResolvePathOutput{.path = final_resolved_path,
                                                    .position = output_position,
                                                    .pattern = output_pattern};
                              VLOG(4)
                                  << "Resolved path: " << final_resolved_path;
                              return IterationControlCommand::kStop;
                            });
                      })
                   .Transform([output](IterationControlCommand) {
                     return output->has_value()
                                ? IterationControlCommand::kStop
                                : IterationControlCommand::kContinue;
                   });
             })
      .Transform(
          [output](IterationControlCommand) -> ValueOrError<ResolvePathOutput> {
            if (output->has_value()) return output->value();

            // TODO(easy): Give a better error. Perhaps include the paths in
            // which we searched? Perhaps the last result of the validator?
            return Error(L"Unable to resolve file.");
          });
}

struct OpenFileResolvePathOutput {
  // If set, this is the buffer to open.
  std::optional<gc::Root<OpenBuffer>> buffer = std::nullopt;
  std::optional<Path> path = std::nullopt;
  std::optional<LineColumn> position = std::nullopt;
  std::wstring pattern = L"";
};

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
         .callback =
             [] {
               CHECK(EndsIn<int>({}, {1, 2, 3}));
             }},
        {.name = L"EmptyCandidate",
         .callback =
             [] {
               CHECK(!EndsIn<int>({1, 2, 3}, {}));
             }},
        {.name = L"ShortNonEmptyCandidate",
         .callback =
             [] {
               CHECK(!EndsIn<int>({1, 2, 3}, {1, 2}));
             }},
        {.name = L"IdenticalNonEmpty",
         .callback =
             [] {
               CHECK(EndsIn<int>({1, 2, 3}, {1, 2, 3}));
             }},
        {.name = L"EndsInLongerCandidate",
         .callback =
             [] {
               CHECK(EndsIn<int>({4, 5, 6}, {1, 2, 3, 4, 5, 6}));
             }},
        {.name = L"NotEndsInLongerCandidate",
         .callback =
             [] {
               CHECK(!EndsIn<int>({4, 5, 6}, {1, 2, 3, 4, 0, 6}));
             }},
    });

futures::ValueOrError<OpenFileResolvePathOutput> OpenFileResolvePath(
    EditorState& editor_state, std::shared_ptr<std::vector<Path>> search_paths,
    std::optional<Path> path,
    std::shared_ptr<FileSystemDriver> file_system_driver) {
  ResolvePathOptions resolve_path_options =
      ResolvePathOptions::NewWithEmptySearchPaths(editor_state,
                                                  file_system_driver);
  const NonNull<std::shared_ptr<OpenFileResolvePathOutput>> output;
  resolve_path_options.validator = [&editor_state,
                                    output](const Path& path_to_validate) {
    return std::visit(
        overload{
            [](Error) { return futures::Past(false); },
            [&](std::list<PathComponent> path_components) {
              for (std::pair<BufferName, gc::Root<OpenBuffer>> buffer_pair :
                   *editor_state.buffers()) {
                gc::Root<OpenBuffer> buffer = buffer_pair.second;
                auto buffer_path = Path::FromString(
                    buffer.ptr()->Read(buffer_variables::path));
                if (IsError(buffer_path)) continue;
                ValueOrError<std::list<PathComponent>> buffer_components =
                    std::get<Path>(buffer_path).DirectorySplit();
                if (IsError(buffer_components)) continue;
                if (EndsIn(path_components, std::get<0>(buffer_components))) {
                  output->buffer = buffer;
                  return futures::Past(true);
                }
              }
              return futures::Past(false);
            }},
        path_to_validate.DirectorySplit());
  };
  if (path.has_value()) {
    resolve_path_options.path = path.value().read();
  }

  return OnError(
      ResolvePath(resolve_path_options)
          .Transform([output](ResolvePathOutput input)
                         -> futures::ValueOrError<OpenFileResolvePathOutput> {
            // If we're here, ResolvePath must have succeeded (otherwise this
            // would have been an error). This only succeeded if the validator
            // returned true. The validator only returns true if it sets
            // output->buffer.
            //
            // TODO(2022-05-01): The above is ugly. Try to use types to ensure
            // correctness.
            CHECK(output->buffer.has_value());

            if (input.position.has_value()) {
              output->buffer->ptr()->set_position(input.position.value());
            }
            // TODO: Apply pattern.
            return futures::Past(Success(std::move(output.value())));
          }),
      [&editor_state, path, output, resolve_path_options, search_paths,
       file_system_driver](
          Error) -> futures::ValueOrError<OpenFileResolvePathOutput> {
        CHECK(!output->buffer.has_value());
        auto resolve_path_options_copy = resolve_path_options;
        resolve_path_options_copy.search_paths = *search_paths;
        resolve_path_options_copy.validator =
            [file_system_driver](const Path& path) {
              return CanStatPath(file_system_driver, path);
            };
        return ResolvePath(resolve_path_options_copy)
            .Transform([path, resolve_path_options_copy = resolve_path_options](
                           ResolvePathOutput input) {
              return futures::Past(Success(OpenFileResolvePathOutput{
                  .path = input.path,
                  .position = input.position,
                  .pattern = input.pattern.value_or(L"")}));
            });
      });
}

gc::Root<OpenBuffer> CreateBuffer(
    const OpenFileOptions& options,
    std::optional<OpenFileResolvePathOutput> resolve_path_output) {
  EditorState& editor_state = options.editor_state;
  auto buffer_options = std::make_shared<OpenBuffer::Options>(
      OpenBuffer::Options{.editor = options.editor_state});

  auto stat_buffer = std::make_shared<struct stat>();
  auto file_system_driver =
      std::make_shared<FileSystemDriver>(editor_state.thread_pool());

  buffer_options->generate_contents = [&editor_state = options.editor_state,
                                       stat_buffer,
                                       file_system_driver](OpenBuffer& target) {
    return GenerateContents(editor_state, stat_buffer, file_system_driver,
                            target);
  };
  buffer_options->handle_visit = [stat_buffer](OpenBuffer& buffer) {
    HandleVisit(*stat_buffer, buffer);
  };
  buffer_options->handle_save =
      [&editor_state = options.editor_state,
       stat_buffer](OpenBuffer::Options::HandleSaveOptions options) {
        gc::WeakPtr<OpenBuffer> buffer_weak =
            options.buffer.NewRoot().ptr().ToWeakPtr();
        return futures::OnError(
            Save(&editor_state, stat_buffer.get(), std::move(options)),
            [buffer_weak](Error error) {
              VisitPointer(
                  buffer_weak.Lock(),
                  [&error](gc::Root<OpenBuffer> buffer) {
                    buffer.ptr()->status().Set(
                        AugmentError(L"🖫 Save failed", error));
                  },
                  [] {});
              return futures::Past(error);
            });
      };
  if (resolve_path_output.has_value() &&
      resolve_path_output->path.has_value()) {
    buffer_options->path = *resolve_path_output->path;
  } else if (options.path.has_value()) {
    buffer_options->path = options.path.value();
  }
  buffer_options->log_supplier =
      [&editor_state = options.editor_state](Path edge_state_directory) {
        FileSystemDriver driver(editor_state.thread_pool());
        return NewFileLog(
            driver,
            Path::Join(edge_state_directory,
                       ValueOrDie(PathComponent::FromString(L".edge_log"))));
      };

  if (options.name.has_value()) {
    buffer_options->name = *options.name;
  } else if (buffer_options->path.has_value()) {
    buffer_options->name = BufferName(buffer_options->path.value().read());
  } else {
    buffer_options->name =
        options.editor_state.GetUnusedBufferName(L"anonymous buffer");
  }

  gc::Root<OpenBuffer> buffer =
      options.editor_state.FindOrBuildBuffer(buffer_options->name, [&] {
        gc::Root<OpenBuffer> buffer = OpenBuffer::New(*buffer_options);
        buffer.ptr()->Set(buffer_variables::persist_state, true);
        buffer.ptr()->Reload();
        return buffer;
      });
  buffer.ptr()->ResetMode();

  if (resolve_path_output.has_value() &&
      resolve_path_output->position.has_value()) {
    buffer.ptr()->set_position(*resolve_path_output->position);
  }

  options.editor_state.AddBuffer(buffer, options.insertion_type);

  if (resolve_path_output.has_value() &&
      !resolve_path_output->pattern.empty()) {
    SearchOptions search_options;
    search_options.starting_position = buffer.ptr()->position();
    search_options.search_query = resolve_path_output->pattern;
    JumpToNextMatch(options.editor_state, search_options, buffer.ptr().value());
  }
  return buffer;
}

futures::ValueOrError<gc::Root<OpenBuffer>> OpenFileIfFound(
    const OpenFileOptions& options) {
  EditorState& editor_state = options.editor_state;

  auto search_paths = std::make_shared<std::vector<Path>>(
      std::move(options.initial_search_paths));
  futures::Value<EmptyValue> search_paths_future = futures::Past(EmptyValue());
  if (options.use_search_paths) {
    search_paths_future = GetSearchPaths(editor_state, search_paths.get());
  }

  auto file_system_driver =
      std::make_shared<FileSystemDriver>(editor_state.thread_pool());

  return search_paths_future
      .Transform([&editor_state, options, search_paths,
                  file_system_driver](EmptyValue) {
        return OpenFileResolvePath(editor_state, search_paths, options.path,
                                   file_system_driver);
      })
      .Transform([options,
                  file_system_driver](OpenFileResolvePathOutput input) {
        return Success(VisitPointer(
            input.buffer,
            [&options](gc::Root<OpenBuffer> buffer) {
              options.editor_state.AddBuffer(buffer, options.insertion_type);
              return buffer;
            },
            [&options, &input] { return CreateBuffer(options, input); }));
      });
}

futures::Value<gc::Root<OpenBuffer>> OpenOrCreateFile(
    const OpenFileOptions& options) {
  return OpenFileIfFound(options).ConsumeErrors(
      [options](Error) { return futures::Past(CreateBuffer(options, {})); });
}

futures::Value<gc::Root<OpenBuffer>> OpenAnonymousBuffer(
    EditorState& editor_state) {
  return OpenOrCreateFile(
      OpenFileOptions{.editor_state = editor_state,
                      .path = std::nullopt,
                      .insertion_type = BuffersList::AddBufferType::kIgnore,
                      .use_search_paths = false});
}

}  // namespace afc::editor
