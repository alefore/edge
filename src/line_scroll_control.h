#ifndef __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
#define __AFC_EDITOR_LINE_SCROLL_CONTROL_H__

#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
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
    size_t lines_shown;

    // Total number of columns in the output for buffer contents.
    size_t columns_shown;

    // Initial position in the buffer where output will begin.
    LineColumn begin;

    // When we advance lines, we will start at the column given by
    // initial_column_.
    size_t initial_column;
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

    std::set<size_t> GetCurrentCursors() const;

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

  Range GetRange(LineColumn begin);

  const Options options_;
  const std::map<size_t, std::set<size_t>> cursors_;

  std::vector<Reader*> readers_;

  Range range_;

  // Counts the number of readers that have switched to State::kDone since the
  // range was last updated.
  size_t readers_done_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
