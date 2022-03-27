#ifndef __AFC_EDITOR_BUFFERS_LIST_H__
#define __AFC_EDITOR_BUFFERS_LIST_H__

#include <list>
#include <memory>
#include <vector>

#include "src/buffer_widget.h"
#include "src/output_producer.h"
#include "src/widget.h"

namespace afc::editor {

// Divides the screen vertically into two sections: at the top, displays a given
// widget. At the bottom, displays a numbered list of buffers.
class BuffersList {
 public:
  BuffersList(const EditorState& editor_state);
  enum class AddBufferType { kVisit, kOnlyList, kIgnore };
  void AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                 AddBufferType add_buffer_type);
  void RemoveBuffer(OpenBuffer* buffer);
  std::vector<std::shared_ptr<OpenBuffer>> GetAllBuffers() const;
  std::shared_ptr<OpenBuffer> GetBuffer(size_t index);
  std::optional<size_t> GetBufferIndex(const OpenBuffer* buffer) const;
  size_t GetCurrentIndex();
  size_t BuffersCount() const;

  std::shared_ptr<OpenBuffer> active_buffer() const;

  // See comments on `filter_`.
  void set_filter(std::optional<std::vector<std::weak_ptr<OpenBuffer>>> filter);

  BufferWidget* GetActiveLeaf();
  const BufferWidget* GetActiveLeaf() const;

  LineWithCursor::Generator::Vector GetLines(
      Widget::OutputProducerOptions options) const;

  enum class BufferSortOrder { kAlphabetic, kLastVisit };
  void SetBufferSortOrder(BufferSortOrder buffer_sort_order);
  void SetBuffersToRetain(std::optional<size_t> buffers_to_retain);
  void SetBuffersToShow(std::optional<size_t> buffers_to_show);

  void Update();

 private:
  const EditorState& editor_state_;
  std::vector<std::shared_ptr<OpenBuffer>> buffers_;
  // Contains the whole hierarchy of widgets.
  std::unique_ptr<Widget> widget_;
  // Points to the BufferWidget that corresponds to the active buffer.
  BufferWidget* active_buffer_widget_ = nullptr;

  // If it has a value, buffers not included will be dimmed (disabled).
  std::optional<std::vector<std::weak_ptr<OpenBuffer>>> filter_;

  BufferSortOrder buffer_sort_order_ = BufferSortOrder::kLastVisit;
  std::optional<size_t> buffers_to_retain_ = {};
  // Must be always >0.
  std::optional<size_t> buffers_to_show_ = {};
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFERS_LIST_H__
