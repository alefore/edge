#include "src/file_link_mode.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <regex>
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
#include "src/dirname.h"
#include "src/editor.h"
#include "src/file_system_driver.h"
#include "src/lazy_string_append.h"
#include "src/line_prompt_mode.h"
#include "src/run_command_handler.h"
#include "src/search_handler.h"
#include "src/server.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/value.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {
using std::make_pair;
using std::shared_ptr;
using std::sort;
using std::unique_ptr;

void StartDeleteFile(EditorState* editor_state, wstring path) {
  PromptOptions options;
  options.editor_state = editor_state;
  options.prompt = L"unlink " + path + L"? [yes/no] ",
  options.history_file = L"confirmation";
  options.handler = [path](const wstring input, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    auto status = buffer == nullptr ? editor_state->status() : buffer->status();
    if (input == L"yes") {
      int result = unlink(ToByteString(path).c_str());
      status->SetInformationText(
          path + L": unlink: " +
          (result == 0 ? L"done"
                       : L"ERROR: " + FromByteString(strerror(errno))));
    } else {
      // TODO: insert it again?  Actually, only let it be erased
      // in the other case.
      status->SetInformationText(L"Ignored.");
    }
    if (buffer != nullptr) {
      buffer->ResetMode();
    }
    return futures::Past(EmptyValue());
  };
  options.predictor = PrecomputedPredictor({L"no", L"yes"}, '/');
  Prompt(std::move(options));
}

void AddLine(EditorState* editor_state, OpenBuffer* target,
             const dirent& entry) {
  enum class SizeBehavior { kShow, kSkip };

  struct FileType {
    wstring description;
    LineModifierSet modifiers;
  };
  static const std::unordered_map<int, FileType> types = {
      {DT_BLK, {L" (block dev)", {GREEN}}},
      {DT_CHR, {L" (char dev)", {RED}}},
      {DT_DIR, {L"/", {CYAN}}},
      {DT_FIFO, {L" (named pipe)", {BLUE}}},
      {DT_LNK, {L"@", {ITALIC}}},
      {DT_REG, {L"", {}}},
      {DT_SOCK, {L" (unix sock)", {MAGENTA}}}};

  auto path = FromByteString(entry.d_name);

  auto type_it = types.find(entry.d_type);
  if (type_it == types.end()) {
    type_it = types.find(DT_REG);
    CHECK(type_it != types.end());
  }

  Line::Options line_options;
  line_options.contents =
      shared_ptr<LazyString>(NewLazyString(path + type_it->second.description));
  if (!type_it->second.modifiers.empty()) {
    line_options.modifiers[ColumnNumber(0)] = (type_it->second.modifiers);
  }

  auto line = std::make_shared<Line>(std::move(line_options));

  target->AppendRawLine(line);
  target->contents()->back()->environment()->Define(
      L"EdgeLineDeleteHandler", vm::NewCallback([editor_state, path]() {
        StartDeleteFile(editor_state, path);
      }));
}

void ShowFiles(EditorState* editor_state, wstring name,
               std::vector<dirent> entries, OpenBuffer* target) {
  if (entries.empty()) {
    return;
  }
  std::sort(entries.begin(), entries.end(),
            [](const dirent& a, const dirent& b) {
              return strcmp(a.d_name, b.d_name) < 0;
            });

  target->AppendLine(NewLazyString(L"## " + name + L" (" +
                                   std::to_wstring(entries.size()) + L")"));
  for (auto& entry : entries) {
    AddLine(editor_state, target, entry);
  }
  target->AppendEmptyLine();
}

struct BackgroundReadDirOutput {
  std::optional<std::wstring> error_description;
  std::vector<dirent> directories;
  std::vector<dirent> regular_files;
  std::vector<dirent> noise;
};

