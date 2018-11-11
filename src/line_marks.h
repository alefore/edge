#ifndef __AFC_EDITOR_LINE_MARKS_H__
#define __AFC_EDITOR_LINE_MARKS_H__

#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor_mode.h"
#include "lazy_string.h"
#include "line_column.h"

namespace afc {
namespace editor {

class OpenBuffer;

struct LineMarks {
 public:
  struct Mark {
    // What created this mark?
    std::wstring source;

    // The contents in the source that created this mark. Typically this will be
    // set to null: the customer must look it up directly through source and
    // source_line. However, we support an "expired" mode, where marks are
    // preserved for some time after their source buffer is removed. In that
    // case, contents will be set to non-null (and source_line must be ignored).
    //
    // We say that a mark is "expired" if source_line_content == nullptr;
    // otherwise, we say that it is "fresh".
    //
    // The reason for expired marks is to preserve marks while recompilation is
    // taking place: the user can still see the old marks (the output from the
    // previous run of the compiler) while they're being updated.
    std::shared_ptr<LazyString> source_line_content;
    bool IsExpired() const { return source_line_content != nullptr; }

    // What line in the source did this mark occur in?
    size_t source_line = 0;

    // What buffer does this mark identify?
    std::wstring target_buffer;

    // The line marked.
    LineColumn target;
  };

  void AddMark(Mark mark);

  void ExpireMarksFromSource(const OpenBuffer& source_buffer,
                             const std::wstring& source);
  void RemoveExpiredMarksFromSource(const std::wstring& source);

  std::vector<Mark> GetMarksForTargetBuffer(
      const std::wstring& target_buffer) const;

  // First key is the source, second key is the target_buffer.
  std::unordered_map<std::wstring, std::multimap<std::wstring, Mark>> marks;
  size_t updates = 0;
};

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_MARKS_H__
