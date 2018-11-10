#include "file_link_mode.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

extern "C" {
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
}

#include "char_buffer.h"
#include "dirname.h"
#include "editor.h"
#include "line_prompt_mode.h"
#include "run_command_handler.h"
#include "search_handler.h"
#include "server.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace afc {
namespace editor {
namespace {
using std::make_pair;
using std::shared_ptr;
using std::unique_ptr;
using std::sort;

void StartDeleteFile(EditorState* editor_state, wstring path) {
  PromptOptions options;
  options.prompt = L"unlink " + path + L"? [yes/no] ",
  options.history_file = L"confirmation";
  options.handler = [path](const wstring input,
                           EditorState* editor_state) {
    if (input == L"yes") {
      int result = unlink(ToByteString(path).c_str());
      editor_state->SetStatus(
          path + L": unlink: "
          + (result == 0
             ? L"done"
             : L"ERROR: " + FromByteString(strerror(errno))));
    } else {
      // TODO: insert it again?  Actually, only let it be erased
      // in the other case.
      editor_state->SetStatus(L"Ignored.");
    }
    if (editor_state->has_current_buffer()) {
      editor_state->current_buffer()->second->ResetMode();
    }
  };
  options.predictor = PrecomputedPredictor({L"no", L"yes"}, '/');
  Prompt(editor_state, std::move(options));
}

class FileBuffer : public OpenBuffer {
 public:
  FileBuffer(EditorState* editor_state, const wstring& path,
             const wstring& name)
      : OpenBuffer(editor_state, name) {
    set_string_variable(variable_path(), path);
  }

  void Visit(EditorState* editor_state) {
    OpenBuffer::Visit(editor_state);

    LOG(INFO) << "Checking if file has changed.";
    const wstring path = GetPath();
    const string path_raw = ToByteString(path);
    struct stat current_stat_buffer;
    if (stat(path_raw.c_str(), &current_stat_buffer) == -1) {
      return;
    }
    if (current_stat_buffer.st_mtime > stat_buffer_.st_mtime) {
      editor_state->SetWarningStatus(
          L"WARNING: File (in disk) changed since last read.");
    }
  }

  bool PersistState() const override {
    auto path_vector = editor_->edge_path();
    if (path_vector.empty()) {
      LOG(INFO) << "Empty edge path.";
      return false;
    }

    auto file_path = read_string_variable(variable_path());
    list<wstring> file_path_components;
    if (file_path.empty() || file_path[0] != '/') {
      LOG(INFO) << "Empty edge path.";
      return false;
    }

    if (!DirectorySplit(file_path, &file_path_components)) {
      LOG(INFO) << "Unable to split path: " << file_path;
      return false;
    }

    file_path_components.push_front(L"state");

    wstring path = path_vector[0];
    LOG(INFO) << "PersistState: Preparing directory for state: " << path;
    for (auto& component : file_path_components) {
      path = PathJoin(path, component);
      struct stat stat_buffer;
      auto path_byte_string = ToByteString(path);
      if (stat(path_byte_string.c_str(), &stat_buffer) != -1) {
        if (S_ISDIR(stat_buffer.st_mode)) {
          continue;
        }
        LOG(INFO) << "Ooops, exists, but is not a directory: " << path;
        return false;
      }
      if (mkdir(path_byte_string.c_str(), 0700)) {
        editor_->SetStatus(
            L"mkdir: " + FromByteString(strerror(errno)) + L": " + path);
        return false;
      }
    }

    path = PathJoin(path, L".edge_state");
    LOG(INFO) << "PersistState: Preparing state file: " << path;
    BufferContents contents;
    contents.push_back(L"// State of file: " + path);
    contents.push_back(L"");

    contents.push_back(
        L"buffer.set_position(" + position().ToCppString() + L");");
    contents.push_back(L"");

    contents.push_back(L"// String variables");
    for (const auto& variable : OpenBuffer::StringStruct()->variables()) {
      contents.push_back(
          L"buffer.set_" + variable.first + L"(\"" +
          CppEscapeString(read_string_variable(variable.second.get()))
          + L"\");");
    }
    contents.push_back(L"");

    contents.push_back(L"// Int variables");
    for (const auto& variable : OpenBuffer::IntStruct()->variables()) {
      contents.push_back(
          L"buffer.set_" + variable.first + L"(" +
          std::to_wstring(Read(variable.second.get())) + L");");
    }
    contents.push_back(L"");

    contents.push_back(L"// Bool variables");
    for (const auto& variable : OpenBuffer::BoolStruct()->variables()) {
      contents.push_back(
          L"buffer.set_" + variable.first + L"(" +
          (read_bool_variable(variable.second.get()) ? L"true" : L"false")
          + L");");
    }
    contents.push_back(L"");

    return SaveContentsToFile(editor_, path, contents);
  }

