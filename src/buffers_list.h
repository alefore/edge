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

// Divides the screen vertically into two sections: at the top, displays a
// numbered list of buffers; at the bottom, displays a given widget.
class BuffersList {
 public:
  BuffersList(const EditorState& editor_state);
  enum class AddBufferType { kVisit, kOnlyList, kIgnore };
  void AddBuffer(language::gc::Root<OpenBuffer> buffer,
                 AddBufferType add_buffer_type);
  void RemoveBuffer(const OpenBuffer& buffer);
  std::vector<language::gc::Root<OpenBuffer>> GetAllBuffers() const;
  const language::gc::Root<OpenBuffer>& GetBuffer(size_t index) const;
  std::optional<size_t> GetBufferIndex(const OpenBuffer& buffer) const;
  size_t GetCurrentIndex();
  size_t BuffersCount() const;

  std::optional<language::gc::Root<OpenBuffer>> active_buffer() const;

  // See comments on `filter_`.
  void set_filter(
      std::optional<std::vector<language::gc::WeakPtr<OpenBuffer>>> filter);

  BufferWidget& GetActiveLeaf();
  const BufferWidget& GetActiveLeaf() const;

  LineWithCursor::Generator::Vector GetLines(
      Widget::OutputProducerOptions options) const;

  enum class BufferSortOrder { kAlphabetic, kLastVisit };
  void SetBufferSortOrder(BufferSortOrder buffer_sort_order);
  void SetBuffersToRetain(std::optional<size_t> buffers_to_retain);
  void SetBuffersToShow(std::optional<size_t> buffers_to_show);

  void Update();

 private:
  BuffersList(const EditorState& editor_state,
              language::NonNull<std::unique_ptr<BufferWidget>> buffer_widget);

  const EditorState& editor_state_;
  std::vector<language::gc::Root<OpenBuffer>> buffers_;

  // Points to the BufferWidget that corresponds to the active buffer.
  language::NonNull<BufferWidget*> active_buffer_widget_;

  // Contains the whole hierarchy of widgets.
  language::NonNull<std::unique_ptr<Widget>> widget_;

  // If it has a value, buffers not included will be dimmed (disabled).
  std::optional<std::vector<language::gc::WeakPtr<OpenBuffer>>> filter_;

  BufferSortOrder buffer_sort_order_ = BufferSortOrder::kLastVisit;
  std::optional<size_t> buffers_to_retain_ = {};
  // Must be always >0.
  std::optional<size_t> buffers_to_show_ = {};
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFERS_LIST_H__
