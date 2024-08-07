#ifndef __AFC_EDITOR_LINE_MARKS_H__
#define __AFC_EDITOR_LINE_MARKS_H__

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "src/buffer_name.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"

namespace afc {
namespace editor {

struct LineMarks {
 public:
  struct Mark {
    // What created this mark?
    const BufferName source_buffer;

    // What line in the source did this mark occur in?
    const language::text::LineNumber source_line;

    // What buffer does this mark identify?
    const BufferName target_buffer;

    // The line marked.
    const language::text::LineColumn target_line_column;
  };

  // A mark whose source buffer was removed will be preserved for some time. In
  // this case, we retain the original content.
  //
  // The reason for expired marks is to preserve marks while recompilation is
  // taking place: the user can still see the old marks (the output from the
  // previous run of the compiler) while they're being updated.
  struct ExpiredMark {
    // What created this mark?
    const BufferName source_buffer;

    // The contents in the source (and line) that created this mark.
    const language::text::Line source_line_content;

    // What buffer does this mark identify?
    const BufferName target_buffer;

    // The position marked.
    const language::text::LineColumn target_line_column;
  };

  void AddMark(Mark mark);

  void RemoveSource(const BufferName& source);

  void ExpireMarksFromSource(const language::text::LineSequence& source_buffer,
                             const BufferName& source);
  void RemoveExpiredMarksFromSource(const BufferName& source);

  const std::multimap<language::text::LineColumn, Mark>&
  GetMarksForTargetBuffer(const BufferName& target_buffer) const;
  const std::multimap<language::text::LineColumn, ExpiredMark>&
  GetExpiredMarksForTargetBuffer(const BufferName& target_buffer) const;

  std::set<BufferName> GetMarkTargets() const;

 private:
  struct MarksMaps {
    std::multimap<language::text::LineColumn, Mark> marks;
    std::multimap<language::text::LineColumn, ExpiredMark> expired_marks;
  };

  // First key is the source, second key is the target_buffer.
  std::unordered_map<BufferName, std::unordered_map<BufferName, MarksMaps>>
      marks_by_source_target;

  // First key is the target_buffer.
  std::unordered_map<BufferName, MarksMaps> marks_by_target;
};

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lc);
std::ostream& operator<<(std::ostream& os, const LineMarks::ExpiredMark& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_MARKS_H__
