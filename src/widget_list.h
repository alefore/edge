#ifndef __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
#define __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__

#include <list>
#include <memory>

#include "src/language/safe_types.h"
#include "src/line_with_cursor.h"
#include "src/widget.h"

namespace afc::editor {

class WidgetList : public Widget {
 protected:
  WidgetList(language::NonNull<std::unique_ptr<Widget>> children);
  WidgetList(std::vector<language::NonNull<std::unique_ptr<Widget>>> children,
             size_t active);

  std::vector<language::NonNull<std::unique_ptr<Widget>>> children_;
  size_t active_;
};

class WidgetListHorizontal : public WidgetList {
 public:
  WidgetListHorizontal(language::NonNull<std::unique_ptr<Widget>> children);

  WidgetListHorizontal(
      std::vector<language::NonNull<std::unique_ptr<Widget>>> children,
      size_t active);

  LineWithCursor::Generator::Vector CreateOutput(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;
  LineNumberDelta DesiredLines() const override;

 private:
  LineWithCursor::Generator::Vector GetChildOutput(
      OutputProducerOptions options, size_t index, LineNumberDelta lines) const;
};

class WidgetListVertical : public WidgetList {
 public:
  WidgetListVertical(language::NonNull<std::unique_ptr<Widget>> children);

  WidgetListVertical(
      std::vector<language::NonNull<std::unique_ptr<Widget>>> children,
      size_t active);

  LineWithCursor::Generator::Vector CreateOutput(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;
  LineNumberDelta DesiredLines() const override;

 private:
  std::vector<ColumnNumberDelta> columns_per_child_;
};

}  // namespace afc::editor
#endif  // __AFC_EDITOR_BUFFER_TREE_HORIZONTAL_H__
