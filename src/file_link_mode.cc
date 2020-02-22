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
    return futures::Past(true);
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

BackgroundReadDirOutput ReadDir(std::wstring path, std::wregex noise_regex) {
  BackgroundReadDirOutput output;
  auto dir = OpenDir(path);
  if (dir == nullptr) {
    output.error_description =
        L"Unable to open directory: " + FromByteString(strerror(errno));
    return output;
  }
  struct dirent* entry;
  while ((entry = readdir(dir.get())) != nullptr) {
    auto path = FromByteString(entry->d_name);
    if (strcmp(entry->d_name, ".") == 0) {
      continue;  // Showing the link to itself is rather pointless.
    }

    if (std::regex_match(path, noise_regex)) {
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

futures::Value<bool> GenerateContents(
    EditorState* editor_state, std::shared_ptr<struct stat> stat_buffer,
    std::shared_ptr<FileSystemDriver> file_system_driver,
    std::shared_ptr<AsyncEvaluator> background_directory_reader,
    OpenBuffer* target) {
  CHECK(target->disk_state() == OpenBuffer::DiskState::kCurrent);
  const wstring path = target->Read(buffer_variables::path);
  LOG(INFO) << "GenerateContents: " << path;
  return futures::Transform(
      file_system_driver->Stat(path),
      [editor_state, stat_buffer, file_system_driver,
       background_directory_reader, target,
       path](std::optional<struct stat> stat_results) {
        if ((path.empty() || stat_results.has_value()) &&
            target->Read(buffer_variables::clear_on_reload)) {
          target->ClearContents(BufferContents::CursorsBehavior::kUnmodified);
          target->SetDiskState(OpenBuffer::DiskState::kCurrent);
        }
        if (!stat_results.has_value()) {
          return futures::Past(true);
        }
        *stat_buffer = stat_results.value();

        if (!S_ISDIR(stat_buffer->st_mode)) {
          return futures::Transform(
              file_system_driver->Open(path, O_RDONLY | O_NONBLOCK),
              [target](int fd) {
                target->SetInputFiles(fd, -1, false, -1);
                return true;
              });
        }

        target->Set(buffer_variables::atomic_lines, true);
        target->Set(buffer_variables::allow_dirty_delete, true);
        target->Set(buffer_variables::tree_parser, L"md");
        return futures::Transform(
            background_directory_reader->Run(
                [path, noise_regexp =
                           target->Read(buffer_variables::directory_noise)]() {
                  return ReadDir(path, std::wregex(noise_regexp));
                }),
            [editor_state, target, path](BackgroundReadDirOutput results) {
              auto disk_state_freezer = target->FreezeDiskState();
              if (results.error_description.has_value()) {
                target->status()->SetInformationText(
                    results.error_description.value());
                target->AppendLine(NewLazyString(
                    std::move(results.error_description.value())));
                return true;
              }

              target->AppendToLastLine(
                  NewLazyString(L"# 🗁  File listing: " + path));
              target->AppendEmptyLine();

              ShowFiles(editor_state, L"🗁  Directories",
                        std::move(results.directories), target);
              ShowFiles(editor_state, L"🗀  Files",
                        std::move(results.regular_files), target);
              ShowFiles(editor_state, L"🗐  Noise", std::move(results.noise),
                        target);
              return true;
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
  if (current_stat_buffer.st_mtime > stat_buffer.st_mtime) {
    buffer.status()->SetWarningText(L"🌷File changed in disk since last read.");
  }
}

futures::Value<std::optional<std::wstring>> Save(
    EditorState* editor_state, struct stat* stat_buffer,
    OpenBuffer::Options::HandleSaveOptions options) {
  auto buffer = options.buffer;
  std::wstring path = buffer->Read(buffer_variables::path);
  if (path.empty()) {
    return futures::Past(std::optional<std::wstring>(
        L"Buffer can't be saved: “path” variable is empty."));
  }
  if (S_ISDIR(stat_buffer->st_mode)) {
    return futures::Past(std::optional<std::wstring>(
        L"Buffer can't be saved: Buffer is a directory."));
  }

  switch (options.save_type) {
    case OpenBuffer::Options::SaveType::kMainFile:
      break;
    case OpenBuffer::Options::SaveType::kBackup:
      std::wstring error;
      auto state_directory = buffer->GetEdgeStateDirectory(&error);
      if (!state_directory.has_value()) {
        return futures::Past(
            std::optional<std::wstring>(L"Unable to backup buffer: " + error));
      }
      path = PathJoin(state_directory.value(), L"backup");
  }

  return futures::Transform(
      SaveContentsToFile(path, *buffer->contents(), buffer->status()),
      [editor_state, stat_buffer, options, buffer,
       path](ValueOrError<bool> save_contents_result) {
        if (save_contents_result.error.has_value()) {
          return futures::Past(save_contents_result.error);
        }
        return futures::Transform(
            buffer->PersistState(),
            [editor_state, stat_buffer, options, buffer,
             path](std::optional<std::wstring> persist_state_error) {
              if (persist_state_error.has_value()) {
                return futures::Past(persist_state_error);
              }
              switch (options.save_type) {
                case OpenBuffer::Options::SaveType::kMainFile:
                  buffer->status()->SetInformationText(L"🖫 Saved: " + path);
                  // TODO(easy): Move this to the caller, for symmetry with
                  // kBackup case.
                  buffer->SetDiskState(OpenBuffer::DiskState::kCurrent);
                  for (const auto& dir : editor_state->edge_path()) {
                    buffer->EvaluateFile(dir + L"/hooks/buffer-save.cc");
                  }
                  if (buffer->Read(
                          buffer_variables::trigger_reload_on_buffer_write)) {
                    for (auto& it : *editor_state->buffers()) {
                      CHECK(it.second != nullptr);
                      if (it.second->Read(
                              buffer_variables::reload_on_buffer_write)) {
                        LOG(INFO) << "Write of " << path << " triggers reload: "
                                  << it.second->Read(buffer_variables::name);
                        it.second->Reload();
                      }
                    }
                  }
                  stat(ToByteString(path).c_str(), stat_buffer);
                  break;
                case OpenBuffer::Options::SaveType::kBackup:
                  break;
              }
              return futures::Past(std::optional<std::wstring>());
            });
      });
}

static bool CanStatPath(const wstring& path) {
  CHECK(!path.empty());
  VLOG(5) << "Considering path: " << path;
  struct stat dummy;
  if (stat(ToByteString(path).c_str(), &dummy) == -1) {
    VLOG(6) << path << ": stat failed";
    return false;
  }
  VLOG(4) << "Stat succeeded: " << path;
  return true;
}

}  // namespace

using std::unique_ptr;

bool SaveContentsToOpenFile(const wstring& path, int fd,
                            const BufferContents& contents, Status* status) {
  // TODO: It'd be significant more efficient to do fewer (bigger)
  // writes.
  return contents.EveryLine([&](LineNumber position, const Line& line) {
    string str =
        (position == LineNumber(0) ? "" : "\n") + ToByteString(line.ToString());
    if (write(fd, str.c_str(), str.size()) == -1) {
      status->SetWarningText(path + L": write failed: " + std::to_wstring(fd) +
                             L": " + FromByteString(strerror(errno)));
      return false;
    }
    return true;
  });
}

futures::Value<ValueOrError<bool>> SaveContentsToFile(
    const wstring& path, const BufferContents& contents, Status* status) {
  const string path_raw = ToByteString(path);
  const string tmp_path = path_raw + ".tmp";

  struct stat original_stat;
  if (stat(path_raw.c_str(), &original_stat) == -1) {
    LOG(INFO) << "Unable to stat file (using default permissions): "
              << path_raw;
    original_stat.st_mode =
        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  }

  // TODO: Make this non-blocking.
  int fd = open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                original_stat.st_mode);
  if (fd == -1) {
    auto error = ValueOrError<bool>::Error(FromByteString(tmp_path) +
                                           L": open failed: " +
                                           FromByteString(strerror(errno)));
    status->SetWarningText(error.error.value());
    return futures::Past(std::move(error));
  }
  bool result =
      SaveContentsToOpenFile(FromByteString(tmp_path), fd, contents, status);
  close(fd);
  if (!result) {
    return futures::Past(ValueOrError<bool>::Error(L"Save failed."));
  }

  // TODO: Make this non-blocking?
  if (rename(tmp_path.c_str(), path_raw.c_str()) == -1) {
    auto error = ValueOrError<bool>::Error(path + L": rename failed: " +
                                           FromByteString(strerror(errno)));
    status->SetWarningText(error.error.value());
    return futures::Past(std::move(error));
  }

  return futures::Past(ValueOrError<bool>::Value(true));
}

shared_ptr<OpenBuffer> GetSearchPathsBuffer(EditorState* editor_state,
                                            const wstring& edge_path) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.name = L"- search paths";
  auto it = editor_state->buffers()->find(options.name);
  if (it != editor_state->buffers()->end()) {
    LOG(INFO) << "search paths buffer already existed.";
    return it->second;
  }
  options.path = edge_path + L"/search_paths";
  options.insertion_type = BuffersList::AddBufferType::kIgnore;
  options.use_search_paths = false;
  it = OpenFile(options);
  CHECK(it != editor_state->buffers()->end());
  CHECK(it->second != nullptr);
  it->second->Set(buffer_variables::save_on_close, true);
  it->second->Set(buffer_variables::trigger_reload_on_buffer_write, false);
  it->second->Set(buffer_variables::show_in_buffers_list, false);
  if (!editor_state->has_current_buffer()) {
    editor_state->set_current_buffer(it->second);
  }
  return it->second;
}

void GetSearchPaths(EditorState* editor_state, vector<wstring>* output) {
  output->push_back(L"");

  for (auto& edge_path : editor_state->edge_path()) {
    auto search_paths_buffer = GetSearchPathsBuffer(editor_state, edge_path);
    if (search_paths_buffer == nullptr) {
      LOG(INFO) << edge_path << ": No search paths buffer.";
      continue;
    }
    search_paths_buffer->contents()->ForEach(
        [editor_state, output](wstring line) {
          if (line.empty()) {
            return;
          }
          output->push_back(editor_state->expand_path(line));
          LOG(INFO) << "Pushed search path: " << output->back();
        });
  }
}

/* static */
ResolvePathOptions ResolvePathOptions::New(EditorState* editor_state) {
  auto output = NewWithEmptySearchPaths(editor_state);
  GetSearchPaths(editor_state, &output.search_paths);
  return output;
}

/* static */ ResolvePathOptions ResolvePathOptions::NewWithEmptySearchPaths(
    EditorState* editor_state) {
  ResolvePathOptions output;
  output.home_directory = editor_state->home_directory();
  output.validator = CanStatPath;
  return output;
}

std::optional<ResolvePathOutput> ResolvePath(ResolvePathOptions input) {
  ResolvePathOutput output;
  if (find(input.search_paths.begin(), input.search_paths.end(), L"") ==
      input.search_paths.end()) {
    input.search_paths.push_back(L"");
  }

  if (input.path == L"~" ||
      (input.path.size() > 2 && input.path.substr(0, 2) == L"~/")) {
    input.path = PathJoin(input.home_directory, input.path.substr(1));
  }

  if (!input.path.empty() && input.path[0] == L'/') {
    input.search_paths = {L""};
  }
  for (auto& search_path : input.search_paths) {
    for (size_t str_end = input.path.size();
         str_end != input.path.npos && str_end != 0;
         str_end = input.path.find_last_of(':', str_end - 1)) {
      wstring path_with_prefix =
          PathJoin(search_path, input.path.substr(0, str_end));

      if (!input.validator(path_with_prefix)) {
        continue;
      }

      output.pattern = L"";
      for (size_t i = 0; i < 2; i++) {
        while (str_end < input.path.size() && ':' == input.path[str_end]) {
          str_end++;
        }
        if (str_end == input.path.size()) {
          break;
        }
        size_t next_str_end = input.path.find(':', str_end);
        const wstring arg = input.path.substr(str_end, next_str_end);
        if (i == 0 && arg.size() > 0 && arg[0] == '/') {
          output.pattern = arg.substr(1);
          break;
        } else {
          size_t value;
          try {
            value = stoi(arg);
            if (value > 0) {
              value--;
            }
          } catch (const std::invalid_argument& ia) {
            LOG(INFO) << "stoi failed: invalid argument: " << arg;
            break;
          } catch (const std::out_of_range& ia) {
            LOG(INFO) << "stoi failed: out of range: " << arg;
            break;
          }
          if (!output.position.has_value()) {
            output.position = LineColumn();
          }
          if (i == 0) {
            output.position->line = LineNumber(value);
          } else {
            output.position->column = ColumnNumber(value);
          }
        }
        str_end = next_str_end;
        if (str_end == input.path.npos) {
          break;
        }
      }
      output.path = Realpath(path_with_prefix);
      VLOG(4) << "Resolved path: " << output.path;
      return output;
    }
  }
  return std::nullopt;
}

map<wstring, shared_ptr<OpenBuffer>>::iterator OpenFile(
    const OpenFileOptions& options) {
  EditorState* editor_state = options.editor_state;
  std::optional<LineColumn> position;
  wstring pattern;

  vector<wstring> search_paths = options.initial_search_paths;
  if (options.use_search_paths) {
    GetSearchPaths(editor_state, &search_paths);
  }

  auto stat_buffer = std::make_shared<struct stat>();

  OpenBuffer::Options buffer_options;
  buffer_options.editor = editor_state;

  auto file_system_driver =
      std::make_shared<FileSystemDriver>(options.editor_state->work_queue());
  auto background_directory_reader = std::make_shared<AsyncEvaluator>(
      L"ReadDir", options.editor_state->work_queue());
  buffer_options.generate_contents =
      [editor_state, stat_buffer, file_system_driver,
       background_directory_reader](OpenBuffer* target) {
        return GenerateContents(editor_state, stat_buffer, file_system_driver,
                                background_directory_reader, target);
      };
  buffer_options.handle_visit = [editor_state,
                                 stat_buffer](OpenBuffer* buffer) {
    HandleVisit(*stat_buffer, *buffer);
  };
  buffer_options.handle_save =
      [editor_state,
       stat_buffer](OpenBuffer::Options::HandleSaveOptions options) {
        auto buffer = options.buffer;
        return futures::Transform(
            Save(editor_state, stat_buffer.get(), std::move(options)),
            [buffer](std::optional<std::wstring> error) {
              if (error.has_value()) {
                buffer->status()->SetInformationText(error.value());
              }
              return error;
            });
      };

  auto resolve_path_options =
      ResolvePathOptions::NewWithEmptySearchPaths(editor_state);
  resolve_path_options.home_directory = editor_state->home_directory();
  resolve_path_options.search_paths = search_paths;
  resolve_path_options.path = options.path;
  if (auto output = ResolvePath(resolve_path_options); output.has_value()) {
    buffer_options.path = output->path;
    position = output->position;
    pattern = output->pattern.value_or(L"");
  } else {
    map<wstring, shared_ptr<OpenBuffer>>::iterator buffer;
    resolve_path_options.validator = [editor_state,
                                      &buffer](const wstring& path) {
      DCHECK(!path.empty());
      for (auto it = editor_state->buffers()->begin();
           it != editor_state->buffers()->end(); ++it) {
        CHECK(it->second != nullptr);
        auto buffer_path = it->second->Read(buffer_variables::path);
        if (buffer_path.size() >= path.size() &&
            buffer_path.substr(buffer_path.size() - path.size()) == path &&
            (buffer_path.size() == path.size() || path[0] == L'/' ||
             buffer_path[buffer_path.size() - path.size() - 1] == L'/')) {
          buffer = it;
          return true;
        }
      }
      return false;
    };
    resolve_path_options.search_paths = {L""};
    if (auto output = ResolvePath(resolve_path_options); output.has_value()) {
      buffer_options.path = output->path;
      editor_state->set_current_buffer(buffer->second);
      if (output->position.has_value()) {
        buffer->second->set_position(output->position.value());
      }
      // TODO: Apply pattern.
      return buffer;
    }
    if (options.ignore_if_not_found) {
      return editor_state->buffers()->end();
    }
    buffer_options.path = options.path;
  }

  shared_ptr<OpenBuffer> buffer;

  if (!options.name.empty()) {
    buffer_options.name = options.name;
  } else if (buffer_options.path.empty()) {
    buffer_options.name =
        editor_state->GetUnusedBufferName(L"anonymous buffer");
    buffer = OpenBuffer::New(buffer_options);
  } else {
    buffer_options.name = buffer_options.path;
  }
  auto it = editor_state->buffers()->insert({buffer_options.name, buffer});
  if (it.second) {
    if (it.first->second.get() == nullptr) {
      it.first->second = OpenBuffer::New(std::move(buffer_options));
      it.first->second->Set(buffer_variables::persist_state, true);
    }
    it.first->second->Reload();
  } else {
    it.first->second->ResetMode();
  }
  if (position.has_value()) {
    it.first->second->set_position(position.value());
  }

  editor_state->AddBuffer(it.first->second, options.insertion_type);

  if (!pattern.empty()) {
    SearchOptions search_options;
    search_options.buffer = it.first->second.get();
    search_options.starting_position = it.first->second->position();
    search_options.search_query = pattern;
    JumpToNextMatch(editor_state, search_options);
  }
  return it.first;
}

std::shared_ptr<OpenBuffer> OpenAnonymousBuffer(EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.insertion_type = BuffersList::AddBufferType::kIgnore;
  return OpenFile(options)->second;
}

}  // namespace editor
}  // namespace afc
