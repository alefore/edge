#ifndef __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
#define __AFC_EDITOR_LINE_SCROLL_CONTROL_H__

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "src/line_output.h"
#include "src/output_producer.h"
#include "src/widget.h"

namespace afc {
namespace editor {
class LineScrollControl
    : public std::enable_shared_from_this<LineScrollControl> {
 private:
  struct ConstructorAccessTag {};

 public:
  struct Options {
    std::shared_ptr<OpenBuffer> buffer;

    // Total number of lines in the output.
    LineNumberDelta lines_shown;

    // Total number of columns in the output for buffer contents.
    ColumnNumberDelta columns_shown;

    // Initial position in the buffer where output will begin.
    LineColumn begin;

    // Number of lines above the buffer->position() that should be shown.
    // Ignored if less than lines_shown / 2, ignored.
    LineNumberDelta margin_lines;
  };

  static std::shared_ptr<LineScrollControl> New(
      LineScrollControl::Options options) {
    return std::make_shared<LineScrollControl>(ConstructorAccessTag(),
                                               std::move(options));
  }

  LineScrollControl(ConstructorAccessTag, Options options);

  class Reader {
   private:
    struct ConstructorAccessTag {};

   public:
    Reader(ConstructorAccessTag, std::shared_ptr<LineScrollControl> parent);

    // Returns the range we're currently outputing. If `nullopt`, it means the
    // reader has signaled that it's done with this range, but other readers are
    // still outputing contents for it; in this case, the reader shouldn't print
    // anything.
    std::optional<Range> GetRange() const;

    bool HasActiveCursor() const;

    // Returns the set of cursors that fall in the current range.
    //
    // The column positions are relative to the beginning of the input line
    // (i.e., changing the range affects only whether a given cursor is
    // returned, but once the decision is made that a cursor will be returned,
    // the value returned for it won't be affected by the range).
    std::set<ColumnNumber> GetCurrentCursors() const;

    void RangeDone() {
      CHECK(state_ == State::kProcessing);
      state_ = State::kDone;
      parent_->SignalReaderDone();
    }

   private:
    friend class LineScrollControl;

    std::shared_ptr<LineScrollControl> const parent_;
    enum class State { kDone, kProcessing };
    State state_ = State::kProcessing;
  };

  std::unique_ptr<Reader> NewReader();

 private:
  friend Reader;
  void SignalReaderDone();

  std::list<ColumnRange> ComputeBreaks(LineNumber line) const;
  std::list<Range> PrependLines(LineNumber line, LineNumberDelta lines_desired,
                                std::list<Range> output) const;
  std::list<Range> AdjustToHonorMargin(std::list<Range> output) const;
  std::list<Range> ComputeRanges() const;
  Range range() const;
  Range next_range() const;

  bool CurrentRangeContainsPosition(LineColumn position) const;

  const Options options_;
  const std::map<LineNumber, std::set<ColumnNumber>> cursors_;

  std::vector<Reader*> readers_;

  // Contains one element for each (screen) line to show, with the corresponding
  // range.
  std::list<Range> ranges_;

  // Counts the number of readers that have switched to State::kDone since the
  // range was last updated.
  size_t readers_done_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