BackgroundReadDirOutput ReadDir(Path path, std::wregex noise_regex) {
  BackgroundReadDirOutput output;
  auto dir = OpenDir(path.ToString());
  if (dir == nullptr) {
    output.error_description =
        L"Unable to open directory: " + FromByteString(strerror(errno));
    return output;
  }
  struct dirent* entry;
  while ((entry = readdir(dir.get())) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0) {
      continue;  // Showing the link to itself is rather pointless.
    }

    auto name = FromByteString(entry->d_name);
    if (std::regex_match(name, noise_regex)) {
      output.noise.push_back(*entry);
      continue;
    }

    if (entry->d_type == DT_DIR) {
      output.directories.push_back(*entry);
      continue;
    }

    output.regular_files.push_back(*entry);
  }
  return output;
}

futures::Value<PossibleError> GenerateContents(
    EditorState* editor_state, std::shared_ptr<struct stat> stat_buffer,
    std::shared_ptr<FileSystemDriver> file_system_driver,
    std::shared_ptr<AsyncEvaluator> background_directory_reader,
    OpenBuffer* target) {
  CHECK(target->disk_state() == OpenBuffer::DiskState::kCurrent);
  auto path = Path::FromString(target->Read(buffer_variables::path));
  if (path.IsError()) return futures::Past(PossibleError(path.error()));
  LOG(INFO) << "GenerateContents: " << path.value();
  return futures::Transform(
      file_system_driver->Stat(path.value()),
      [editor_state, stat_buffer, file_system_driver,
       background_directory_reader, target,
       path](std::optional<struct stat> stat_results) {
        if (stat_results.has_value() &&
            target->Read(buffer_variables::clear_on_reload)) {
          target->ClearContents(BufferContents::CursorsBehavior::kUnmodified);
          target->SetDiskState(OpenBuffer::DiskState::kCurrent);
        }
        if (!stat_results.has_value()) {
          return futures::Past(Success());
        }
        *stat_buffer = stat_results.value();

        if (!S_ISDIR(stat_buffer->st_mode)) {
          return futures::Transform(
              file_system_driver->Open(path.value(), O_RDONLY | O_NONBLOCK, 0),
              [target](int fd) {
                target->SetInputFiles(fd, -1, false, -1);
                return Success();
              });
        }

        target->Set(buffer_variables::atomic_lines, true);
        target->Set(buffer_variables::allow_dirty_delete, true);
        target->Set(buffer_variables::tree_parser, L"md");
        return futures::Transform(
            background_directory_reader->Run(
                [path, noise_regexp =
                           target->Read(buffer_variables::directory_noise)]() {
                  return ReadDir(path.value(), std::wregex(noise_regexp));
                }),
            [editor_state, target, path](BackgroundReadDirOutput results) {
              auto disk_state_freezer = target->FreezeDiskState();
              if (results.error_description.has_value()) {
                target->status()->SetInformationText(
                    results.error_description.value());
                target->AppendLine(NewLazyString(
                    std::move(results.error_description.value())));
                return Success();
              }

              target->AppendToLastLine(NewLazyString(L"# 🗁  File listing: " +
                                                     path.value().ToString()));
              target->AppendEmptyLine();

              ShowFiles(editor_state, L"🗁  Directories",
                        std::move(results.directories), target);
              ShowFiles(editor_state, L"🗀  Files",
                        std::move(results.regular_files), target);
              ShowFiles(editor_state, L"🗐  Noise", std::move(results.noise),
                        target);
              return Success();
            });
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
    buffer.status()->SetInformationText(
        L"🌷Directory changed in disk since last read.");
  } else {
    buffer.status()->SetWarningText(L"🌷File changed in disk since last read.");
  }
}

template <typename T, typename Callable, typename ErrorCallable>
auto HandleError(ValueOrError<T> expr, ErrorCallable error_callable,
                 Callable callable) {
  return expr.IsError() ? error_callable(expr.error.value())
                        : callable(expr.value.value());
}

futures::Value<PossibleError> Save(
    EditorState* editor_state, struct stat* stat_buffer,
    OpenBuffer::Options::HandleSaveOptions options) {
  auto buffer = options.buffer;
  auto path_or_error = Path::FromString(buffer->Read(buffer_variables::path));
  if (path_or_error.IsError()) {
    return futures::Past(PossibleError(
        Error(L"Buffer can't be saved: Invalid “path” variable: " +
              path_or_error.error().description)));
  }
  auto path = path_or_error.value();
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
      auto state_directory = buffer->GetEdgeStateDirectory();
      if (state_directory.IsError()) {
        return futures::Past(PossibleError(Error::Augment(
            L"Unable to backup buffer: ", state_directory.error())));
      }
      path = Path::Join(state_directory.value(),
                        PathComponent::FromString(L"backup").value());
  }

  return futures::Transform(
      SaveContentsToFile(path, *buffer->contents(), buffer->work_queue()),
      [buffer](EmptyValue) { return buffer->PersistState(); },
      [editor_state, stat_buffer, options, buffer, path](EmptyValue) {
        switch (options.save_type) {
          case OpenBuffer::Options::SaveType::kMainFile:
            buffer->status()->SetInformationText(L"🖫 Saved: " +
                                                 path.ToString());
            // TODO(easy): Move this to the caller, for symmetry with
            // kBackup case.
            buffer->SetDiskState(OpenBuffer::DiskState::kCurrent);
            for (const auto& dir : editor_state->edge_path()) {
              buffer->EvaluateFile(Path::Join(
                  dir, Path::FromString(L"/hooks/buffer-save.cc").value()));
            }
            if (buffer->Read(
                    buffer_variables::trigger_reload_on_buffer_write)) {
              for (auto& it : *editor_state->buffers()) {
                CHECK(it.second != nullptr);
                if (it.second->Read(buffer_variables::reload_on_buffer_write)) {
                  LOG(INFO) << "Write of " << path << " triggers reload: "
                            << it.second->Read(buffer_variables::name);
                  it.second->Reload();
                }
              }
            }
            stat(ToByteString(path.ToString()).c_str(), stat_buffer);
            break;
          case OpenBuffer::Options::SaveType::kBackup:
            break;
        }
        return futures::Past(Success());
      });
}

