#include "src/line_marks.h"

#include <glog/logging.h>

#include <string>
#include <vector>

#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/text/line_sequence.h"
#include "src/language/wstring.h"

namespace container = afc::language::container;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::LineSequence;

namespace afc::editor {

void LineMarks::AddMark(Mark mark) {
  marks_by_source_target[mark.source_buffer][mark.target_buffer].marks.insert(
      std::make_pair(mark.target_line_column, mark));
  marks_by_target[mark.target_buffer].marks.insert(
      std::make_pair(mark.target_line_column, mark));
}

void LineMarks::RemoveSource(const BufferName& source) {
  LOG(INFO) << "Removing source: " << source;
  if (auto it = marks_by_source_target.find(source);
      it != marks_by_source_target.end()) {
    for (auto& [target, source_target_marks] : it->second) {
      auto& target_marks = marks_by_target[target];

      std::erase_if(target_marks.marks,
                    [&](const std::pair<LineColumn, const Mark>& entry) {
                      return entry.second.source_buffer == source;
                    });

      std::erase_if(target_marks.expired_marks,
                    [&](const std::pair<LineColumn, const ExpiredMark>& entry) {
                      return entry.second.source_buffer == source;
                    });
    }
    marks_by_source_target.erase(it);
  }
}

void LineMarks::ExpireMarksFromSource(const LineSequence& source_buffer,
                                      const BufferName& source) {
  TRACK_OPERATION(LineMarks_ExpireMarksFromSource);

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
          .source_line_content = mark.source_line > source_buffer.EndLine()
                                     ? Line(L"(expired)")
                                     : source_buffer.at(mark.source_line),
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
  TRACK_OPERATION(LineMarks_RemoveExpiredMarksFromSource);

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
    std::erase_if(marks_by_target[target].expired_marks,
                  [&](const std::pair<LineColumn, ExpiredMark>& entry) {
                    return entry.second.source_buffer == source;
                  });
  }
}

const std::multimap<LineColumn, LineMarks::Mark>&
LineMarks::GetMarksForTargetBuffer(const BufferName& target_buffer) const {
  TRACK_OPERATION(LineMarks_GetMarksForTargetBuffer);

  VLOG(5) << "Producing marks for buffer: " << target_buffer;
  if (auto it = marks_by_target.find(target_buffer);
      it != marks_by_target.end())
    return it->second.marks;
  static const std::multimap<LineColumn, LineMarks::Mark> empty_output;
  return empty_output;
}

const std::multimap<LineColumn, LineMarks::ExpiredMark>&
LineMarks::GetExpiredMarksForTargetBuffer(
    const BufferName& target_buffer) const {
  TRACK_OPERATION(LineMarks_GetExpiredMarksForTargetBuffer);

  if (auto it = marks_by_target.find(target_buffer);
      it != marks_by_target.end())
    return it->second.expired_marks;
  static const std::multimap<LineColumn, LineMarks::ExpiredMark> empty_output;
  return empty_output;
}

std::set<BufferName> LineMarks::GetMarkTargets() const {
  return container::MaterializeSet(marks_by_target | std::views::keys);
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
