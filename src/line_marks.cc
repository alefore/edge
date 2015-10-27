#include "line_marks.h"

#include <string>
#include <vector>

#include <glog/logging.h>

#include "wstring.h"

namespace afc {
namespace editor {

void LineMarks::AddMark(Mark mark) {
  marks[mark.source].insert(make_pair(mark.target_buffer, mark));
  updates++;
}

void LineMarks::RemoveMarksFromSource(const std::wstring& source) {
  LOG(INFO) << "Removing marks from: " << source;
  if (marks.erase(source)) {
    LOG(INFO) << "Actually removed some marks.";
    updates++;
  }
}

std::vector<LineMarks::Mark> LineMarks::GetMarksForTargetBuffer(
    const std::wstring& target_buffer) const {
  DLOG(INFO) << "Producing marks for buffer: " << target_buffer;
  std::vector<LineMarks::Mark> output;
  for (auto& source_it : marks) {
    auto range = source_it.second.equal_range(target_buffer);
    if (range.first == source_it.second.end()) {
      DVLOG(5) << "Didn't find any marks.";
      continue;
    }
    while (range.first != range.second) {
      DVLOG(6) << "Mark: " << range.first->second;
      output.push_back(range.first->second);
      ++range.first;
    }
  }
  return output;
}

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lm) {
  os << "[" << lm.source << ":" << lm.target_buffer << ":" << lm.target << "]";
  return os;
}

} // namespace editor
} // namespace afc
