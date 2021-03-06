#ifndef __AFC_EDITOR_BUFFERS_LIST_H__
#define __AFC_EDITOR_BUFFERS_LIST_H__

#include <list>
#include <memory>
#include <vector>

#include "src/output_producer.h"
#include "src/widget.h"

namespace afc::editor {

// Divides the screen vertically into two sections: at the top, displays a given
// widget. At the bottom, displays a numbered list of buffers.
class BuffersList : public DelegatingWidget {
 public:
  BuffersList(const EditorState* editor_state, std::unique_ptr<Widget> widget);
  enum class AddBufferType { kVisit, kOnlyList, kIgnore };
  void AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                 AddBufferType add_buffer_type);
  void RemoveBuffer(OpenBuffer* buffer);
  std::vector<std::shared_ptr<OpenBuffer>> GetAllBuffers() const;
  std::shared_ptr<OpenBuffer> GetBuffer(size_t index);
  std::optional<size_t> GetBufferIndex(const OpenBuffer* buffer) const;
  size_t GetCurrentIndex();
  size_t BuffersCount() const;

  // See comments on `filter_`.
  void set_filter(std::optional<std::vector<std::weak_ptr<OpenBuffer>>> filter);

  // Overrides from Widget
  wstring Name() const override;
  wstring ToString() const override;

  BufferWidget* GetActiveLeaf() override;
  const BufferWidget* GetActiveLeaf() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer(
      OutputProducerOptions options) const override;

  LineNumberDelta MinimumLines() const override;

  size_t CountLeaves() const override;

  int AdvanceActiveLeafWithoutWrapping(int delta) override;
  void SetActiveLeavesAtStart() override;

  // Overrides from DelegatingWidget:
  Widget* Child() override;
  void SetChild(std::unique_ptr<Widget> widget) override;
  void WrapChild(std::function<std::unique_ptr<Widget>(std::unique_ptr<Widget>)>
                     callback) override;

 private:
  const EditorState* const editor_state_;
  std::map<wstring, std::shared_ptr<OpenBuffer>> buffers_;
  std::unique_ptr<Widget> widget_;

  // If it has a value, buffers not included will be dimmed (disabled).
  std::optional<std::vector<std::weak_ptr<OpenBuffer>>> filter_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFERS_LIST_H__
