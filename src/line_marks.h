#ifndef __AFC_EDITOR_LINE_MARKS_H__
#define __AFC_EDITOR_LINE_MARKS_H__

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/buffer_name.h"
#include "src/editor_mode.h"
#include "src/lazy_string.h"
#include "src/line_column.h"

namespace afc {
namespace editor {

class OpenBuffer;

struct LineMarks {
 public:
  struct Mark {
    // What created this mark?
    const BufferName source = BufferName(L"");

    // What line in the source did this mark occur in?
    const LineNumber source_line;

    // What buffer does this mark identify?
    const BufferName target_buffer = BufferName(L"");

    // The line marked.
    const LineColumn target_line_column;
  };

  // A mark whose source buffer was removed will be preserved for some time. In
  // this case, we retain the original content.
  //
  // The reason for expired marks is to preserve marks while recompilation is
  // taking place: the user can still see the old marks (the output from the
  // previous run of the compiler) while they're being updated.
  struct ExpiredMark {
    // What created this mark?
    const BufferName source = BufferName(L"");

    // The contents in the source (and line) that created this mark.
    const language::NonNull<std::shared_ptr<LazyString>> source_line_content;

    // What buffer does this mark identify?
    const BufferName target_buffer = BufferName(L"");

    // The position marked.
    const LineColumn target_line_column;
  };

  void AddMark(Mark mark);

  void ExpireMarksFromSource(const OpenBuffer& source_buffer,
                             const BufferName& source);
  void RemoveExpiredMarksFromSource(const BufferName& source);

  const std::multimap<LineColumn, Mark>& GetMarksForTargetBuffer(
      const BufferName& target_buffer) const;
  const std::multimap<LineColumn, ExpiredMark>& GetExpiredMarksForTargetBuffer(
      const BufferName& target_buffer) const;

  struct MarksMaps {
    std::multimap<LineColumn, Mark> marks;
    std::multimap<LineColumn, ExpiredMark> expired_marks;
  };

  // First key is the source, second key is the target_buffer.
  std::unordered_map<BufferName, std::unordered_map<BufferName, MarksMaps>>
      marks_by_source_target;

  // First key is the target_buffer.
  std::unordered_map<BufferName, MarksMaps> marks_by_target;

  size_t updates = 0;
};

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lc);
std::ostream& operator<<(std::ostream& os, const LineMarks::ExpiredMark& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_MARKS_H__
