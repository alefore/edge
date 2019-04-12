#include "line_marks.h"

#include <glog/logging.h>

#include <string>
#include <vector>

#include "buffer.h"
#include "char_buffer.h"
#include "wstring.h"

namespace afc {
namespace editor {

void LineMarks::AddMark(Mark mark) {
  marks[mark.source].insert(make_pair(mark.target_buffer, mark));
  updates++;
}

void LineMarks::ExpireMarksFromSource(const OpenBuffer& source_buffer,
                                      const std::wstring& source) {
  auto it = marks.find(source);
  if (it == marks.end() || it->second.empty()) {
    LOG(INFO) << "No marks from source: " << source;
    return;
  }

  DVLOG(5) << "Expiring marks from: " << source;
  bool changes = false;
  for (auto& mark : it->second) {
    if (mark.second.IsExpired()) {
      DVLOG(10) << "Skipping already expired mark.";
      continue;
    }

    DVLOG(10) << "Mark transitions from fresh to expired.";
    changes = true;
    auto line = source_buffer.empty()
                    ? nullptr
                    : source_buffer.LineAt(mark.second.source_line);
    if (line == nullptr) {
      DVLOG(3) << "Unable to find content for mark!";
      mark.second.source_line_content = NewLazyString(L"Expired mark.");
    } else {
      mark.second.source_line_content = line->contents();
    }
    CHECK(mark.second.IsExpired());
  }

  if (changes) {
    LOG(INFO) << "Actually expired some marks.";
    updates++;
  }
}

void LineMarks::RemoveExpiredMarksFromSource(const std::wstring& source) {
  auto it = marks.find(source);
  if (it == marks.end() || it->second.empty()) {
    LOG(INFO) << "No marks from source: " << source;
    return;
  }

  DVLOG(5) << "Removing expired marks from: " << source;
  bool changes = false;
  auto& marks_from_source = it->second;
  for (auto mark = marks_from_source.begin();
       mark != marks_from_source.end();) {
    if (mark->second.IsExpired()) {
      DVLOG(5) << "Removing expired mark.";
      changes = true;
      marks_from_source.erase(mark++);
    } else {
      DVLOG(10) << "Skipping fresh mark.";
      ++mark;
    }
  }

  if (changes) {
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

}  // namespace editor
}  // namespace afc
