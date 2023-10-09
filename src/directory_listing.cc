#include "src/directory_listing.h"

#include <regex>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/language//safe_types.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/line_prompt_mode.h"
#include "src/vm/constant_expression.h"
#include "src/vm/function_call.h"

namespace afc::editor {
using infrastructure::OpenDir;
using infrastructure::Path;
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using language::EmptyValue;
using language::Error;
using language::FromByteString;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Observers;
using language::Success;
using language::ToByteString;
using language::ValueOrError;
using language::lazy_string::Append;
using language::lazy_string::ColumnNumber;
using language::lazy_string::EmptyString;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;
using language::text::LineSequence;
using language::text::MutableLineSequence;
using vm::Environment;
using vm::Expression;
using vm::Type;

namespace gc = language::gc;

namespace {
struct BackgroundReadDirOutput {
  std::vector<dirent> directories;
  std::vector<dirent> regular_files;
  std::vector<dirent> noise;
};

ValueOrError<BackgroundReadDirOutput> ReadDir(Path path,
                                              std::wregex noise_regex) {
  TRACK_OPERATION(GenerateDirectoryListing_ReadDir);
  BackgroundReadDirOutput output;
  auto dir = OpenDir(path.read());
  if (dir == nullptr) {
    return Error(L"Unable to open directory: " +
                 FromByteString(strerror(errno)));
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

void StartDeleteFile(EditorState& editor_state, std::wstring path) {
  int result = unlink(ToByteString(path).c_str());
  editor_state.status().SetInformationText(MakeNonNullShared<Line>(
      LineBuilder(Append(NewLazyString(path), NewLazyString(L": unlink: "),
                         NewLazyString(result == 0 ? L"done"
                                                   : L"ERROR: " +
                                                         FromByteString(
                                                             strerror(errno)))))
          .Build()));
}

#if 0
// This is disable because we don't seem to have found any use for it. By
// disabling it, we are able to construct all the contents in the background
// thread, which matters when generating views for very large directories.
language::text::LineMetadataEntry GetMetadata(OpenBuffer& target,
                                              std::wstring path) {
  VLOG(6) << "Get metadata for: " << path;
  std::optional<gc::Root<vm::Value>> callback = target.environment()->Lookup(
      target.editor().gc_pool(), vm::Namespace(), L"GetPathMetadata",
      vm::types::Function{.output = vm::Type{vm::types::String{}},
                          .inputs = {vm::types::String{}}});
  if (!callback.has_value()) {
    VLOG(5) << "Unable to find suitable GetPathMetadata definition";
    return {
        .initial_value = EmptyString(),
        .value = futures::Future<NonNull<std::shared_ptr<LazyString>>>().value};
  }

  std::vector<NonNull<std::shared_ptr<Expression>>> args;
  args.push_back(vm::NewConstantExpression(
      {vm::Value::NewString(target.editor().gc_pool(), path)}));
  NonNull<std::unique_ptr<Expression>> expression = vm::NewFunctionCall(
      vm::NewConstantExpression(*callback), std::move(args));
  return {
      .initial_value = NewLazyString(L"…"),
      .value =
          target
              .EvaluateExpression(std::move(expression),
                                  target.environment().ToRoot())
              .Transform([](gc::Root<vm::Value> value)
                             -> futures::ValueOrError<
                                 NonNull<std::shared_ptr<LazyString>>> {
                VLOG(7) << "Evaluated result: " << value.ptr()->get_string();
                return futures::Past(NewLazyString(value.ptr()->get_string()));
              })
              .ConsumeErrors([](Error error) {
                VLOG(7) << "Evaluation error: " << error;
                return futures::Past(
                    NewLazyString(L"E: " + std::move(error.read())));
              })};
}
#endif

NonNull<std::shared_ptr<Line>> ShowLine(EditorState& editor,
                                        const dirent& entry) {
  enum class SizeBehavior { kShow, kSkip };

  struct FileType {
    std::wstring description;
    LineModifierSet modifiers;
  };
  static const std::unordered_map<int, FileType> types = {
      {DT_BLK, {L" (block dev)", {LineModifier::kGreen}}},
      {DT_CHR, {L" (char dev)", {LineModifier::kRed}}},
      {DT_DIR, {L"/", {LineModifier::kCyan}}},
      {DT_FIFO, {L" (named pipe)", {LineModifier::kBlue}}},
      {DT_LNK, {L"@", {LineModifier::kItalic}}},
      {DT_REG, {L"", {}}},
      {DT_SOCK, {L" (unix sock)", {LineModifier::kMagenta}}}};

  auto path = FromByteString(entry.d_name);

  auto type_it = types.find(entry.d_type);
  if (type_it == types.end()) {
    type_it = types.find(DT_REG);
    CHECK(type_it != types.end());
  }

  LineBuilder line_options(NewLazyString(path + type_it->second.description));
  if (!type_it->second.modifiers.empty()) {
    line_options.set_modifiers(ColumnNumber(0), type_it->second.modifiers);
  }

  // See note about why GetMetadata is disabled (above).
  // line_options.SetMetadata(GetMetadata(target, path));
  line_options.SetExplicitDeleteObserver(
      [&editor, path] { StartDeleteFile(editor, path); });

  return MakeNonNullShared<Line>(std::move(line_options).Build());
}

void ShowFiles(EditorState& editor, std::wstring name,
               std::vector<dirent> entries, MutableLineSequence& builder) {
  if (entries.empty()) return;
  std::sort(entries.begin(), entries.end(),
            [](const dirent& a, const dirent& b) {
              return strcmp(a.d_name, b.d_name) < 0;
            });

  builder.push_back(MakeNonNullShared<Line>(
      LineBuilder(NewLazyString(L"## " + name + L" (" +
                                std::to_wstring(entries.size()) + L")"))
          .Build()));
  for (auto& entry : entries) builder.push_back(ShowLine(editor, entry));
  builder.push_back(L"");
}
}  // namespace

futures::Value<EmptyValue> GenerateDirectoryListing(Path path,
                                                    OpenBuffer& output) {
  LOG(INFO) << "GenerateDirectoryListing: " << path;
  output.Set(buffer_variables::atomic_lines, true);
  output.Set(buffer_variables::allow_dirty_delete, true);
  output.Set(buffer_variables::tree_parser, L"md");
  output.AppendToLastLine(NewLazyString(L"# 🗁  File listing: " + path.read()));
  output.AppendEmptyLine();

  return output.editor()
      .thread_pool()
      .Run([&editor = output.editor(), path,
            noise_regexp = output.Read(buffer_variables::directory_noise)]()
               -> ValueOrError<LineSequence> {
        DECLARE_OR_RETURN(BackgroundReadDirOutput results,
                          ReadDir(path, std::wregex(noise_regexp)));

        TRACK_OPERATION(GenerateDirectoryListing_BuildingMarkdown);
        MutableLineSequence builder;
        ShowFiles(editor, L"🗁  Directories", std::move(results.directories),
                  builder);
        ShowFiles(editor, L"🗀  Files", std::move(results.regular_files),
                  builder);
        ShowFiles(editor, L"🗐  Noise", std::move(results.noise), builder);
        return Success(builder.snapshot());
      })
      .Transform([&output, path](LineSequence contents) {
        TRACK_OPERATION(GenerateDirectoryListing_InsertContents);
        auto disk_state_freezer = output.FreezeDiskState();
        output.InsertInPosition(contents, output.contents().range().end(),
                                std::nullopt);
        return Success();
      })
      .ConsumeErrors([&output](Error error) {
        auto disk_state_freezer = output.FreezeDiskState();
        output.status().InsertError(error);
        output.AppendLine(NewLazyString(std::move(error).read()));
        return futures::Past(EmptyValue());
      });
}
}  // namespace afc::editor
