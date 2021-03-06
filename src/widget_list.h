#ifndef __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
#define __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__

#include <list>
#include <memory>

#include "src/buffers_list.h"
#include "src/output_producer.h"
#include "src/vm/public/environment.h"
#include "src/widget.h"

namespace afc {
namespace editor {

class EditorState;

class WidgetList : public Widget {
 protected:
  WidgetList(const EditorState* editor, std::unique_ptr<Widget> children);
  WidgetList(const EditorState* editor,
             std::vector<std::unique_ptr<Widget>> children, size_t active);

  const EditorState* const editor_;

  std::vector<std::unique_ptr<Widget>> children_;
  size_t active_;
};

class WidgetListHorizontal : public WidgetList {
 public:
  WidgetListHorizontal(const EditorState* editor,
                       std::unique_ptr<Widget> children);

  WidgetListHorizontal(const EditorState* editor,
                       std::vector<std::unique_ptr<Widget>> children,
                       size_t active);

  wstring Name() const;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;

 private:
  // Will return nullptr when the child should be skipped.
  std::unique_ptr<OutputProducer> NewChildProducer(
      OutputProducerOptions options, size_t index, LineNumberDelta lines) const;
};

class WidgetListVertical : public WidgetList {
 public:
  WidgetListVertical(const EditorState* editor,
                     std::unique_ptr<Widget> children);

  WidgetListVertical(const EditorState* editor,
                     std::vector<std::unique_ptr<Widget>> children,
                     size_t active);

  wstring Name() const;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;

 private:
  std::vector<ColumnNumberDelta> columns_per_child_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
