#include "src/line_marks.h"

#include <glog/logging.h>

#include <functional>
#include <string>
#include <vector>

#include "src/infrastructure/tracker.h"
#include "src/language/container.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/lazy_string/single_line.h"
#include "src/language/text/line_sequence.h"
#include "src/language/wstring.h"
#include "src/tests/factory.h"

namespace container = afc::language::container;

using namespace afc::language::lazy_string;
using namespace afc::language::text;

namespace afc::editor {
LineMarks::MarkMapKey LineMarks::Mark::key() const {
  return std::make_pair(target_line_column, source_line);
}

void LineMarks::AddMark(Mark mark) {
  marks_by_source_target[mark.source_buffer][mark.target_buffer].marks.insert(
      std::make_pair(mark.key(), mark));
  marks_by_target[mark.target_buffer].marks.insert(
      std::make_pair(mark.key(), mark));
}

void LineMarks::RemoveSource(const BufferName& source) {
  LOG(INFO) << "Removing source: " << source;
  if (auto it = marks_by_source_target.find(source);
      it != marks_by_source_target.end()) {
    for (BufferName target : it->second | std::views::keys) {
      if (auto target_marks_it = marks_by_target.find(target);
          target_marks_it != marks_by_target.end()) {
        std::erase_if(target_marks_it->second.marks,
                      [&](const std::pair<MarkMapKey, const Mark>& entry) {
                        return entry.second.source_buffer == source;
                      });

        std::erase_if(target_marks_it->second.expired_marks,
                      [&](const std::pair<MarkMapKey, const Mark>& entry) {
                        return entry.second.source_buffer == source;
                      });
        if (target_marks_it->second.IsEmpty())
          marks_by_target.erase(target_marks_it);
      }
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
      Mark expired_mark{.source_buffer = source,
                        .source_line = mark.source_line,
                        .source_line_content =
                            mark.source_line > source_buffer.EndLine()
                                ? Line{SingleLine{LazyString{L"(expired)"}}}
                                : source_buffer.at(mark.source_line).value,
                        .target_buffer = mark.target_buffer,
                        .target_line_column = mark.target_line_column};
      source_target_marks.expired_marks.insert(
          {expired_mark.key(), expired_mark});
      target_marks.expired_marks.insert({expired_mark.key(), expired_mark});
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

  std::ranges::for_each(
      it->second, [&](std::pair<const BufferName, MarksMaps>& data) {
        if (data.second.expired_marks.empty()) return;
        data.second.expired_marks.clear();

        if (auto target_marks_it = marks_by_target.find(data.first);
            target_marks_it != marks_by_target.end()) {
          std::erase_if(marks_by_target[data.first].expired_marks,
                        [&](const std::pair<MarkMapKey, Mark>& entry) {
                          return entry.second.source_buffer == source;
                        });
          if (target_marks_it->second.IsEmpty())
            marks_by_target.erase(target_marks_it);
        }
      });
}

const std::multimap<LineMarks::MarkMapKey, LineMarks::Mark>&
LineMarks::GetMarksForTargetBuffer(const BufferName& target_buffer) const {
  TRACK_OPERATION(LineMarks_GetMarksForTargetBuffer);

  VLOG(5) << "Producing marks for buffer: " << target_buffer;
  if (auto it = marks_by_target.find(target_buffer);
      it != marks_by_target.end())
    return it->second.marks;
  static const std::multimap<MarkMapKey, LineMarks::Mark> empty_output;
  return empty_output;
}

const std::multimap<LineMarks::MarkMapKey, LineMarks::Mark>&
LineMarks::GetExpiredMarksForTargetBuffer(
    const BufferName& target_buffer) const {
  TRACK_OPERATION(LineMarks_GetExpiredMarksForTargetBuffer);

  if (auto it = marks_by_target.find(target_buffer);
      it != marks_by_target.end())
    return it->second.expired_marks;
  static const std::multimap<MarkMapKey, LineMarks::Mark> empty_output;
  return empty_output;
}

std::set<BufferName> LineMarks::GetMarkTargets() const {
  return marks_by_target | std::views::keys | std::ranges::to<std::set>();
}

bool LineMarks::MarksMaps::IsEmpty() const {
  return marks.empty() && expired_marks.empty();
}

std::ostream& operator<<(std::ostream& os, const LineMarks::Mark& lm) {
  os << "[" << lm.source_buffer << ":" << lm.target_buffer << ":"
     << lm.target_line_column << "]";
  return os;
}

namespace {
TEST_GROUP(LineMarks_MapCleanup,
           [](std::function<void(LineMarks&)> perform_action) {
             LineMarks marks;
             marks.AddMark(LineMarks::Mark{
                 AnonymousBufferName{0}, LineNumber{1},
                 Line{SINGLE_LINE_CONSTANT(L"content")}, AnonymousBufferName{2},
                 LineColumn{LineNumber{1}}});
             marks.AddMark(LineMarks::Mark{
                 AnonymousBufferName{0}, LineNumber{1},
                 Line{SINGLE_LINE_CONSTANT(L"content")}, AnonymousBufferName{3},
                 LineColumn{LineNumber{1}}});
             marks.AddMark(LineMarks::Mark{
                 AnonymousBufferName{1}, LineNumber{2},
                 Line{SINGLE_LINE_CONSTANT(L"content")}, AnonymousBufferName{3},
                 LineColumn{LineNumber{2}}});
             perform_action(marks);
             return marks.GetMarkTargets();
           })
    .Add(
        L"RemoveUnknownSource_DoesNothing",
        [](LineMarks& marks) { marks.RemoveSource(AnonymousBufferName{99}); },
        std::set<BufferName>{AnonymousBufferName{2}, AnonymousBufferName{3}})
    .Add(
        L"RemoveOneSource",
        [](LineMarks& marks) { marks.RemoveSource(AnonymousBufferName{0}); },
        std::set<BufferName>{AnonymousBufferName{3}})
    .Add(
        L"RemoveOneSourceButTargetSurvives",
        [](LineMarks& marks) { marks.RemoveSource(AnonymousBufferName{1}); },
        std::set<BufferName>{AnonymousBufferName{2}, AnonymousBufferName{3}})
    .Add(
        L"RemoveBothSources",
        [](LineMarks& marks) {
          marks.RemoveSource(AnonymousBufferName{0});
          marks.RemoveSource(AnonymousBufferName{1});
        },
        std::set<BufferName>{})
    .Add(
        L"ExpireWithoutRemove",
        [](LineMarks& marks) {
          marks.ExpireMarksFromSource(LineSequence{}, AnonymousBufferName{0});
        },
        std::set<BufferName>{AnonymousBufferName{2}, AnonymousBufferName{3}})
    .Add(
        L"ExpireAndRemoveExpired",
        [](LineMarks& marks) {
          BufferName source = AnonymousBufferName{0};
          marks.ExpireMarksFromSource(LineSequence{}, source);
          marks.RemoveExpiredMarksFromSource(source);
        },
        std::set<BufferName>{AnonymousBufferName{3}})
    .Add(
        L"RemoveSourceClearsExpiredMarks",
        [](LineMarks& marks) {
          BufferName source = AnonymousBufferName{0};
          marks.ExpireMarksFromSource(LineSequence{}, source);
          marks.RemoveSource(source);
        },
        std::set<BufferName>{AnonymousBufferName{3}});
}  // namespace
}  // namespace afc::editor
