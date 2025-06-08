#include "src/file_tags.h"

#include <vector>

#include "src/buffer.h"
#include "src/buffer_vm.h"
#include "src/concurrent/protected.h"
#include "src/language/container.h"
#include "src/language/error/view.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/column_number.h"
#include "src/language/lazy_string/lowercase.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_sequence.h"
#include "src/math/numbers.h"
#include "src/search_handler.h"
#include "src/vm/container.h"
#include "src/vm/string.h"
#include "src/vm/types.h"

namespace container = afc::language::container;
namespace gc = afc::language::gc;

using afc::concurrent::MakeProtected;
using afc::concurrent::Protected;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstOf;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::LowerCase;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::StartsWith;
using afc::language::lazy_string::Trim;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::math::numbers::Number;

namespace afc::editor {
/* static */ ValueOrError<FileTags> FileTags::New(gc::Ptr<OpenBuffer> buffer) {
  DECLARE_OR_RETURN(auto tags, LoadTags(buffer->contents().snapshot()));
  return FileTags(buffer, tags);
}

FileTags::FileTags(gc::Ptr<OpenBuffer> buffer, TagsMap tags)
    : buffer_(std::move(buffer)), tags_(std::move(tags)) {}

NonNull<std::shared_ptr<Protected<std::vector<LazyString>>>> FileTags::Find(
    LazyString tag_name) {
  if (auto it = tags_.find(tag_name); it != tags_.end()) return it->second;
  static const auto kEmptyValues =
      MakeNonNullShared<Protected<std::vector<LazyString>>>(
          MakeProtected(std::vector<LazyString>{}));
  return kEmptyValues;
}

const gc::Ptr<OpenBuffer>& FileTags::buffer() const { return buffer_; }

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> FileTags::Expand()
    const {
  return {buffer_.object_metadata()};
}

/* static */ ValueOrError<FileTags::TagsMap> FileTags::LoadTags(
    const LineSequence& contents) {
  DECLARE_OR_RETURN(
      language::text::LineColumn tags_start,
      GetNextMatch(Direction::kForwards,
                   SearchOptions{
                       .search_query = SINGLE_LINE_CONSTANT(L"## Tags"),
                       .required_positions = 1,
                       .case_sensitive = true,
                   },
                   contents));

  TagsMap output;
  LineNumber line_number = tags_start.line + LineNumberDelta{1};
  std::vector<Error> errors;
  while (line_number <= contents.EndLine() &&
         !StartsWith(contents.at(line_number).contents(), LazyString{L"#"})) {
    SingleLine line = contents.at(line_number).contents();
    ++line_number;
    if (!line.empty())
      VisitOptional(
          [&output, &line](ColumnNumber colon) {
            SingleLine tag =
                LowerCase(line.Substring(ColumnNumber{}, colon.ToDelta()));
            colon += ColumnNumberDelta{1};
            SingleLine value = Trim(line.Substring(colon), {L' '});
            DVLOG(5) << "Found tag: " << tag << ": " << value;
            if (auto it = output.find(ToLazyString(tag)); it != output.end())
              it->second->lock()->push_back(ToLazyString(value));
            else
              output.insert(std::pair(
                  ToLazyString(tag),
                  MakeNonNullShared<Protected<std::vector<LazyString>>>(
                      MakeProtected(
                          std::vector<LazyString>{ToLazyString(value)}))));
          },
          [&errors, &line] {
            errors.push_back(
                Error(LazyString{L"Unable to parse line: "} + line));
          },
          FindFirstOf(line, {L':'}));
  }

  if (errors.empty()) return output;
  return MergeErrors(errors, L", ");
}

}  // namespace afc::editor

