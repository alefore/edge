#ifndef __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
#define __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__

#include <list>
#include <memory>

#include "src/output_producer.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class WidgetList : public Widget {
 protected:
  WidgetList(std::unique_ptr<Widget> children);
  WidgetList(std::vector<std::unique_ptr<Widget>> children, size_t active);

  std::vector<std::unique_ptr<Widget>> children_;
  size_t active_;
};

class WidgetListHorizontal : public WidgetList {
 public:
  WidgetListHorizontal(std::unique_ptr<Widget> children);

  WidgetListHorizontal(std::vector<std::unique_ptr<Widget>> children,
                       size_t active);

  std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;
  LineNumberDelta DesiredLines() const override;

 private:
  // Will return nullptr when the child should be skipped.
  std::unique_ptr<OutputProducer> NewChildProducer(
      OutputProducerOptions options, size_t index, LineNumberDelta lines) const;
};

class WidgetListVertical : public WidgetList {
 public:
  WidgetListVertical(std::unique_ptr<Widget> children);

  WidgetListVertical(std::vector<std::unique_ptr<Widget>> children,
                     size_t active);

  std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;
  LineNumberDelta DesiredLines() const override;

 private:
  std::vector<ColumnNumberDelta> columns_per_child_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