  void ReloadInto(EditorState* editor_state, OpenBuffer* target) {
    CHECK(!target->modified());
    const wstring path = GetPath();
    LOG(INFO) << "ReloadInto: " << path;
    const string path_raw = ToByteString(path);
    if (!path.empty() && stat(path_raw.c_str(), &stat_buffer_) == -1) {
      return;
    }

    if (target->read_bool_variable(OpenBuffer::variable_clear_on_reload())) {
      target->ClearContents(editor_state);
      target->ClearModified();
    }

    editor_state->ScheduleRedraw();

    if (path.empty()) {
      return;
    }

    if (!S_ISDIR(stat_buffer_.st_mode)) {
      char* tmp = strdup(path_raw.c_str());
      if (0 == strcmp(basename(tmp), "passwd")) {
        RunCommandHandler(L"parsers/passwd <" + path, editor_state);
      } else {
        int fd = open(ToByteString(path).c_str(), O_RDONLY | O_NONBLOCK);
        target->SetInputFiles(editor_state, fd, -1, false, -1);
      }
      editor_state->CheckPosition();
      editor_state->PushCurrentPosition();
      return;
    }

    set_bool_variable(variable_atomic_lines(), true);
    set_bool_variable(variable_allow_dirty_delete(), true);
    target->AppendToLastLine(editor_state,
        NewCopyString(L"File listing: " + path));

    DIR* dir = opendir(path_raw.c_str());
    if (dir == nullptr) {
      auto description =
          L"Unable to open directory: " + FromByteString(strerror(errno));
      editor_state->SetStatus(description);
      target->AppendLine(
          editor_state, shared_ptr<LazyString>(NewCopyString(description)));
      return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
      static std::unordered_map<int, wstring> types = {
          { DT_BLK, L" (block dev)" },
          { DT_CHR, L" (char dev)" },
          { DT_DIR, L"/" },
          { DT_FIFO, L" (named pipe)" },
          { DT_LNK, L"@" },
          { DT_REG, L"" },
          { DT_SOCK, L" (unix sock)" }
      };
      if (strcmp(entry->d_name, ".") == 0) {
        continue;  // Showing the link to itself is rather pointless.
      }
      auto type_it = types.find(entry->d_type);
      auto path = FromByteString(entry->d_name);
      target->AppendLine(editor_state, shared_ptr<LazyString>(
          NewCopyString(
              path + (type_it == types.end() ? L"" : type_it->second))));

      target->contents()->back()->environment()->Define(
          L"EdgeLineDeleteHandler", Value::NewFunction({ VMType::Void() },
              [editor_state, path](
                  vector<Value::Ptr> args, OngoingEvaluation *evaluation) {
                CHECK_EQ(args.size(), size_t(0));
                StartDeleteFile(editor_state, path);
                evaluation->return_consumer(Value::NewVoid());
              }));
    }
    closedir(dir);

    target->SortContents(1, target->contents()->size(),
        [](const shared_ptr<const Line>& a, const shared_ptr<const Line>& b) {
          return *a->contents() < *b->contents();
        });
    target->ClearModified();
    editor_state->CheckPosition();
    editor_state->PushCurrentPosition();
  }

  void Save(EditorState* editor_state) {
    const wstring path = GetPath();
    if (path.empty()) {
      OpenBuffer::Save(editor_state);
      editor_state->SetStatus(
          L"Buffer can't be saved: “path” variable is empty.");
      return;
    }
    if (S_ISDIR(stat_buffer_.st_mode)) {
      OpenBuffer::Save(editor_state);
      return;
    }

    if (!SaveContentsToFile(editor_state, path, contents_) || !PersistState()) {
      LOG(INFO) << "Saving failed.";
      return;
    }
    ClearModified();
    editor_state->SetStatus(L"Saved: " + path);
    for (const auto& dir : editor_state->edge_path()) {
      EvaluateFile(editor_state, dir + L"/hooks/buffer-save.cc");
    }
    for (auto& it : *editor_state->buffers()) {
      CHECK(it.second != nullptr);
      if (it.second->read_bool_variable(
              OpenBuffer::variable_reload_on_buffer_write())) {
        LOG(INFO) << "Write of " << path << " triggers reload: "
                  << it.second->name();
        it.second->Reload(editor_state);
      }
    }
    stat(ToByteString(path).c_str(), &stat_buffer_);
  }