namespace afc::vm {
template <>
const types::ObjectName
    VMTypeMapper<NonNull<std::shared_ptr<editor::FileTags>>>::object_type_name =
        types::ObjectName{IDENTIFIER_CONSTANT(L"FileTags")};

template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    Protected<std::vector<NonNull<std::shared_ptr<editor::FileTags>>>>>>>::
    object_type_name =
        types::ObjectName(IDENTIFIER_CONSTANT(L"VectorFileTags"));
}  // namespace afc::vm
namespace afc::editor {
void RegisterFileTags(language::gc::Pool& pool, vm::Environment& environment) {
  gc::Root<vm::ObjectType> file_tags_object_type = vm::ObjectType::New(
      pool,
      vm::VMTypeMapper<NonNull<std::shared_ptr<FileTags>>>::object_type_name);

  environment.DefineType(file_tags_object_type.ptr());

  environment.Define(
      IDENTIFIER_CONSTANT(L"FileTags"),
      vm::NewCallback(
          pool, vm::kPurityTypePure, [&pool](gc::Ptr<OpenBuffer> buffer) {
            return buffer->WaitForEndOfFile().Transform([root_buffer =
                                                             buffer.ToRoot()](
                                                            EmptyValue) {
              return std::visit(
                  overload{
                      [](FileTags value)
                          -> ValueOrError<NonNull<std::shared_ptr<FileTags>>> {
                        return MakeNonNullUnique<FileTags>(std::move(value));
                      },
                      [](Error error)
                          -> ValueOrError<NonNull<std::shared_ptr<FileTags>>> {
                        return error;
                      }},
                  FileTags::New(root_buffer.ptr()));
            });
          }));

  file_tags_object_type->AddField(
      IDENTIFIER_CONSTANT(L"buffer"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<FileTags>> file_tags) {
                        return file_tags->buffer().ToRoot();
                      })
          .ptr());
  file_tags_object_type->AddField(
      IDENTIFIER_CONSTANT(L"get"),
      vm::NewCallback(pool, vm::kPurityTypePure,
                      [](NonNull<std::shared_ptr<FileTags>> file_tags,
                         LazyString tag) { return file_tags->Find(tag); })
          .ptr());
  file_tags_object_type->AddField(
      IDENTIFIER_CONSTANT(L"get_first"),
      vm::NewCallback(
          pool, vm::kPurityTypePure,
          [](NonNull<std::shared_ptr<FileTags>> file_tags, LazyString tag) {
            return file_tags->Find(tag)->lock(
                [](const std::vector<LazyString>& values) {
                  return MakeNonNullShared<std::optional<LazyString>>(
                      values.empty()
                          ? std::optional<LazyString>()
                          : std::optional<LazyString>(values.front()));
                });
          })
          .ptr());

  vm::container::Export<std::vector<NonNull<std::shared_ptr<FileTags>>>>(
      pool, environment);

  environment.Define(
      IDENTIFIER_CONSTANT(L"VectorFileTags"),
      vm::NewCallback(
          pool, vm::kPurityTypePure,
          [](NonNull<
              std::shared_ptr<Protected<std::vector<gc::Ptr<OpenBuffer>>>>>
                 buffers) {
            return futures::UnwrapVectorFuture(
                       MakeNonNullShared<
                           std::vector<futures::Value<EmptyValue>>>(
                           buffers->lock(
                               [](const std::vector<gc::Ptr<OpenBuffer>>&
                                      buffers_data) {
                                 return container::MaterializeVector(
                                     buffers_data |
                                     std::views::transform(
                                         [](const gc::Ptr<OpenBuffer>& buffer) {
                                           return buffer->WaitForEndOfFile();
                                         }));
                               })))
                .Transform([buffers](const auto&) {
                  return MakeNonNullShared<Protected<
                      std::vector<NonNull<std::shared_ptr<FileTags>>>>>(
                      MakeProtected<
                          std::vector<NonNull<std::shared_ptr<FileTags>>>>(
                          buffers->lock(
                              [](const std::vector<gc::Ptr<OpenBuffer>>&
                                     buffers_data) {
                                return container::MaterializeVector(
                                    buffers_data |
                                    std::views::transform(
                                        [](gc::Ptr<OpenBuffer> buffer)
                                            -> ValueOrError<NonNull<
                                                std::shared_ptr<FileTags>>> {
                                          DECLARE_OR_RETURN(
                                              FileTags tags,
                                              FileTags::New(buffer));
                                          return MakeNonNullUnique<FileTags>(
                                              std::move(tags));
                                        }) |
                                    language::view::SkipErrors);
                              })));
                });
          }));
}
}  // namespace afc::editor