static futures::Value<bool> CanStatPath(
    std::shared_ptr<FileSystemDriver> file_system_driver,
    const wstring& path_str) {
  CHECK(!path_str.empty());
  VLOG(5) << "Considering path: " << path_str;
  futures::Future<bool> output;
  Path path = Path::FromString(path_str).value();
  file_system_driver->Stat(path).SetConsumer(
      [path, consumer =
                 std::move(output.consumer)](ValueOrError<struct stat> result) {
        if (result.IsError()) {
          VLOG(6) << path.ToString() << ": stat failed: " << result.error();
          consumer(false);
        } else {
          VLOG(4) << "Stat succeeded: " << path.ToString();
          consumer(true);
        }
      });
  return output.value;
}

}  // namespace

using std::unique_ptr;

// Always returns an actual value.
futures::Value<PossibleError> SaveContentsToOpenFile(
    WorkQueue* work_queue, Path path, int fd, const BufferContents& contents) {
  auto contents_writer =
      std::make_shared<AsyncEvaluator>(L"SaveContentsToOpenFile", work_queue);
  return futures::Transform(
      contents_writer->Run([contents, path, fd]() {
        // TODO: It'd be significant more efficient to do fewer (bigger)
        // writes.
        std::optional<PossibleError> error;
        contents.EveryLine([&](LineNumber position, const Line& line) {
          string str = (position == LineNumber(0) ? "" : "\n") +
                       ToByteString(line.ToString());
          if (write(fd, str.c_str(), str.size()) == -1) {
            error = Error(path.ToString() + L": write failed: " +
                          std::to_wstring(fd) + L": " +
                          FromByteString(strerror(errno)));
            return false;
          }
          return true;
        });
        return error.value_or(Success());
      }),
      // Ensure that `contents_writer` survives the future.
      //
      // TODO: Improve AsyncEvaluator functionality to survive being deleted
      // while executing?
      [contents_writer](EmptyValue) { return Success(); });
}

