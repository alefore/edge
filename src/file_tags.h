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
  struct ConstructorAccessKey {};

  const language::gc::Ptr<OpenBuffer> buffer_;

  using TagsMap =
      std::map<language::lazy_string::NonEmptySingleLine,
               language::NonNull<std::shared_ptr<concurrent::Protected<
                   std::vector<language::lazy_string::LazyString>>>>>;
  struct Data {
    language::text::LineNumber start_line;
    language::text::LineNumber end_line;

    TagsMap tags = {};
  };

  concurrent::Protected<Data> data_;

  struct LoadTagsOutput {
    language::text::LineNumber end_line;
    TagsMap tags_map;
  };

 public:
  static language::ValueOrError<language::gc::Root<FileTags>> New(
      language::gc::Ptr<OpenBuffer> buffer);

  FileTags(ConstructorAccessKey, language::gc::Ptr<OpenBuffer> buffer,
           language::text::LineNumber start_line,
           LoadTagsOutput load_tags_output);

  language::NonNull<std::shared_ptr<
      concurrent::Protected<std::vector<language::lazy_string::LazyString>>>>
  Find(language::lazy_string::NonEmptySingleLine tag_name);

  const language::gc::Ptr<OpenBuffer>& buffer() const;

  void Add(language::lazy_string::NonEmptySingleLine name,
           language::lazy_string::SingleLine value);

  void Expand(language::gc::ObjectMetadata::Receiver& visit) const;

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
