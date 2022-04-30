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
  marks_by_source_target[mark.source_buffer][mark.target_buffer].marks.insert(
      std::make_pair(mark.target_line_column, mark));
  marks_by_target[mark.target_buffer].marks.insert(
      std::make_pair(mark.target_line_column, mark));
}

void LineMarks::RemoveSource(const BufferName& source) {
  LOG(INFO) << "Removing source: " << source;
  auto it = marks_by_source_target.find(source);
  if (it == marks_by_source_target.end()) return;
  for (auto& [target, source_target_marks] : it->second) {
    auto& target_marks = marks_by_target[target];

    for (auto it = target_marks.marks.begin();
         it != target_marks.marks.end();) {
      if (it->second.source_buffer == source)
        target_marks.marks.erase(it++);
      else
        ++it;
    }

    for (auto it = target_marks.expired_marks.begin();
         it != target_marks.expired_marks.end();) {
      if (it->second.source_buffer == source)
        target_marks.expired_marks.erase(it++);
      else
        ++it;
    }
  }
  marks_by_source_target.erase(it);
}

void LineMarks::ExpireMarksFromSource(const BufferContents& source_buffer,
                                      const BufferName& source) {
  static Tracker tracker(L"LineMarks::ExpireMarksFromSource");
  auto call = tracker.Call();

  auto it = marks_by_source_target.find(source);
  if (it == marks_by_source_target.end() || it->second.empty()) {
    LOG(INFO) << "No marks from source: " << source;
    return;
  }

  DVLOG(5) << "Expiring marks from: " << source;
  for (auto& [target, source_target_marks] : it->second) {
    auto& target_marks = marks_by_target[target];
    DVLOG(10) << "Mark transitions from fresh to expired.";
    for (auto& [position, mark] : source_target_marks.marks) {
      ExpiredMark expired_mark{
          .source_buffer = source,
          .source_line_content =
              mark.source_line > source_buffer.EndLine()
                  ? NewLazyString(L"(expired)")
                  : source_buffer.at(mark.source_line)->contents(),
          .target_buffer = mark.target_buffer,
          .target_line_column = mark.target_line_column};
      source_target_marks.expired_marks.insert({position, expired_mark});
      target_marks.expired_marks.insert({position, expired_mark});
      auto range = target_marks.marks.equal_range(position);
      while (range.first != range.second) {
        if (range.first->second.source_buffer == source)
          target_marks.marks.erase(range.first++);
        else
          ++range.first;
      }
    }
    source_target_marks.marks.clear();
  }
}

void LineMarks::RemoveExpiredMarksFromSource(const BufferName& source) {
  static Tracker tracker(L"LineMarks::RemoveExpiredMarksFromSource");
  auto call = tracker.Call();

  auto it = marks_by_source_target.find(source);
  if (it == marks_by_source_target.end() || it->second.empty()) {
    LOG(INFO) << "No marks from source: " << source;
    return;
  }

  std::vector<BufferName> targets_to_process;
  for (auto& [target, marks_set] : it->second) {
    if (marks_set.expired_marks.empty()) continue;
    marks_set.expired_marks.clear();
    targets_to_process.push_back(target);
  }
  for (auto& target : targets_to_process) {
    std::multimap<LineColumn, ExpiredMark>& target_expired_marks =
        marks_by_target[target].expired_marks;
    for (auto it = target_expired_marks.begin();
         it != target_expired_marks.end();)
      if (it->second.source_buffer == source)
        target_expired_marks.erase(it++);
      else
        ++it;
  }
}

const std::multimap<LineColumn, LineMarks::Mark>&
LineMarks::GetMarksForTargetBuffer(const BufferName& target_buffer) const {
  static Tracker tracker(L"LineMarks::GetMarksForTargetBuffer");
  auto call = tracker.Call();

  DLOG(INFO) << "Producing marks for buffer: " << target_buffer;
  if (auto it = marks_by_target.find(target_buffer);
      it != marks_by_target.end())
    return it->second.marks;
  static const std::multimap<LineColumn, LineMarks::Mark> empty_output;
  return empty_output;
}

const std::multimap<LineColumn, LineMarks::ExpiredMark>&
LineMarks::GetExpiredMarksForTargetBuffer(
    const BufferName& target_buffer) const {
  static Tracker tracker(L"LineMarks::GetExpiredMarksForTargetBuffer");
  auto call = tracker.Call();

  if (auto it = marks_by_target.find(target_buffer);
      it != marks_by_target.end())
    return it->second.expired_marks;
  static const std::multimap<LineColumn, LineMarks::ExpiredMark> empty_output;
  return empty_output;
}

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lm) {
  os << "[" << lm.source_buffer << ":" << lm.target_buffer << ":"
     << lm.target_line_column << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const LineMarks::ExpiredMark& lm) {
  os << "[expired:" << lm.source_buffer << ":" << lm.target_buffer << ":"
     << lm.target_line_column << "]";
  return os;
}
}  // namespace afc::editor