futures::Value<PossibleError> SaveContentsToFile(const Path& path,
                                                 const BufferContents& contents,
                                                 WorkQueue* work_queue) {
  auto file_system_driver = std::make_shared<FileSystemDriver>(work_queue);
  Path tmp_path = Path::Join(
      path.Dirname().value(),
      PathComponent::FromString(path.Basename().value().ToString() + L".tmp")
          .value());
  return futures::Transform(
      futures::OnError(
          file_system_driver->Stat(path),
          [](Error error) {
            LOG(INFO)
                << "Ignoring stat error; maybe a new file is being created: "
                << error.description;
            struct stat value;
            value.st_mode =
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
            return Success(value);
          }),
      [path, contents, work_queue, file_system_driver,
       tmp_path](struct stat stat_value) {
        return file_system_driver->Open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC,
                                        stat_value.st_mode);
      },
      [path, contents, work_queue, tmp_path, file_system_driver](int fd) {
        CHECK_NE(fd, -1);
        return futures::Transform(
            OnError(SaveContentsToOpenFile(work_queue, tmp_path, fd, contents),
                    [file_system_driver, fd](Error error) {
                      file_system_driver->Close(fd);
                      return error;
                    }),
            [file_system_driver, fd](EmptyValue) {
              return file_system_driver->Close(fd);
            },
            [path, file_system_driver, tmp_path](EmptyValue) {
              return file_system_driver->Rename(tmp_path, path);
            });
      });
}

futures::Value<std::shared_ptr<OpenBuffer>> GetSearchPathsBuffer(
    EditorState* editor_state, const Path& edge_path) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.name = L"- search paths";
  auto it = editor_state->buffers()->find(options.name);
  if (it != editor_state->buffers()->end()) {
    LOG(INFO) << "search paths buffer already existed.";
    return futures::Past(it->second);
  }
  options.path =
      Path::Join(edge_path, Path::FromString(L"/search_paths").value());
  options.insertion_type = BuffersList::AddBufferType::kIgnore;
  options.use_search_paths = false;
  return futures::Transform(
      OpenFile(options),
      [editor_state](map<wstring, shared_ptr<OpenBuffer>>::iterator it) {
        CHECK(it != editor_state->buffers()->end());
        CHECK(it->second != nullptr);
        it->second->Set(buffer_variables::save_on_close, true);
        it->second->Set(buffer_variables::trigger_reload_on_buffer_write,
                        false);
        it->second->Set(buffer_variables::show_in_buffers_list, false);
        if (!editor_state->has_current_buffer()) {
          editor_state->set_current_buffer(
              it->second, CommandArgumentModeApplyMode::kFinal);
        }
        return it->second;
      });
}

futures::Value<EmptyValue> GetSearchPaths(EditorState* editor_state,
                                          vector<Path>* output) {
  output->push_back(Path::LocalDirectory());

  auto paths = editor_state->edge_path();
  return futures::Transform(
      futures::ForEachWithCopy(
          paths.begin(), paths.end(),
          [editor_state, output](Path edge_path) {
            return futures::Transform(
                GetSearchPathsBuffer(editor_state, edge_path),
                [editor_state, output,
                 edge_path](std::shared_ptr<OpenBuffer> buffer) {
                  if (buffer == nullptr) {
                    LOG(INFO) << edge_path << ": No search paths buffer.";
                    return futures::IterationControlCommand::kContinue;
                  }
                  buffer->contents()->ForEach([editor_state,
                                               output](wstring line) {
                    auto path = Path::FromString(line);
                    if (path.IsError()) return;
                    output->push_back(
                        Path::FromString(
                            editor_state->expand_path(path.value().ToString()))
                            .value());
                    LOG(INFO) << "Pushed search path: " << output->back();
                  });
                  return futures::IterationControlCommand::kContinue;
                });
          }),
      [](futures::IterationControlCommand) { return EmptyValue(); });
}

