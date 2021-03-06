#ifndef __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class VerticalSplitOutputProducer : public OutputProducer {
 public:
  struct Column {
    std::unique_ptr<OutputProducer> producer;

    // If absent, this column will be the last column produced, and it will
    // allow to span the entire screen.
    std::optional<ColumnNumberDelta> width = std::nullopt;
  };

  VerticalSplitOutputProducer(std::vector<Column> columns, size_t index_active);

  Generator Next() override;

 private:
  const std::vector<Column> columns_;
  const size_t index_active_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__