 private:
  static bool SaveContentsToFile(
      EditorState* editor_state,
      const wstring& path, const BufferContents& contents) {
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
      editor_state->SetStatus(
          FromByteString(tmp_path) + L": open failed: "
          + FromByteString(strerror(errno)));
      return false;
    }
    bool result = SaveContentsToOpenFile(
        editor_state, FromByteString(tmp_path), fd, contents);
    close(fd);
    if (!result) {
      return false;
    }

    // TODO: Make this non-blocking?
    if (rename(tmp_path.c_str(), path_raw.c_str()) == -1) {
      editor_state->SetStatus(
          path + L": rename failed: " + FromByteString(strerror(errno)));
      return false;
    }

    return true;
  }

  static bool SaveContentsToOpenFile(
      EditorState* editor_state, const wstring& path, int fd,
      const BufferContents& contents) {
    // TODO: It'd be significant more efficient to do fewer (bigger) writes.
    return contents.ForEach(
        [editor_state, fd, path](size_t position, const Line& line) {
          string str = (position == 0 ? "" : "\n")
              + ToByteString(line.ToString());
          if (write(fd, str.c_str(), str.size()) == -1) {
            editor_state->SetStatus(
                path + L": write failed: " + std::to_wstring(fd) + L": "
            + FromByteString(strerror(errno)));
            return false;
          }
          return true;
        });
  }

  wstring GetPath() const {
    return read_string_variable(variable_path());
  }

