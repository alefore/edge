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
  marks[mark.source][mark.target_buffer].insert(
      std::make_pair(mark.target_line_column.line, mark));
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
  std::unordered_map<BufferName, std::multimap<LineNumber, ExpiredMark>>&
      expired_marks_by_source = expired_marks[source];
  for (auto& [target, marks_multimap] : it->second) {
    if (marks_multimap.empty()) continue;
    DVLOG(10) << "Mark transitions from fresh to expired.";
    std::multimap<LineNumber, ExpiredMark>& output =
        expired_marks_by_source[target];
    for (auto& [line, mark] : marks_multimap) {
      changes = true;
      output.insert(
          {line,
           ExpiredMark{.source = source,
                       .source_line_content =
                           line > source_buffer.contents().EndLine()
                               ? NewLazyString(L"Expired mark.")
                               : source_buffer.contents().at(line)->contents(),
                       .target_buffer = mark.target_buffer,
                       .target_line_column = mark.target_line_column}});
    }
  }
  marks.erase(it);
  if (changes) {
    LOG(INFO) << "Actually expired some marks.";
    updates++;
  }
}

void LineMarks::RemoveExpiredMarksFromSource(const BufferName& source) {
  static Tracker tracker(L"LineMarks::RemoveExpiredMarksFromSource");
  auto call = tracker.Call();

  updates += expired_marks.erase(source);
}

std::multimap<LineColumn, LineMarks::Mark> LineMarks::GetMarksForTargetBuffer(
    const BufferName& target_buffer) const {
  static Tracker tracker(L"LineMarks::GetMarksForTargetBuffer");
  auto call = tracker.Call();

  DLOG(INFO) << "Producing marks for buffer: " << target_buffer;
  std::multimap<LineColumn, LineMarks::Mark> output;
  for (auto& [source, source_target_map] : marks)
    if (auto target_it = source_target_map.find(target_buffer);
        target_it != source_target_map.end())
      output.insert(target_it->second.begin(), target_it->second.end());

  return output;
}

std::multimap<LineColumn, LineMarks::ExpiredMark>
LineMarks::GetExpiredMarksForTargetBuffer(
    const BufferName& target_buffer) const {
  static Tracker tracker(L"LineMarks::GetExpiredMarksForTargetBuffer");
  auto call = tracker.Call();

  DLOG(INFO) << "Producing marks for buffer: " << target_buffer;
  std::multimap<LineColumn, LineMarks::ExpiredMark> output;
  for (auto& [source, source_target_map] : expired_marks)
    if (auto target_it = source_target_map.find(target_buffer);
        target_it != source_target_map.end())
      output.insert(target_it->second.begin(), target_it->second.end());
  return output;
}

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lm) {
  os << "[" << lm.source << ":" << lm.target_buffer << ":"
     << lm.target_line_column << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const LineMarks::ExpiredMark& lm) {
  os << "[expired:" << lm.source << ":" << lm.target_buffer << ":"
     << lm.target_line_column << "]";
  return os;
}

}  // namespace afc::editor
