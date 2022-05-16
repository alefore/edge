#include "src/line_marks.h"

#include <glog/logging.h>

#include "src/editor.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace afc::editor {
using language::NonNull;

namespace gc = language::gc;
namespace {
class LineMarksTest {
 public:
  LineMarksTest() {
    LOG(INFO) << "LineMarksTest constructed: " << source_name() << " and "
              << target_name();
  }
  LineMarks::Mark TestMark(LineNumber source_line,
                           LineColumn target_line_column) {
    return LineMarks::Mark{.source_buffer = source_.ptr()->name(),
                           .source_line = source_line,
                           .target_buffer = target_.ptr()->name(),
                           .target_line_column = target_line_column};
  }

  void ValidateEmpty() {
    auto marks = EditorForTests().line_marks();
    CHECK(marks.GetMarksForTargetBuffer(source_.ptr()->name()).empty());
    CHECK(marks.GetExpiredMarksForTargetBuffer(source_.ptr()->name()).empty());
    CHECK(marks.GetMarksForTargetBuffer(target_.ptr()->name()).empty());
    CHECK(marks.GetExpiredMarksForTargetBuffer(target_.ptr()->name()).empty());
  };

  BufferName source_name() const { return source_.ptr()->name(); }
  BufferName target_name() const { return target_.ptr()->name(); }

 private:
  gc::Root<OpenBuffer> source_ = NewBufferForTests();
  gc::Root<OpenBuffer> target_ = NewBufferForTests();
};

bool line_marks_test_registration = tests::Register(
    L"LineMarks",
    std::vector<tests::Test>(
        {{.name = L"AddMarkAndRemoveSource", .callback = [] {
            auto marks = EditorForTests().line_marks();
            LineMarksTest test;
            test.ValidateEmpty();

            marks.AddMark(test.TestMark(
                LineNumber(4), LineColumn(LineNumber(100), ColumnNumber(50))));

            CHECK(marks.GetMarksForTargetBuffer(test.source_name()).empty());
            CHECK(marks.GetExpiredMarksForTargetBuffer(test.source_name())
                      .empty());
            CHECK_EQ(marks.GetMarksForTargetBuffer(test.target_name()).size(),
                     1ul);
            CHECK(marks.GetExpiredMarksForTargetBuffer(test.target_name())
                      .empty());

            std::pair<LineColumn, LineMarks::Mark> entry =
                *marks.GetMarksForTargetBuffer(test.target_name()).begin();
            CHECK_EQ(entry.first,
                     LineColumn(LineNumber(100), ColumnNumber(50)));
            CHECK_EQ(entry.second.source_buffer, test.source_name());
            CHECK_EQ(entry.second.source_line, LineNumber(4));
            CHECK_EQ(entry.second.target_buffer, test.target_name());
            CHECK_EQ(entry.second.target_line_column,
                     LineColumn(LineNumber(100), ColumnNumber(50)));

            marks.RemoveSource(test.source_name());
            test.ValidateEmpty();
          }}}));
}  // namespace
}  // namespace afc::editor
