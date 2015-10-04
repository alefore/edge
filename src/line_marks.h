#ifndef __AFC_EDITOR_LINE_MARKS_H__
#define __AFC_EDITOR_LINE_MARKS_H__

#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor_mode.h"

namespace afc {
namespace editor {

struct LineMarks {
 public:
  struct Mark {
    // What created this mark?
    std::wstring source;

    // What buffer does this mark identify?
    std::wstring target_buffer;

    // The line marked.
    size_t line;
  };

  void AddMark(Mark mark);

  void RemoveMarksFromSource(const std::wstring& source);

  std::vector<Mark> GetMarksForTargetBuffer(
      const std::wstring& target_buffer) const;

  // First key is the source, second key is the target_buffer.
  std::unordered_map<std::wstring, std::multimap<std::wstring, Mark>> marks;
  size_t updates;
};

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_MARKS_H__
