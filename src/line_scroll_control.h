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
                    size_t columns_shown);

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
  Range range_;
  const std::map<size_t, std::set<size_t>> cursors_;
};  // namespace editor

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_LINE_SCROLL_CONTROL_H__
