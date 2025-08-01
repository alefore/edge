#ifndef __AFC_EDITOR_BUFFERS_LIST_H__
#define __AFC_EDITOR_BUFFERS_LIST_H__

#include <list>
#include <memory>
#include <vector>

#include "src/buffer_widget.h"
#include "src/language/gc.h"
#include "src/line_with_cursor.h"
#include "src/widget.h"

namespace afc::editor {
class BufferRegistry;

// Divides the screen vertically into two sections: at the top, displays a
// numbered list of buffers; at the bottom, displays a given widget.
class BuffersList {
  BufferRegistry& buffer_registry_;

 public:
  class CustomerAdapter {
   public:
    virtual ~CustomerAdapter() = default;

    virtual std::vector<language::gc::Root<OpenBuffer>> active_buffers() = 0;

    virtual bool multiple_buffers_mode() = 0;
  };

  BuffersList(BufferRegistry& buffer_registry,
              language::NonNull<std::unique_ptr<CustomerAdapter>> customer);
  enum class AddBufferType { kVisit, kOnlyList, kIgnore };
  void AddBuffer(language::gc::Root<OpenBuffer> buffer,
                 AddBufferType add_buffer_type);

  size_t GetCurrentIndex();

  std::optional<language::gc::Root<OpenBuffer>> active_buffer() const;

  // See comments on `filter_`.
  void set_filter(
      std::optional<std::vector<language::gc::WeakPtr<OpenBuffer>>> filter);

  BufferWidget& GetActiveLeaf();
  const BufferWidget& GetActiveLeaf() const;

  LineWithCursor::Generator::Vector GetLines(
      Widget::OutputProducerOptions options) const;

  void Update();

 private:
  BuffersList(BufferRegistry& buffer_registry,
              language::NonNull<std::unique_ptr<CustomerAdapter>> customer,
              language::NonNull<std::unique_ptr<BufferWidget>> buffer_widget);

  const language::NonNull<std::unique_ptr<CustomerAdapter>> customer_;

  // Points to the BufferWidget that corresponds to the active buffer.
  language::NonNull<BufferWidget*> active_buffer_widget_;

  // Contains the whole hierarchy of widgets.
  language::NonNull<std::unique_ptr<Widget>> widget_;

  // If it has a value, buffers not included will be dimmed (disabled).
  std::optional<std::vector<language::gc::WeakPtr<OpenBuffer>>> filter_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFERS_LIST_H__
