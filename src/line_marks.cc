#include "src/line_marks.h"

#include <glog/logging.h>

#include <string>
#include <vector>

#include "src/buffer.h"
#include "src/char_buffer.h"
#include "src/infrastructure/tracker.h"
#include "src/language/wstring.h"

namespace afc::editor {
using infrastructure::Tracker;

void LineMarks::AddMark(Mark mark) {
  marks[mark.source].insert(make_pair(mark.target_buffer, mark));
  updates++;
}

void LineMarks::ExpireMarksFromSource(const OpenBuffer& source_buffer,
                                      const BufferName& source) {
  static Tracker tracker(L"LineMarks::ExpireMarksFromSource");
  auto call = tracker.Call();

  auto it = marks.find(source);
  if (it == marks.end() || it->second.empty()) {
    LOG(INFO) << "No marks from source: " << source;
    return;
  }

  DVLOG(5) << "Expiring marks from: " << source;
  bool changes = false;
  std::multimap<BufferName, ExpiredMark>& output = expired_marks[source];
  for (auto& mark : it->second) {
    DVLOG(10) << "Mark transitions from fresh to expired.";
    changes = true;
    auto line = source_buffer.LineAt(mark.second.source_line);
    if (line == nullptr) {
      DVLOG(3) << "Unable to find content for mark!";
      output.insert(
          {it->first,
           ExpiredMark{.source = it->first,
                       .source_line_content = NewLazyString(L"Expired mark."),
                       .target_buffer = mark.second.target_buffer,
                       .target = mark.second.target}});
    } else {
      output.insert(
          {it->first, ExpiredMark{.source = it->first,
                                  .source_line_content = line->contents(),
                                  .target_buffer = mark.second.target_buffer,
                                  .target = mark.second.target}});
    }
  }

  if (changes) {
    LOG(INFO) << "Actually expired some marks.";
    updates++;
  }
}

void LineMarks::RemoveExpiredMarksFromSource(const BufferName& source) {
  static Tracker tracker(L"LineMarks::RemoveExpiredMarksFromSource");
  auto call = tracker.Call();

  auto it = expired_marks.find(source);
  if (it == expired_marks.end() || it->second.empty()) {
    LOG(INFO) << "No marks from source: " << source;
    return;
  }

  DVLOG(5) << "Removing expired marks from: " << source;
  expired_marks.erase(it);
  updates++;
}

std::vector<LineMarks::Mark> LineMarks::GetMarksForTargetBuffer(
    const BufferName& target_buffer) const {
  static Tracker tracker(L"LineMarks::GetMarksForTargetBuffer");
  auto call = tracker.Call();

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

std::vector<LineMarks::ExpiredMark> LineMarks::GetExpiredMarksForTargetBuffer(
    const BufferName& target_buffer) const {
  static Tracker tracker(L"LineMarks::GetExpiredMarksForTargetBuffer");
  auto call = tracker.Call();

  DLOG(INFO) << "Producing marks for buffer: " << target_buffer;
  std::vector<LineMarks::ExpiredMark> output;
  for (auto& source_it : expired_marks) {
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

std::ostream& operator<<(std::ostream& os, const LineMarks::ExpiredMark& lm) {
  os << "[expired:" << lm.source << ":" << lm.target_buffer << ":" << lm.target
     << "]";
  return os;
}

}  // namespace afc::editor
