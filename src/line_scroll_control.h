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
  static std::shared_ptr<LineScrollControl> New(
      std::shared_ptr<OpenBuffer> buffer, LineColumn view_start,
      size_t columns_shown) {
    return std::make_shared<LineScrollControl>(
        ConstructorAccessTag(), std::move(buffer), view_start, columns_shown);
  }

  LineScrollControl(ConstructorAccessTag, std::shared_ptr<OpenBuffer> buffer,
                    LineColumn view_start, size_t columns_shown);

  class Reader {
   private:
    struct ConstructorAccessTag {};

   public:
    Reader(ConstructorAccessTag, std::shared_ptr<LineScrollControl> parent);

    // Returns the range corresponding to the current line. If `nullopt`, it
    // means the reader has signaled that it's done with this range, but other
    // readers are still outputing contents for it; in this case, it shouldn't
    // print anything.
    std::optional<Range> GetRange() const {
      return state_ == State::kDone ? std::nullopt
                                    : std::optional<Range>(parent_->range_);
    }

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
  void Advance();

  const std::shared_ptr<OpenBuffer> buffer_;
  const LineColumn view_start_;
  const size_t columns_shown_;

  Range range_;
  const std::map<size_t, std::set<size_t>> cursors_;

  std::vector<Reader*> readers_;

  // Counts the number of readers that have switched to State::kDone since the
  // range was last updated.
  size_t readers_done_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
