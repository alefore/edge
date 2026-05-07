#include "src/directory_listing.h"

#include <regex>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/line_prompt_mode.h"
#include "src/parsers/markdown.h"
#include "src/vm/constant_expression.h"
#include "src/vm/function_call.h"

namespace staging = afc::language::staging;
namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::OpenDir;
using afc::infrastructure::Path;
using afc::infrastructure::screen::Color;using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::FromByteString;
using afc::language::GetValueOrDefault;
using afc::language::GetValueOrDie;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::Observers;
using afc::language::overload;
using afc::language::Success;
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::ToLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LinePartMetadata;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;
using afc::vm::Environment;
using afc::vm::Expression;
using afc::vm::Type;

namespace afc::editor {
namespace {
struct FileEntry {
  LazyString name;
  SingleLine file_type_description;
  Style file_type_modifiers;
};

struct BackgroundReadDirOutput {
  std::vector<FileEntry> directories;
  std::vector<FileEntry> regular_files;
  std::vector<FileEntry> noise;
};

ValueOrError<BackgroundReadDirOutput> ReadDir(Path path,
                                              std::wregex noise_regex) {
  TRACK_OPERATION(GenerateDirectoryListing_ReadDir);
  return Visit(
      OpenDir(path),
      [&](NonNull<std::unique_ptr<DIR, std::function<void(DIR*)>>> dir) {
        BackgroundReadDirOutput output;
        struct dirent* entry;
        while ((entry = readdir(dir.get().get())) != nullptr) {
          if (strcmp(entry->d_name, ".") == 0) {
            continue;  // Showing the link to itself is rather pointless.
          }

          struct FileType {
            SingleLine description;
            Style modifiers;
          };
          static const std::unordered_map<int, FileType> types = {
              {DT_BLK,
               FileType{.description = SingleLine{LazyString{L" (block dev)"}},
                        .modifiers = Style{.foreground_color = StandardColor::Green}}},
              {DT_CHR,
               FileType{.description = SingleLine{LazyString{L" (char dev)"}},
                        .modifiers = Style{.foreground_color = StandardColor::Red}}},
              {DT_DIR,
               FileType{.description = SingleLine{LazyString{L"/"}},
                        .modifiers = Style{.foreground_color = StandardColor::Cyan}}},
              {DT_FIFO,
               FileType{.description = SingleLine{LazyString{L" (named pipe)"}},
                        .modifiers = Style{.foreground_color = StandardColor::Blue}}},
              {DT_LNK,
               FileType{
                   .description = SingleLine{LazyString{L"@"}},
                   .modifiers = Style{.attributes = StyleAttribute::Italic}}},
              {DT_REG, FileType{.description = SingleLine{LazyString{L""}},
                                .modifiers = Style{}}},
              {DT_SOCK,
               FileType{
                   .description = SingleLine{LazyString{L" (unix sock)"}},
                   .modifiers = Style{.foreground_color = StandardColor::Magenta}}}};
          FileType file_type = GetValueOrDefault(types, entry->d_type,
                                                 GetValueOrDie(types, DT_REG));

          std::wstring name = FromByteString(entry->d_name);
          FileEntry file_entry{.name = name,
                               .file_type_description = file_type.description,
                               .file_type_modifiers = file_type.modifiers};
          if (std::regex_match(name, noise_regex)) {
            output.noise.push_back(file_entry);
            continue;
          }

          if (entry->d_type == DT_DIR) {
            output.directories.push_back(file_entry);
            continue;
          }

          output.regular_files.push_back(file_entry);
        }
        return output;
      },
      [](Error) { return BackgroundReadDirOutput{}; });
}

void StartDeleteFile(EditorState& editor_state, vm::EscapedString path) {
  int result = unlink(path.OriginalString().ToBytes().c_str());
  editor_state.status().SetInformationText(LineBuilder{
      path.EscapedRepresentation() + SingleLine{LazyString{L": unlink: "}} +
      SingleLine{LazyString{result == 0 ? L"done"
                                        : L"ERROR: " +
                                              FromByteString(strerror(errno))}}}
                                               .Build());
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
        .initial_value = LazyString(),
        .value = futures::Future<LazyString>().value};
  }

  std::vector<NonNull<std::shared_ptr<Expression>>> args;
  args.push_back(vm::NewConstantExpression(
      {vm::Value::NewString(target.editor().gc_pool(), path)}));
  NonNull<std::unique_ptr<Expression>> expression = vm::NewFunctionCall(
      vm::NewConstantExpression(*callback), std::move(args));
  return language::text::LineMetadataEntry{
      .initial_value = LazyString{L"…"},
      .value =
          target
              .EvaluateExpression(std::move(expression),
                                  target.environment().ToRoot())
              .Transform([](gc::Root<vm::Value> value)
                             -> futures::ValueOrError<
                                 LazyString> {
                VLOG(7) << "Evaluated result: " << value.ptr()->get_string();
                return LazyString{value.ptr()->get_string()};
              })
              .ConsumeErrors([](Error error) {
                VLOG(7) << "Evaluation error: " << error;
                return LazyString{L"E: "} + std::move(error.read());
              })};
}
#endif

