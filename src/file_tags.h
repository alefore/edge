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
class FileTags {
  language::gc::Ptr<OpenBuffer> buffer_;

  // TODO(2025-06-06, trivial): Use NonEmptySingleLine for the key.
  using TagsMap =
      std::map<language::lazy_string::LazyString,
               language::NonNull<std::shared_ptr<concurrent::Protected<
                   std::vector<language::lazy_string::LazyString>>>>>;
  TagsMap tags_ = {};

 public:
  static language::ValueOrError<FileTags> New(
      language::gc::Ptr<OpenBuffer> buffer);

  FileTags(language::gc::Ptr<OpenBuffer> buffer, TagsMap tags);

  language::NonNull<std::shared_ptr<
      concurrent::Protected<std::vector<language::lazy_string::LazyString>>>>
  Find(language::lazy_string::LazyString tag_name);

  const language::gc::Ptr<OpenBuffer>& buffer() const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  static language::ValueOrError<TagsMap> LoadTags(
      const language::text::LineSequence& contents,
      language::text::LineNumber tags_position);
};

void RegisterFileTags(language::gc::Pool& pool, vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_FILE_TAGS_H__
