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
  options.prompt = L"unlink " + path + L"? [yes/no] ",
  options.history_file = L"confirmation";
  options.handler = [path](const wstring input, EditorState* editor_state) {
    if (input == L"yes") {
      int result = unlink(ToByteString(path).c_str());
      editor_state->SetStatus(
          path + L": unlink: " +
          (result == 0 ? L"done"
                       : L"ERROR: " + FromByteString(strerror(errno))));
    } else {
      // TODO: insert it again?  Actually, only let it be erased
      // in the other case.
      editor_state->SetStatus(L"Ignored.");
    }
    auto buffer = editor_state->current_buffer();
    if (buffer != nullptr) {
      buffer->ResetMode();
    }
  };
  options.predictor = PrecomputedPredictor({L"no", L"yes"}, '/');
  Prompt(editor_state, std::move(options));
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
  line_options.modifiers =
      std::vector<LineModifierSet>(line_options.contents->size());
  if (!type_it->second.modifiers.empty()) {
    for (size_t i = 0; i < path.size(); i++) {
      line_options.modifiers[i] = (type_it->second.modifiers);
    }
  }

  auto line = std::make_shared<Line>(std::move(line_options));

  target->AppendRawLine(line);
  target->contents()->back()->environment()->Define(
      L"EdgeLineDeleteHandler",
      vm::NewCallback(std::function<void()>(
          [editor_state, path]() { StartDeleteFile(editor_state, path); })));
}

void ShowFiles(EditorState* editor_state, wstring name,
               std::vector<dirent> entries, OpenBuffer* target) {
  if (entries.empty()) {
    return;
  }
  target->AppendLine(NewLazyString(L"## " + name));
  int start = target->contents()->size();
  for (auto& entry : entries) {
    AddLine(editor_state, target, entry);
  }
  target->SortContents(
      start, target->contents()->size(),
      [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
        return *a->contents() < *b->contents();
      });
  target->AppendEmptyLine();
}

void GenerateContents(EditorState* editor_state, struct stat* stat_buffer,
                      OpenBuffer* target) {
  CHECK(!target->modified());
  const wstring path = target->Read(buffer_variables::path);
  LOG(INFO) << "GenerateContents: " << path;
  const string path_raw = ToByteString(path);
  if (!path.empty() && stat(path_raw.c_str(), stat_buffer) == -1) {
    return;
  }

  if (target->Read(buffer_variables::clear_on_reload)) {
    target->ClearContents(BufferContents::CursorsBehavior::kUnmodified);
    target->ClearModified();
  }

  editor_state->ScheduleRedraw();

  if (path.empty()) {
    return;
  }

  if (!S_ISDIR(stat_buffer->st_mode)) {
    char* tmp = strdup(path_raw.c_str());
    if (0 == strcmp(basename(tmp), "passwd")) {
      RunCommandHandler(L"parsers/passwd <" + path, editor_state, {});
    } else {
      int fd = open(ToByteString(path).c_str(), O_RDONLY | O_NONBLOCK);
      target->SetInputFiles(fd, -1, false, -1);
    }
    return;
  }

  target->Set(buffer_variables::atomic_lines, true);
  target->Set(buffer_variables::allow_dirty_delete, true);
  target->Set(buffer_variables::tree_parser, L"md");

  DIR* dir = opendir(path_raw.c_str());
  if (dir == nullptr) {
    auto description =
        L"Unable to open directory: " + FromByteString(strerror(errno));
    editor_state->SetStatus(description);
    target->AppendLine(NewLazyString(std::move(description)));
    return;
  }
  struct dirent* entry;

  std::vector<dirent> directories;
  std::vector<dirent> regular_files;
  std::vector<dirent> noise;

  std::wregex noise_regex(target->Read(buffer_variables::directory_noise));

  while ((entry = readdir(dir)) != nullptr) {
    auto path = FromByteString(entry->d_name);
    if (strcmp(entry->d_name, ".") == 0) {
      continue;  // Showing the link to itself is rather pointless.
    }

    if (std::regex_match(path, noise_regex)) {
      noise.push_back(*entry);
      continue;
    }

    if (entry->d_type == DT_DIR) {
      directories.push_back(*entry);
      continue;
    }

    regular_files.push_back(*entry);
  }
  closedir(dir);

  target->AppendToLastLine(NewLazyString(L"# 🗁  File listing: " + path));
  target->AppendEmptyLine();

  ShowFiles(editor_state, L"🗁  Directories", std::move(directories), target);
  ShowFiles(editor_state, L"🗀  Files", std::move(regular_files), target);
  ShowFiles(editor_state, L"🗐  Noise", std::move(noise), target);

  target->ClearModified();
}

void HandleVisit(EditorState* editor_state, const struct stat& stat_buffer,
                 const OpenBuffer& buffer) {
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
    editor_state->SetWarningStatus(L"🌷File changed in disk since last read.");
  }
}