  struct stat stat_buffer_;
};

static wstring realpath_safe(const wstring& path) {
  char* result = realpath(ToByteString(path).c_str(), nullptr);
  return result == nullptr ? path : FromByteString(result);
}

wstring GetAnonymousBufferName(size_t i) {
  return L"[anonymous buffer " + std::to_wstring(i) + L"]";
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

bool FindPath(
    EditorState* editor_state, vector<wstring> search_paths,
    wstring path, std::function<bool(const wstring&)> validator,
    wstring* resolved_path, LineColumn* position, wstring* pattern) {
  LineColumn position_dummy;
  if (position == nullptr) {
    position = &position_dummy;
  }

  wstring pattern_dummy;
  if (pattern == nullptr) {
    pattern = &pattern_dummy;
  }

  if (find(search_paths.begin(), search_paths.end(), L"")
          == search_paths.end()) {
    search_paths.push_back(L"");
  }

  path = editor_state->expand_path(path);
  if (!path.empty() && path[0] == L'/') {
    search_paths = {L""};
  }
  for (auto search_path_it = search_paths.begin();
       search_path_it != search_paths.end();
       ++search_path_it) {
    for (size_t str_end = path.size(); str_end != path.npos && str_end != 0;
         str_end = path.find_last_of(':', str_end - 1)) {
      wstring path_with_prefix =
          *search_path_it
          + (!search_path_it->empty() && *search_path_it->rbegin() != L'/'
                 ? L"/" : L"")
          + path.substr(0, str_end);

      if (!validator(path_with_prefix)) {
        continue;
      }

      *position = LineColumn();
      *pattern = L"";
      for (size_t i = 0; i < 2; i++) {
        while (str_end < path.size() && ':' == path[str_end]) {
          str_end++;
        }
        if (str_end == path.size()) { break; }
        size_t next_str_end = path.find(':', str_end);
        const wstring arg = path.substr(str_end, next_str_end);
        if (i == 0 && arg.size() > 0 && arg[0] == '/') {
          *pattern = arg.substr(1);
          break;
        } else {
          size_t value;
          try {
            value = stoi(arg);
            if (value > 0) {value--; }
          } catch (const std::invalid_argument& ia) {
            LOG(INFO) << "stoi failed: invalid argument: " << arg;
            break;
          } catch (const std::out_of_range& ia) {
            LOG(INFO) << "stoi failed: out of range: " << arg;
            break;
          }
          (i == 0 ? position->line : position->column) = value;
        }
        str_end = next_str_end;
        if (str_end == path.npos) { break; }
      }
      *resolved_path = realpath_safe(path_with_prefix);
      VLOG(4) << "Resolved path: " << *resolved_path;
      return true;
    }
  }
  return false;
}

static bool FindPath(
    EditorState* editor_state, vector<wstring> search_paths,
    const wstring& path, wstring* resolved_path, LineColumn* position,
    wstring* pattern) {
  return FindPath(editor_state, search_paths, path, CanStatPath, resolved_path,
                  position, pattern);
}

}  // namespace

using std::unique_ptr;

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
  options.make_current_buffer = false;
  options.use_search_paths = false;
  it = OpenFile(options);
  assert(it != editor_state->buffers()->end());
  assert(it->second != nullptr);
  it->second->set_bool_variable(OpenBuffer::variable_save_on_close(), true);
  it->second->set_bool_variable(
      OpenBuffer::variable_show_in_buffers_list(), false);
  if (!editor_state->has_current_buffer()) {
    editor_state->set_current_buffer(it);
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
  search_paths_buffer->ForEachLine(
      [editor_state, output](wstring line) {
        if (line.empty()) { return; }
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
  LineColumn position;
  wstring pattern;

  vector<wstring> search_paths = options.initial_search_paths;
  if (options.use_search_paths) {
    GetSearchPaths(editor_state, &search_paths);
  }
  wstring actual_path;
  FindPath(editor_state, search_paths, options.path, &actual_path, &position,
           &pattern);

  if (actual_path.empty()) {
    map<wstring, shared_ptr<OpenBuffer>>::iterator buffer;
    auto validator = [editor_state, &buffer](const wstring& path) {
      DCHECK(!path.empty());
      for (auto it = editor_state->buffers()->begin();
           it != editor_state->buffers()->end(); ++it) {
        CHECK(it->second != nullptr);
        auto buffer_path =
            it->second->read_string_variable(OpenBuffer::variable_path());
        if (buffer_path.size() >= path.size() &&
            buffer_path.substr(buffer_path.size() - path.size()) == path &&
            (buffer_path.size() == path.size() ||
             path[0] == L'/' ||
             buffer_path[buffer_path.size() - path.size() - 1] == L'/')) {
          buffer = it;
          return true;
        }
      }
      return false;
    };
    if (FindPath(editor_state, {L""}, options.path, validator,
                 &actual_path, &position, &pattern)) {
      editor_state->set_current_buffer(buffer);
      buffer->second->set_position(position);
      // TODO: Apply pattern.
      editor_state->ScheduleRedraw();
      return buffer;
    }
    if (options.ignore_if_not_found) {
      return editor_state->buffers()->end();
    }
    actual_path = options.path;
  }

  shared_ptr<OpenBuffer> buffer;
  wstring name;
  if (!options.name.empty()) {
    name = options.name;
  } else if (actual_path.empty()) {
    size_t count = 0;
    while (editor_state->buffers()->find(GetAnonymousBufferName(count))
           != editor_state->buffers()->end()) {
      count++;
    }
    name = GetAnonymousBufferName(count);
    buffer = std::make_shared<FileBuffer>(editor_state, actual_path, name);
  } else {
    name = actual_path;
  }
  auto it = editor_state->buffers()->insert(make_pair(name, buffer));
  if (it.second) {
    if (it.first->second.get() == nullptr) {
      it.first->second =
          std::make_shared<FileBuffer>(editor_state, actual_path, name);
    }
    it.first->second->Reload(editor_state);
  } else {
    it.first->second->ResetMode();
  }
  editor_state->PushCurrentPosition();
  it.first->second->set_position(position);
  if (options.make_current_buffer) {
    editor_state->set_current_buffer(it.first);
    editor_state->ScheduleRedraw();
  }
  if (!pattern.empty()) {
    SearchOptions search_options;
    search_options.starting_position = it.first->second->position();
    search_options.search_query = pattern;
    JumpToNextMatch(editor_state, search_options);
  }
  return it.first;
}

map<wstring, shared_ptr<OpenBuffer>>::iterator OpenAnonymousBuffer(
    EditorState* editor_state) {
  OpenFileOptions options;
  options.editor_state = editor_state;
  return OpenFile(options);
}

}  // namespace afc
}  // namespace editor
