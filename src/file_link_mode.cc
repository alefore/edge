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
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_system_driver.h"
#include "src/lazy_string_append.h"
#include "src/run_command_handler.h"
#include "src/safe_types.h"
#include "src/search_handler.h"
#include "src/server.h"
#include "src/tests/tests.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/value.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {
using std::shared_ptr;
using std::unique_ptr;

futures::Value<PossibleError> GenerateContents(
    EditorState& editor_state, std::shared_ptr<struct stat> stat_buffer,
    std::shared_ptr<FileSystemDriver> file_system_driver, OpenBuffer& target) {
  CHECK(target.disk_state() == OpenBuffer::DiskState::kCurrent);
  auto path = Path::FromString(target.Read(buffer_variables::path));
  if (path.IsError()) return futures::Past(PossibleError(path.error()));
  LOG(INFO) << "GenerateContents: " << path.value();
  return file_system_driver->Stat(path.value())
      .Transform([&editor_state, stat_buffer, file_system_driver, &target,
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
          return file_system_driver
              ->Open(path.value(), O_RDONLY | O_NONBLOCK, 0)
              .Transform([&target](FileDescriptor fd) {
                target.SetInputFiles(fd, FileDescriptor(-1), false, -1);
                return Success();
              });
        }

        return GenerateDirectoryListing(path.value(), target)
            .Transform([](EmptyValue) { return futures::Past(Success()); });
      });
}

