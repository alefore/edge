#include "src/directory_listing.h"

#include <regex>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/line_prompt_mode.h"

namespace afc::editor {
namespace {
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

void StartDeleteFile(EditorState& editor_state, wstring path) {
  Prompt({.editor_state = editor_state,
          .prompt = L"unlink " + path + L"? [yes/no] ",
          .history_file = L"confirmation",
          .handler =
              [&editor_state, path](const wstring input) {
                auto buffer = editor_state.current_buffer();
                Status& status = buffer == nullptr ? editor_state.status()
                                                   : buffer->status();
                if (input == L"yes") {
                  int result = unlink(ToByteString(path).c_str());
                  status.SetInformationText(
                      path + L": unlink: " +
                      (result == 0
                           ? L"done"
                           : L"ERROR: " + FromByteString(strerror(errno))));
                } else {
                  // TODO: insert it again?  Actually, only let it be erased
                  // in the other case.
                  status.SetInformationText(L"Ignored.");
                }
                if (buffer != nullptr) {
                  buffer->ResetMode();
                }
                return futures::Past(EmptyValue());
              },
          .predictor = PrecomputedPredictor({L"no", L"yes"}, '/')});
}

void AddLine(OpenBuffer& target, const dirent& entry) {
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

  target.AppendRawLine(line);
  target.contents().back()->environment()->Define(
      L"EdgeLineDeleteHandler",
      vm::NewCallback([&editor = target.editor(), path]() {
        StartDeleteFile(editor, path);
      }));
}

void ShowFiles(wstring name, std::vector<dirent> entries, OpenBuffer& target) {
  if (entries.empty()) {
    return;
  }
  std::sort(entries.begin(), entries.end(),
            [](const dirent& a, const dirent& b) {
              return strcmp(a.d_name, b.d_name) < 0;
            });

  target.AppendLine(NewLazyString(L"## " + name + L" (" +
                                  std::to_wstring(entries.size()) + L")"));
  for (auto& entry : entries) {
    AddLine(target, entry);
  }
  target.AppendEmptyLine();
}
}  // namespace

futures::Value<EmptyValue> GenerateDirectoryListing(Path path,
                                                    OpenBuffer& output) {
  output.Set(buffer_variables::atomic_lines, true);
  output.Set(buffer_variables::allow_dirty_delete, true);
  output.Set(buffer_variables::tree_parser, L"md");
  return output.editor()
      .thread_pool()
      .Run([path,
            noise_regexp = output.Read(buffer_variables::directory_noise)]() {
        return ReadDir(path, std::wregex(noise_regexp));
      })
      .Transform([&output, path](BackgroundReadDirOutput results) {
        auto disk_state_freezer = output.FreezeDiskState();
        if (results.error_description.has_value()) {
          output.status().SetInformationText(results.error_description.value());
          output.AppendLine(
              NewLazyString(std::move(results.error_description.value())));
          return EmptyValue();
        }

        output.AppendToLastLine(
            NewLazyString(L"# 🗁  File listing: " + path.ToString()));
        output.AppendEmptyLine();

        ShowFiles(L"🗁  Directories", std::move(results.directories), output);
        ShowFiles(L"🗀  Files", std::move(results.regular_files), output);
        ShowFiles(L"🗐  Noise", std::move(results.noise), output);
        return EmptyValue();
      });
}
}  // namespace afc::editor
