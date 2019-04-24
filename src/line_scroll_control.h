#ifndef __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
#define __AFC_EDITOR_LINE_SCROLL_CONTROL_H__

#include <list>
#include <map>
#include <memory>
#include <set>

#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class LineScrollControl {
 private:
  struct ConstructorAccessTag {};

 public:
  LineScrollControl(std::shared_ptr<OpenBuffer> buffer, LineColumn view_start,
                    size_t columns_shown, size_t customers);

  // Returns the prediction for the range (from the buffer) that will be
  // displayed in the next line. The start is known to be accurate, but the end
  // could be inaccurate because we don't fully know how much certain characters
  // (mostly tabs, but also multi-width characters) will actually consume.
  Range GetRange() const { return range_; }

  bool HasActiveCursor() const;

  std::set<size_t> GetCurrentCursors() const;

  void Advance();

 private:
  const std::shared_ptr<OpenBuffer> buffer_;
  const LineColumn view_start_;
  const size_t columns_shown_;
  // How many customers will be notifying us (through calls to `Advance`) that
  // they are done printing output for the current range?
  const size_t customers_;

  Range range_;
  const std::map<size_t, std::set<size_t>> cursors_;
  // Counts calls to `Advance` since the last update to `range_`.
  size_t customers_done_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
