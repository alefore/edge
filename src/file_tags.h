#ifndef __AFC_EDITOR_FILE_TAGS_H__
#define __AFC_EDITOR_FILE_TAGS_H__

#include <map>
#include <memory>

#include "src/buffer.h"
#include "src/concurrent/protected.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line_sequence.h"
#include "src/vm/environment.h"

namespace afc::editor {
// TODO(2025-06-08, trivial): Use private ConstructorAccessTag, force
// construction through a gc::Pool (to ensure no problems with gc::Ptr).
//
// TODO(2025-06-08, trivial): Make this class thread-safe.
class FileTags {
  language::gc::Ptr<OpenBuffer> buffer_;

  language::text::LineNumber start_line_;
  language::text::LineNumber end_line_;

  using TagsMap =
      std::map<language::lazy_string::NonEmptySingleLine,
               language::NonNull<std::shared_ptr<concurrent::Protected<
                   std::vector<language::lazy_string::LazyString>>>>>;
  TagsMap tags_ = {};

  struct LoadTagsOutput {
    language::text::LineNumber end_line;
    TagsMap tags_map;
  };

 public:
  static language::ValueOrError<FileTags> New(
      language::gc::Ptr<OpenBuffer> buffer);

  FileTags(language::gc::Ptr<OpenBuffer> buffer,
           language::text::LineNumber start_line,
           LoadTagsOutput load_tags_output);

  language::NonNull<std::shared_ptr<
      concurrent::Protected<std::vector<language::lazy_string::LazyString>>>>
  Find(language::lazy_string::NonEmptySingleLine tag_name);

  const language::gc::Ptr<OpenBuffer>& buffer() const;

  void Add(language::lazy_string::NonEmptySingleLine name,
           language::lazy_string::SingleLine value);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  static language::ValueOrError<LoadTagsOutput> LoadTags(
      const language::text::LineSequence& contents,
      language::text::LineNumber tags_position);

  static void AddTag(language::lazy_string::NonEmptySingleLine name,
                     language::lazy_string::SingleLine value,
                     TagsMap& output_tags_map);
};

void RegisterFileTags(language::gc::Pool& pool, vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_FILE_TAGS_H__