void HandleVisit(const struct stat& stat_buffer, const OpenBuffer& buffer) {
  const wstring path = buffer.Read(buffer_variables::path);
  if (stat_buffer.st_mtime == 0) {
    LOG(INFO) << "Skipping file change check.";
    return;
  }

  LOG(INFO) << "Checking if file has changed: " << path;
  const string path_raw = ToByteString(path);
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

template <typename T, typename Callable, typename ErrorCallable>
auto HandleError(ValueOrError<T> expr, ErrorCallable error_callable,
                 Callable callable) {
  return expr.IsError() ? error_callable(expr.error.value())
                        : callable(expr.value.value());
}

futures::Value<PossibleError> Save(
    EditorState*, struct stat* stat_buffer,
    OpenBuffer::Options::HandleSaveOptions options) {
  auto& buffer = options.buffer;
  auto path_or_error = Path::FromString(buffer.Read(buffer_variables::path));
  if (path_or_error.IsError()) {
    return futures::Past(PossibleError(
        Error(L"Buffer can't be saved: Invalid “path” variable: " +
              path_or_error.error().description)));
  }
  if (S_ISDIR(stat_buffer->st_mode)) {
    return options.save_type == OpenBuffer::Options::SaveType::kBackup
               ? futures::Past(Success())
               : futures::Past(PossibleError(
                     Error(L"Buffer can't be saved: Buffer is a directory.")));
  }

  futures::ValueOrError<Path> path = futures::Past(path_or_error);

  switch (options.save_type) {
    case OpenBuffer::Options::SaveType::kMainFile:
      break;
    case OpenBuffer::Options::SaveType::kBackup:
      path = OnError(buffer.GetEdgeStateDirectory(), [](Error error) {
               return Error::Augment(L"Unable to backup buffer: ", error);
             }).Transform([](Path state_directory) {
        return Success(Path::Join(
            state_directory, PathComponent::FromString(L"backup").value()));
      });
  }

  return path.Transform([stat_buffer, options,
                         buffer = buffer.shared_from_this()](Path path) {
    return SaveContentsToFile(path, buffer->contents().copy(),
                              buffer->editor().thread_pool(),
                              buffer->file_system_driver())
        .Transform([buffer](EmptyValue) { return buffer->PersistState(); })
        .Transform([stat_buffer, options, buffer, path](EmptyValue) {
          CHECK(buffer != nullptr);
          switch (options.save_type) {
            case OpenBuffer::Options::SaveType::kMainFile:
              buffer->status().SetInformationText(L"🖫 Saved: " +
                                                  path.read());
              // TODO(easy): Move this to the caller, for symmetry with
              // kBackup case.
              // TODO: Since the save is async, what if the contents have
              // changed in the meantime?
              buffer->SetDiskState(OpenBuffer::DiskState::kCurrent);
              for (const auto& dir : buffer->editor().edge_path()) {
                buffer->EvaluateFile(Path::Join(
                    dir, Path::FromString(L"/hooks/buffer-save.cc").value()));
              }
              if (buffer->Read(
                      buffer_variables::trigger_reload_on_buffer_write)) {
                for (auto& it : *buffer->editor().buffers()) {
                  CHECK(it.second != nullptr);
                  if (it.second->Read(
                          buffer_variables::reload_on_buffer_write)) {
                    LOG(INFO) << "Write of " << path << " triggers reload: "
                              << it.second->Read(buffer_variables::name);
                    it.second->Reload();
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
      [path, consumer =
                 std::move(output.consumer)](ValueOrError<struct stat> result) {
        if (result.IsError()) {
          VLOG(6) << path << ": stat failed: " << result.error();
          consumer(false);
        } else {
          VLOG(4) << "Stat succeeded: " << path;
          consumer(true);
        }
      });
  return std::move(output.value);
}

futures::Value<PossibleError> SaveContentsToOpenFile(
    ThreadPool& thread_pool, Path path, FileDescriptor fd,
    std::shared_ptr<const BufferContents> contents) {
  return thread_pool.Run([contents, path, fd]() {
    // TODO: It'd be significant more efficient to do fewer (bigger)
    // writes.
    std::optional<PossibleError> error;
    contents->EveryLine([&](LineNumber position, const Line& line) {
      string str = (position == LineNumber(0) ? "" : "\n") +
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
    const Path& path, std::shared_ptr<const BufferContents> contents,
    ThreadPool& thread_pool, FileSystemDriver& file_system_driver) {
  Path tmp_path = Path::Join(
      path.Dirname().value(),
      PathComponent::FromString(path.Basename().value().ToString() + L".tmp")
          .value());
  return futures::OnError(
             file_system_driver.Stat(path),
             [](Error error) {
               LOG(INFO)
                   << "Ignoring stat error; maybe a new file is being created: "
                   << error.description;
               struct stat value;
               value.st_mode =
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
               return Success(value);
             })
      .Transform([path, &file_system_driver, tmp_path](struct stat stat_value) {
        return file_system_driver.Open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC,
                                       stat_value.st_mode);
      })
      .Transform([&thread_pool, path, contents, tmp_path,
                  &file_system_driver](FileDescriptor fd) {
        CHECK_NE(fd.read(), -1);
        return OnError(
                   SaveContentsToOpenFile(thread_pool, tmp_path, fd, contents),
                   [&file_system_driver, fd](Error error) {
                     file_system_driver.Close(fd);
                     return error;
                   })
            .Transform([&file_system_driver, fd](EmptyValue) {
              return file_system_driver.Close(fd);
            })
            .Transform([path, &file_system_driver, tmp_path](EmptyValue) {
              return file_system_driver.Rename(tmp_path, path);
            });
      });
}

futures::Value<std::shared_ptr<OpenBuffer>> GetSearchPathsBuffer(
    EditorState& editor_state, const Path& edge_path) {
  BufferName buffer_name(L"- search paths");
  auto it = editor_state.buffers()->find(buffer_name);
  futures::Value<std::shared_ptr<OpenBuffer>> output =
      it != editor_state.buffers()->end()
          ? futures::Past(it->second)
          : OpenFile(
                OpenFileOptions{
                    .editor_state = editor_state,
                    .name = buffer_name,
                    .path = Path::Join(
                        edge_path, Path::FromString(L"/search_paths").value()),
                    .insertion_type = BuffersList::AddBufferType::kIgnore,
                    .use_search_paths = false})
                .Transform(
                    [&editor_state](
                        std::map<BufferName,
                                 std::shared_ptr<OpenBuffer>>::iterator it) {
                      CHECK(it != editor_state.buffers()->end());
                      CHECK(it->second != nullptr);
                      it->second->Set(buffer_variables::save_on_close, true);
                      it->second->Set(
                          buffer_variables::trigger_reload_on_buffer_write,
                          false);
                      it->second->Set(buffer_variables::show_in_buffers_list,
                                      false);
                      if (!editor_state.has_current_buffer()) {
                        editor_state.set_current_buffer(
                            it->second, CommandArgumentModeApplyMode::kFinal);
                      }
                      return it->second;
                    });

  return output.Transform([](std::shared_ptr<OpenBuffer> buffer) {
    return buffer->WaitForEndOfFile().Transform(
        [buffer](EmptyValue) { return buffer; });
  });
}

futures::Value<EmptyValue> GetSearchPaths(EditorState& editor_state,
                                          vector<Path>* output) {
  output->push_back(Path::LocalDirectory());

  auto paths = editor_state.edge_path();
  return futures::ForEachWithCopy(
             paths.begin(), paths.end(),
             [&editor_state, output](Path edge_path) {
               return GetSearchPathsBuffer(editor_state, edge_path)
                   .Transform([&editor_state, output,
                               edge_path](std::shared_ptr<OpenBuffer> buffer) {
                     if (buffer == nullptr) {
                       LOG(INFO) << edge_path << ": No search paths buffer.";
                       return futures::IterationControlCommand::kContinue;
                     }
                     buffer->contents().ForEach([&editor_state,
                                                 output](wstring line) {
                       auto path = Path::FromString(line);
                       if (path.IsError()) return;
                       output->push_back(
                           editor_state.expand_path(path.value()));
                       LOG(INFO) << "Pushed search path: " << output->back();
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

ResolvePathOptions::ResolvePathOptions(Path home_directory, Validator validator)
    : home_directory(std::move(home_directory)),
      validator(std::move(validator)) {}

futures::ValueOrError<ResolvePathOutput> ResolvePath(ResolvePathOptions input) {
  if (find(input.search_paths.begin(), input.search_paths.end(),
           Path::LocalDirectory()) == input.search_paths.end()) {
    input.search_paths.push_back(Path::LocalDirectory());
  }

  if (auto path = Path::FromString(input.path); !path.IsError()) {
    input.path =
        Path::ExpandHomeDirectory(input.home_directory, path.value()).read();
  }
  if (!input.path.empty() && input.path[0] == L'/') {
    input.search_paths = {Path::Root()};
  }
  auto output =
      std::make_shared<std::optional<ValueOrError<ResolvePathOutput>>>();
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
                        if (input_path.IsError()) {
                          state->str_end =
                              input.path.find_last_of(':', state->str_end - 1);
                          return Past(IterationControlCommand::kContinue);
                        }
                        auto path_with_prefix =
                            Path::Join(state->search_path, input_path.value());
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
                                const wstring arg = input.path.substr(
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
                              auto resolved = path_with_prefix.Resolve();
                              *output = Success(ResolvePathOutput{
                                  .path = resolved.IsError() ? path_with_prefix
                                                             : resolved.value(),
                                  .position = output_position,
                                  .pattern = output_pattern});
                              VLOG(4) << "Resolved path: "
                                      << output->value().value().path;
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
            return ValueOrError<ResolvePathOutput>(
                Error(L"Unable to resolve file."));
          });
}

struct OpenFileResolvePathOutput {
  // If set, this is the buffer to open.
  std::optional<std::map<BufferName, std::shared_ptr<OpenBuffer>>::iterator>
      buffer = std::nullopt;
  std::optional<Path> path = std::nullopt;
  std::optional<LineColumn> position = std::nullopt;
  wstring pattern = L"";
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

futures::Value<OpenFileResolvePathOutput> OpenFileResolvePath(
    EditorState& editor_state, std::shared_ptr<std::vector<Path>> search_paths,
    std::optional<Path> path, bool ignore_if_not_found,
    std::shared_ptr<FileSystemDriver> file_system_driver) {
  ResolvePathOptions resolve_path_options =
      ResolvePathOptions::NewWithEmptySearchPaths(editor_state,
                                                  file_system_driver);
  auto output = std::make_shared<OpenFileResolvePathOutput>();
  resolve_path_options.validator = [&editor_state, output](const Path& path) {
    auto path_components = path.DirectorySplit();
    if (path_components.IsError()) return futures::Past(false);
    for (auto it = editor_state.buffers()->begin();
         it != editor_state.buffers()->end(); ++it) {
      CHECK(it->second != nullptr);
      auto buffer_path =
          Path::FromString(it->second->Read(buffer_variables::path));
      if (buffer_path.IsError()) continue;
      auto buffer_components = buffer_path.value().DirectorySplit();
      if (buffer_components.IsError()) continue;
      if (EndsIn(path_components.value(), buffer_components.value())) {
        output->buffer = it;
        return futures::Past(true);
      }
    }
    return futures::Past(false);
  };
  if (path.has_value()) {
    resolve_path_options.path = path.value().read();
  }

  return ResolvePath(resolve_path_options)
      .Transform([output](ResolvePathOutput input) {
        CHECK(output->buffer.has_value());
        if (input.position.has_value()) {
          output->buffer.value()->second->set_position(input.position.value());
        }
        // TODO: Apply pattern.
        return futures::Past(Success(std::move(*output)));
      })
      .ConsumeErrors([&editor_state, path, ignore_if_not_found, output,
                      resolve_path_options, search_paths,
                      file_system_driver](Error) {
        auto resolve_path_options_copy = resolve_path_options;
        resolve_path_options_copy.search_paths = *search_paths;
        resolve_path_options_copy.validator =
            [file_system_driver](const Path& path) {
              return CanStatPath(file_system_driver, path);
            };
        return ResolvePath(resolve_path_options_copy)
            .Transform([path, ignore_if_not_found,
                        resolve_path_options_copy =
                            resolve_path_options](ResolvePathOutput input) {
              return futures::Past(Success(OpenFileResolvePathOutput{
                  .path = input.path,
                  .position = input.position,
                  .pattern = input.pattern.value_or(L"")}));
            })
            .ConsumeErrors(
                [&editor_state, path, ignore_if_not_found, output](Error) {
                  if (ignore_if_not_found) {
                    output->buffer = editor_state.buffers()->end();
                  }
                  if (path.has_value()) {
                    output->path = path.value();
                  }
                  return futures::Past(std::move(*output));
                });
      });
}

futures::Value<std::map<BufferName, std::shared_ptr<OpenBuffer>>::iterator>
OpenFile(const OpenFileOptions& options) {
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
                                   options.ignore_if_not_found,
                                   file_system_driver);
      })
      .Transform([options,
                  file_system_driver](OpenFileResolvePathOutput input) {
        if (input.buffer.has_value()) {
          // Found the buffer, just return it.
          auto value = input.buffer.value();
          if (value != options.editor_state.buffers()->end()) {
            CHECK(value->second != nullptr);
            options.editor_state.AddBuffer(value->second,
                                           options.insertion_type);
          }
          return input.buffer.value();
        }
        auto buffer_options = std::make_shared<OpenBuffer::Options>(
            OpenBuffer::Options{.editor = options.editor_state});

        auto stat_buffer = std::make_shared<struct stat>();
        buffer_options->generate_contents =
            [&editor_state = options.editor_state, stat_buffer,
             file_system_driver](OpenBuffer& target) {
              return GenerateContents(editor_state, stat_buffer,
                                      file_system_driver, target);
            };
        buffer_options->handle_visit = [stat_buffer](OpenBuffer& buffer) {
          HandleVisit(*stat_buffer, buffer);
        };
        buffer_options->handle_save =
            [&editor_state = options.editor_state,
             stat_buffer](OpenBuffer::Options::HandleSaveOptions options) {
              auto& buffer = options.buffer;
              return futures::OnError(
                  Save(&editor_state, stat_buffer.get(), std::move(options)),
                  [buffer_weak = std::weak_ptr<OpenBuffer>(
                       buffer.shared_from_this())](Error error) {
                    IfObj(buffer_weak, [&error](OpenBuffer& buffer) {
                      buffer.status().SetWarningText(L"🖫 Save failed: " +
                                                     error.description);
                    });
                    return error;
                  });
            };
        if (input.path.has_value()) {
          buffer_options->path = input.path.value();
        }
        buffer_options->log_supplier =
            [path = input.path, &editor_state = options.editor_state](
                std::shared_ptr<WorkQueue>, Path edge_state_directory) {
              FileSystemDriver driver(editor_state.thread_pool());
              return NewFileLog(
                  &driver,
                  Path::Join(edge_state_directory,
                             PathComponent::FromString(L".edge_log").value()));
            };

        std::shared_ptr<OpenBuffer> buffer;

        if (options.name.has_value()) {
          buffer_options->name = *options.name;
        } else if (buffer_options->path.has_value()) {
          buffer_options->name =
              BufferName(buffer_options->path.value().read());
        } else {
          buffer_options->name =
              options.editor_state.GetUnusedBufferName(L"anonymous buffer");
          buffer = OpenBuffer::New(*buffer_options);
        }
        auto insert_result = options.editor_state.buffers()->insert(
            {buffer_options->name, buffer});
        if (insert_result.second) {
          if (insert_result.first->second.get() == nullptr) {
            insert_result.first->second =
                OpenBuffer::New(std::move(*buffer_options));
            insert_result.first->second->Set(buffer_variables::persist_state,
                                             true);
          }
          insert_result.first->second->Reload();
        } else {
          insert_result.first->second->ResetMode();
        }
        if (input.position.has_value()) {
          insert_result.first->second->set_position(input.position.value());
        }

        options.editor_state.AddBuffer(insert_result.first->second,
                                       options.insertion_type);

        if (!input.pattern.empty()) {
          SearchOptions search_options;
          search_options.starting_position =
              insert_result.first->second->position();
          search_options.search_query = input.pattern;
          JumpToNextMatch(options.editor_state, search_options,
                          *insert_result.first->second);
        }
        return insert_result.first;
      });
}

futures::Value<std::shared_ptr<OpenBuffer>> OpenAnonymousBuffer(
    EditorState& editor_state) {
  return OpenFile(OpenFileOptions{
                      .editor_state = editor_state,
                      .path = std::nullopt,
                      .insertion_type = BuffersList::AddBufferType::kIgnore,
                      .use_search_paths = false})
      .Transform([](auto it) { return it->second; });
}

}  // namespace editor
}  // namespace afc
