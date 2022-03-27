#ifndef __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

struct RowsVector {
 public:
  struct Row {
    std::function<LineWithCursor::Generator::Vector(LineNumberDelta)> callback;
    LineNumberDelta lines;
    enum class OverlapBehavior {
      // Rows after this one are pushed down in the output, unmodified.
      kSolid,
      // Each line from this row consumes entries from subsequent rows.
      kFloat
    };
    OverlapBehavior overlap_behavior = OverlapBehavior::kSolid;
  };

  Row& back() {
    CHECK(!rows.empty());
    return rows.back();
  }
  void push_back(Row row) { rows.push_back(std::move(row)); }

  std::vector<Row> rows = {};
  size_t index_active = 0;
  LineNumberDelta lines;
};

LineWithCursor::Generator::Vector OutputFromRowsVector(RowsVector table);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__