void Save(EditorState* editor_state, struct stat* stat_buffer,
          OpenBuffer* buffer) {
  const wstring path = buffer->Read(buffer_variables::path);
  if (path.empty()) {
    editor_state->SetStatus(
        L"Buffer can't be saved: “path” variable is empty.");
    return;
  }
  if (S_ISDIR(stat_buffer->st_mode)) {
    editor_state->SetStatus(L"Buffer can't be saved: Buffer is a directory.");
    return;
  }

  if (!SaveContentsToFile(editor_state, path, *buffer->contents()) ||
      !buffer->PersistState()) {
    LOG(INFO) << "Saving failed.";
    return;
  }
  buffer->ClearModified();
  editor_state->SetStatus(L"🖫 Saved: " + path);
  for (const auto& dir : editor_state->edge_path()) {
    buffer->EvaluateFile(dir + L"/hooks/buffer-save.cc",
                         [](std::unique_ptr<Value>) {});
  }
  if (buffer->Read(buffer_variables::trigger_reload_on_buffer_write)) {
    for (auto& it : *editor_state->buffers()) {
      CHECK(it.second != nullptr);
      if (it.second->Read(buffer_variables::reload_on_buffer_write)) {
        LOG(INFO) << "Write of " << path << " triggers reload: "
                  << it.second->Read(buffer_variables::name);
        it.second->Reload();
      }
    }
  }
  stat(ToByteString(path).c_str(), stat_buffer);
}

static wstring realpath_safe(const wstring& path) {
  char* result = realpath(ToByteString(path).c_str(), nullptr);
  return result == nullptr ? path : FromByteString(result);
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

bool FindPath(EditorState* editor_state, vector<wstring> search_paths,
              wstring path, std::function<bool(const wstring&)> validator,
              wstring* resolved_path, std::optional<LineColumn>* position,
              wstring* pattern) {
  std::optional<LineColumn> position_dummy;
  if (position == nullptr) {
    position = &position_dummy;
  }

  wstring pattern_dummy;
  if (pattern == nullptr) {
    pattern = &pattern_dummy;
  }

  if (find(search_paths.begin(), search_paths.end(), L"") ==
      search_paths.end()) {
    search_paths.push_back(L"");
  }

  path = editor_state->expand_path(path);
  if (!path.empty() && path[0] == L'/') {
    search_paths = {L""};
  }
  for (auto search_path_it = search_paths.begin();
       search_path_it != search_paths.end(); ++search_path_it) {
    for (size_t str_end = path.size(); str_end != path.npos && str_end != 0;
         str_end = path.find_last_of(':', str_end - 1)) {
      wstring path_with_prefix =
          *search_path_it +
          (!search_path_it->empty() && *search_path_it->rbegin() != L'/'
               ? L"/"
               : L"") +
          path.substr(0, str_end);

      if (!validator(path_with_prefix)) {
        continue;
      }

      *pattern = L"";
      for (size_t i = 0; i < 2; i++) {
        while (str_end < path.size() && ':' == path[str_end]) {
          str_end++;
        }
        if (str_end == path.size()) {
          break;
        }
        size_t next_str_end = path.find(':', str_end);
        const wstring arg = path.substr(str_end, next_str_end);
        if (i == 0 && arg.size() > 0 && arg[0] == '/') {
          *pattern = arg.substr(1);
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
          if (!position->has_value()) {
            *position = LineColumn();
          }
          (i == 0 ? position->value().line : position->value().column) = value;
        }
        str_end = next_str_end;
        if (str_end == path.npos) {
          break;
        }
      }
      *resolved_path = realpath_safe(path_with_prefix);
      VLOG(4) << "Resolved path: " << *resolved_path;
      return true;
    }
  }
  return false;
}

static bool FindPath(EditorState* editor_state, vector<wstring> search_paths,
                     const wstring& path, wstring* resolved_path,
                     std::optional<LineColumn>* position, wstring* pattern) {
  return FindPath(editor_state, search_paths, path, CanStatPath, resolved_path,
                  position, pattern);
}

}  // namespace

using std::unique_ptr;

bool SaveContentsToOpenFile(EditorState* editor_state, const wstring& path,
                            int fd, const BufferContents& contents) {
  // TODO: It'd be significant more efficient to do fewer (bigger) writes.
  return contents.EveryLine([editor_state, fd, path](size_t position,
                                                     const Line& line) {
    string str = (position == 0 ? "" : "\n") + ToByteString(line.ToString());
    if (write(fd, str.c_str(), str.size()) == -1) {
      editor_state->SetStatus(path + L": write failed: " + std::to_wstring(fd) +
                              L": " + FromByteString(strerror(errno)));
      return false;
    }
    return true;
  });
}