/* static */
ResolvePathOptions ResolvePathOptions::New(
    EditorState* editor_state,
    std::shared_ptr<FileSystemDriver> file_system_driver) {
  auto output =
      NewWithEmptySearchPaths(editor_state, std::move(file_system_driver));
  GetSearchPaths(editor_state, &output.search_paths);
  return output;
}

/* static */ ResolvePathOptions ResolvePathOptions::NewWithEmptySearchPaths(
    EditorState* editor_state,
    std::shared_ptr<FileSystemDriver> file_system_driver) {
  return ResolvePathOptions{
      .home_directory = editor_state->home_directory(),
      .validator = [file_system_driver](const std::wstring& path) {
        return CanStatPath(file_system_driver, path);
      }};
}

futures::ValueOrError<ResolvePathOutput> ResolvePath(ResolvePathOptions input) {
  if (find(input.search_paths.begin(), input.search_paths.end(),
           Path::LocalDirectory()) == input.search_paths.end()) {
    input.search_paths.push_back(Path::LocalDirectory());
  }

  if (auto path = Path::FromString(input.path); !path.IsError()) {
    input.path = Path::ExpandHomeDirectory(input.home_directory, path.value())
                     .ToString();
  }
  if (!input.path.empty() && input.path[0] == L'/') {
    input.search_paths = {Path::Root()};
  }
  auto output =
      std::make_shared<std::optional<ValueOrError<ResolvePathOutput>>>();
  using futures::IterationControlCommand;
  using futures::Past;
  using futures::Transform;
  return Transform(
      futures::ForEachWithCopy(
          input.search_paths.begin(), input.search_paths.end(),
          [input, output](Path search_path) {
            struct State {
              const Path search_path;
              size_t str_end;
            };
            auto state = std::make_shared<State>(
                State{.search_path = std::move(search_path),
                      .str_end = input.path.size()});
            return futures::Transform(
                futures::While([input, output, state]() {
                  if (state->str_end == input.path.npos ||
                      state->str_end == 0) {
                    return Past(IterationControlCommand::kStop);
                  }

                  auto input_path =
                      Path::FromString(input.path.substr(0, state->str_end));
                  if (input_path.IsError()) {
                    state->str_end =
                        input.path.find_last_of(':', state->str_end - 1);
                    return Past(IterationControlCommand::kContinue);
                  }
                  auto path_with_prefix =
                      Path::Join(state->search_path, input_path.value());
                  return futures::Transform(
                      input.validator(path_with_prefix.ToString()),
                      [input, output, state,
                       path_with_prefix](bool validator_output)
                          -> futures::Value<IterationControlCommand> {
                        if (!validator_output) {
                          state->str_end =
                              input.path.find_last_of(':', state->str_end - 1);
                          return Past(IterationControlCommand::kContinue);
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
                          const wstring arg =
                              input.path.substr(state->str_end, next_str_end);
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
                                  << "stoi failed: invalid argument: " << arg;
                              break;
                            } catch (const std::out_of_range& ia) {
                              LOG(INFO) << "stoi failed: out of range: " << arg;
                              break;
                            }
                            if (!output_position.has_value()) {
                              output_position = LineColumn();
                            }
                            if (i == 0) {
                              output_position->line = LineNumber(value);
                            } else {
                              output_position->column = ColumnNumber(value);
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
                        return Past(IterationControlCommand::kStop);
                      });
                }),
                [output](IterationControlCommand) {
                  return output->has_value()
                             ? IterationControlCommand::kStop
                             : IterationControlCommand::kContinue;
                });
          }),
      [output](IterationControlCommand) -> ValueOrError<ResolvePathOutput> {
        if (output->has_value()) return output->value();

        // TODO(easy): Give a better error. Perhaps include the paths in which
        // we searched? Perhaps the last result of the validator?
        return ValueOrError<ResolvePathOutput>(
            Error(L"Unable to resolve file."));
      });
}

struct OpenFileResolvePathOutput {
  // If set, this is the buffer to open.
  std::optional<map<wstring, shared_ptr<OpenBuffer>>::iterator> buffer = {};
  std::optional<Path> path = {};
  std::optional<LineColumn> position = {};
  wstring pattern = L"";
};

futures::Value<OpenFileResolvePathOutput> OpenFileResolvePath(
    EditorState* editor_state, std::shared_ptr<std::vector<Path>> search_paths,
    std::optional<Path> path, bool ignore_if_not_found,
    std::shared_ptr<FileSystemDriver> file_system_driver) {
  auto resolve_path_options = ResolvePathOptions::NewWithEmptySearchPaths(
      editor_state, file_system_driver);
  resolve_path_options.search_paths = *search_paths;
  if (path.has_value()) {
    resolve_path_options.path = path.value().ToString();
  }
  futures::Future<OpenFileResolvePathOutput> output;
  ResolvePath(resolve_path_options)
      .SetConsumer([editor_state, path, ignore_if_not_found,
                    resolve_path_options_copy = resolve_path_options,
                    consumer = std::move(output.consumer)](
                       ValueOrError<ResolvePathOutput> input) {
        if (!input.IsError()) {
          consumer(OpenFileResolvePathOutput{
              .path = input.value().path,
              .position = input.value().position,
              .pattern = input.value().pattern.value_or(L"")});
          return;
        }
        auto resolve_path_options = resolve_path_options_copy;
        auto output = std::make_shared<OpenFileResolvePathOutput>();
        resolve_path_options.validator = [editor_state,
                                          output](const wstring& path) {
          DCHECK(!path.empty());
          for (auto it = editor_state->buffers()->begin();
               it != editor_state->buffers()->end(); ++it) {
            CHECK(it->second != nullptr);
            auto buffer_path = it->second->Read(buffer_variables::path);
            if (buffer_path.size() >= path.size() &&
                buffer_path.substr(buffer_path.size() - path.size()) == path &&
                (buffer_path.size() == path.size() || path[0] == L'/' ||
                 buffer_path[buffer_path.size() - path.size() - 1] == L'/')) {
              output->buffer = it;
              return futures::Past(true);
            }
          }
          return futures::Past(false);
        };
        resolve_path_options.search_paths = {Path::LocalDirectory()};
        ResolvePath(resolve_path_options)
            .SetConsumer([editor_state, path, ignore_if_not_found,
                          consumer = std::move(consumer),
                          output](ValueOrError<ResolvePathOutput> input) {
              if (!input.IsError()) {
                CHECK(output->buffer.has_value());
                editor_state->set_current_buffer(
                    output->buffer.value()->second,
                    CommandArgumentModeApplyMode::kFinal);
                if (input.value().position.has_value()) {
                  output->buffer.value()->second->set_position(
                      input.value().position.value());
                }
                // TODO: Apply pattern.
              } else {
                if (ignore_if_not_found) {
                  output->buffer = editor_state->buffers()->end();
                }
                if (path.has_value()) {
                  output->path = path.value();
                }
              }
              consumer(*output);
            });
      });
  return output.value;
}

futures::Value<map<wstring, shared_ptr<OpenBuffer>>::iterator> OpenFile(
    const OpenFileOptions& options) {
  EditorState* editor_state = options.editor_state;

  auto search_paths = std::make_shared<std::vector<Path>>(
      std::move(options.initial_search_paths));
  futures::Value<EmptyValue> search_paths_future = futures::Past(EmptyValue());
  if (options.use_search_paths) {
    search_paths_future = GetSearchPaths(editor_state, search_paths.get());
  }

  auto file_system_driver =
      std::make_shared<FileSystemDriver>(options.editor_state->work_queue());

  return futures::Transform(
      search_paths_future,
      [editor_state, options, search_paths, file_system_driver](EmptyValue) {
        return OpenFileResolvePath(editor_state, search_paths, options.path,
                                   options.ignore_if_not_found,
                                   file_system_driver);
      },
      [editor_state, options,
       file_system_driver](OpenFileResolvePathOutput input) {
        if (input.buffer.has_value()) {
          return input.buffer.value();  // Found the buffer, just return it.
        }
        auto buffer_options = std::make_shared<OpenBuffer::Options>();
        buffer_options->editor = editor_state;

        auto stat_buffer = std::make_shared<struct stat>();
        auto background_directory_reader = std::make_shared<AsyncEvaluator>(
            L"ReadDir", options.editor_state->work_queue());
        buffer_options->generate_contents =
            [editor_state, stat_buffer, file_system_driver,
             background_directory_reader](OpenBuffer* target) {
              return GenerateContents(editor_state, stat_buffer,
                                      file_system_driver,
                                      background_directory_reader, target);
            };
        buffer_options->handle_visit = [editor_state,
                                        stat_buffer](OpenBuffer* buffer) {
          HandleVisit(*stat_buffer, *buffer);
        };
        buffer_options->handle_save =
            [editor_state,
             stat_buffer](OpenBuffer::Options::HandleSaveOptions options) {
              auto buffer = options.buffer;
              return futures::OnError(
                  Save(editor_state, stat_buffer.get(), std::move(options)),
                  [buffer](Error error) {
                    buffer->status()->SetWarningText(L"🖫 Save failed: " +
                                                     error.description);
                    return error;
                  });
            };
        if (input.path.has_value()) {
          buffer_options->path = input.path.value();
        }
        buffer_options->log_supplier = [editor_state, path = input.path](
                                           WorkQueue* work_queue,
                                           Path edge_state_directory) {
          FileSystemDriver driver(work_queue);
          return NewFileLog(
              &driver,
              Path::Join(edge_state_directory,
                         PathComponent::FromString(L".edge_log").value()));
        };

        std::shared_ptr<OpenBuffer> buffer;

        if (!options.name.empty()) {
          buffer_options->name = options.name;
        } else if (buffer_options->path.has_value()) {
          buffer_options->name = buffer_options->path.value().ToString();
        } else {
          buffer_options->name =
              editor_state->GetUnusedBufferName(L"anonymous buffer");
          buffer = OpenBuffer::New(*buffer_options);
        }
        auto it =
            editor_state->buffers()->insert({buffer_options->name, buffer});
        if (it.second) {
          if (it.first->second.get() == nullptr) {
            it.first->second = OpenBuffer::New(std::move(*buffer_options));
            it.first->second->Set(buffer_variables::persist_state, true);
          }
          it.first->second->Reload();
        } else {
          it.first->second->ResetMode();
        }
        if (input.position.has_value()) {
          it.first->second->set_position(input.position.value());
        }

        editor_state->AddBuffer(it.first->second, options.insertion_type);

        if (!input.pattern.empty()) {
          SearchOptions search_options;
          search_options.starting_position = it.first->second->position();
          search_options.search_query = input.pattern;
          JumpToNextMatch(editor_state, search_options, it.first->second.get());
        }
        return it.first;
      });
}

futures::Value<std::shared_ptr<OpenBuffer>> OpenAnonymousBuffer(
    EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.insertion_type = BuffersList::AddBufferType::kIgnore;
  return futures::Transform(OpenFile(options),
                            [](auto it) { return it->second; });
}

}  // namespace editor
}  // namespace afc