Line ShowLine(EditorState& editor, const FileEntry& entry) {
  enum class SizeBehavior { kShow, kSkip };

  vm::EscapedString path = vm::EscapedString::FromString(entry.name);

  LineBuilder line_options{path.EscapedRepresentation() +
                           entry.file_type_description};

  if (!entry.file_type_modifiers.empty())
    line_options.set_modifiers(
        ColumnNumber(0), LinePartMetadata{.style = entry.file_type_modifiers});

  // See note about why GetMetadata is disabled (above).
  // line_options.SetMetadata(GetMetadata(target, path));
  line_options.SetExplicitDeleteObserver(
      [&editor, path] { StartDeleteFile(editor, path); });

  return std::move(line_options).Build();
}

LineSequence ShowFiles(EditorState& editor, LazyString name,
                       std::vector<FileEntry> entries) {
  if (entries.empty()) return LineSequence();
  std::sort(
      entries.begin(), entries.end(),
      [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });

  MutableLineSequence output =
      MutableLineSequence::WithLine(staging::CleanValue(LineBuilder{
          SingleLine{LazyString{L"## "}} + SingleLine{name} +
          SingleLine{LazyString{L" ("}} +
          SingleLine{LazyString{std::to_wstring(entries.size())}} +
          SingleLine{LazyString{L")"}}}.Build()));
  output.append_back(
      std::move(entries) |
      std::views::transform([&editor](const FileEntry& entry) -> Line {
        return ShowLine(editor, entry);
      }) |
      staging::AddOrigin(staging::Clean));
  output.push_back(L"");
  return output.snapshot();
}
}  // namespace

futures::Value<EmptyValue> GenerateDirectoryListing(Path path,
                                                    OpenBuffer& output) {
  LOG(INFO) << "GenerateDirectoryListing: " << path;
  output.Set(buffer_variables::atomic_lines, true);
  output.Set(buffer_variables::allow_dirty_delete, true);
  output.Set(buffer_variables::tree_parser,
             language::lazy_string::ToLazyString(ParserId::Markdown()));
  output.AppendToLastLine(
      SINGLE_LINE_CONSTANT(L"# 🗁  File listing: ") +
      vm::EscapedString::FromString(path.read()).EscapedRepresentation());
  output.AppendEmptyLine();

  return output.editor()
      .thread_pool()
      .Run([&editor = output.editor(), path,
            noise_regexp = output.Read(buffer_variables::directory_noise)]()
               -> ValueOrError<LineSequence> {
        DECLARE_OR_RETURN(BackgroundReadDirOutput results,
                          ReadDir(path, std::wregex(noise_regexp.ToString())));

        TRACK_OPERATION(GenerateDirectoryListing_BuildingMarkdown);
        MutableLineSequence builder;
        builder.insert(builder.EndLine(),
                       ShowFiles(editor, LazyString{L"🗁  Directories"},
                                 std::move(results.directories)),
                       {});
        builder.insert(builder.EndLine(),
                       ShowFiles(editor, LazyString{L"🗀  Files"},
                                 std::move(results.regular_files)),
                       {});
        builder.insert(builder.EndLine(),
                       ShowFiles(editor, LazyString{L"🗐  Noise"},
                                 std::move(results.noise)),
                       {});
        return Success(builder.snapshot());
      })
      .Transform(LockAndVisitCallback(
          [&output, path](LineSequence contents, gc::Root<OpenBuffer> buffer) {
            TRACK_OPERATION(GenerateDirectoryListing_InsertContents);
            auto disk_state_freezer = buffer->FreezeDiskState();
            buffer->InsertInPosition(contents, buffer->contents().range().end(),
                                     std::nullopt);
            return Success();
          },
          [](LineSequence) { return Success(); }, output.WeakPtrFromThis()))
      .ConsumeErrors(LockAndVisitCallback(
          [&output](Error error,
                    gc::Root<OpenBuffer> buffer) -> futures::Value<EmptyValue> {
            auto disk_state_freezer = buffer->FreezeDiskState();
            buffer->status().InsertError(error);
            buffer->AppendLine(staging::CleanValue(
                LineSequence::BreakLines(std::move(error).read()).FoldLines()));
            return EmptyValue{};
          },
          [](Error) -> futures::Value<EmptyValue> { return EmptyValue{}; },
          output.WeakPtrFromThis()));
}
}  // namespace afc::editor