bool SaveContentsToFile(EditorState* editor_state, const wstring& path,
                        const BufferContents& contents) {
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
    editor_state->SetStatus(FromByteString(tmp_path) + L": open failed: " +
                            FromByteString(strerror(errno)));
    return false;
  }
  bool result = SaveContentsToOpenFile(editor_state, FromByteString(tmp_path),
                                       fd, contents);
  close(fd);
  if (!result) {
    return false;
  }

  // TODO: Make this non-blocking?
  if (rename(tmp_path.c_str(), path_raw.c_str()) == -1) {
    editor_state->SetStatus(path + L": rename failed: " +
                            FromByteString(strerror(errno)));
    return false;
  }

  return true;
}

shared_ptr<OpenBuffer> GetSearchPathsBuffer(EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.name = L"- search paths";
  auto it = editor_state->buffers()->find(options.name);
  if (it != editor_state->buffers()->end()) {
    LOG(INFO) << "search paths buffer already existed.";
    return it->second;
  }
  options.path = (*editor_state->edge_path().begin()) + L"/search_paths";
  options.insertion_type = BufferTreeHorizontal::InsertionType::kSkip;
  options.use_search_paths = false;
  it = OpenFile(options);
  CHECK(it != editor_state->buffers()->end());
  CHECK(it->second != nullptr);
  it->second->Set(buffer_variables::save_on_close, true);
  it->second->Set(buffer_variables::trigger_reload_on_buffer_write, false);
  it->second->Set(buffer_variables::show_in_buffers_list, false);
  if (!editor_state->has_current_buffer()) {
    editor_state->set_current_buffer(it->second);
    editor_state->ScheduleRedraw();
  }
  return it->second;
}

void GetSearchPaths(EditorState* editor_state, vector<wstring>* output) {
  output->push_back(L"");
  auto search_paths_buffer = GetSearchPathsBuffer(editor_state);
  if (search_paths_buffer == nullptr) {
    LOG(INFO) << "No search paths buffer.";
    return;
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

bool ResolvePath(ResolvePathOptions options) {
  vector<wstring> search_paths;
  GetSearchPaths(options.editor_state, &search_paths);
  return FindPath(options.editor_state, std::move(search_paths), options.path,
                  options.validator ? options.validator : CanStatPath,
                  options.output_path, options.output_position,
                  options.output_pattern);
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
  buffer_options.editor_state = editor_state;
  buffer_options.generate_contents = [editor_state,
                                      stat_buffer](OpenBuffer* target) {
    GenerateContents(editor_state, stat_buffer.get(), target);
  };
  buffer_options.handle_visit = [editor_state,
                                 stat_buffer](OpenBuffer* buffer) {
    HandleVisit(editor_state, *stat_buffer, *buffer);
  };
  buffer_options.handle_save = [editor_state, stat_buffer](OpenBuffer* buffer) {
    Save(editor_state, stat_buffer.get(), buffer);
  };

  FindPath(editor_state, search_paths, options.path, &buffer_options.path,
           &position, &pattern);

  if (buffer_options.path.empty()) {
    map<wstring, shared_ptr<OpenBuffer>>::iterator buffer;
    auto validator = [editor_state, &buffer](const wstring& path) {
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
    if (FindPath(editor_state, {L""}, options.path, validator,
                 &buffer_options.path, &position, &pattern)) {
      editor_state->set_current_buffer(buffer->second);
      if (position.has_value()) {
        buffer->second->set_position(position.value());
      }
      // TODO: Apply pattern.
      editor_state->ScheduleRedraw();
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
    buffer = std::make_shared<OpenBuffer>(buffer_options);
  } else {
    buffer_options.name = buffer_options.path;
  }
  auto it = editor_state->buffers()->insert({buffer_options.name, buffer});
  if (it.second) {
    if (it.first->second.get() == nullptr) {
      it.first->second = std::make_shared<OpenBuffer>(buffer_options);
      it.first->second->Set(buffer_variables::persist_state, true);
    }
    it.first->second->Reload();
  } else {
    it.first->second->ResetMode();
  }
  if (position.has_value()) {
    it.first->second->set_position(position.value());
  }
  editor_state->buffer_tree()->InsertChildren(it.first->second,
                                              options.insertion_type);
  editor_state->ScheduleRedraw();
  if (!pattern.empty()) {
    SearchOptions search_options;
    search_options.starting_position = it.first->second->position();
    search_options.search_query = pattern;
    JumpToNextMatch(editor_state, search_options);
  }
  return it.first;
}

std::shared_ptr<OpenBuffer> OpenAnonymousBuffer(EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  options.insertion_type = BufferTreeHorizontal::InsertionType::kSkip;
  return OpenFile(options)->second;
}

}  // namespace editor
}  // namespace afc
