#ifndef __AFC_EDITOR_BUFFERS_LIST_H__
#define __AFC_EDITOR_BUFFERS_LIST_H__

#include <list>
#include <memory>
#include <vector>

#include "src/output_producer.h"
#include "src/widget.h"

namespace afc {
namespace editor {

// Divides the screen vertically into two sections: at the top, displays a given
// widget. At the bottom, displays a list of buffers.
class BuffersList : public DelegatingWidget {
 public:
  BuffersList(std::unique_ptr<Widget> widget);
  enum class AddBufferType { kVisit, kOnlyList, kIgnore };
  void AddBuffer(std::shared_ptr<OpenBuffer> buffer,
                 AddBufferType add_buffer_type);
  std::shared_ptr<OpenBuffer> GetBuffer(size_t index);
  std::optional<size_t> GetBufferIndex(const OpenBuffer* buffer) const;
  size_t GetCurrentIndex();
  size_t BuffersCount() const;

  // Overrides from Widget
  wstring Name() const override;
  wstring ToString() const override;

  BufferWidget* GetActiveLeaf() override;
  const BufferWidget* GetActiveLeaf() const override;
  void ForEachBufferWidget(
      std::function<void(BufferWidget*)> callback) override;
  void ForEachBufferWidgetConst(
      std::function<void(const BufferWidget*)> callback) const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void SetSize(LineColumnDelta size) override;
  LineNumberDelta MinimumLines() override;

  void RemoveBuffer(OpenBuffer* buffer) override;

  size_t CountLeaves() const override;

  int AdvanceActiveLeafWithoutWrapping(int delta) override;
  void SetActiveLeavesAtStart() override;

  // Overrides from DelegatingWidget:
  Widget* Child() override;
  void SetChild(std::unique_ptr<Widget> widget) override;
  void WrapChild(std::function<std::unique_ptr<Widget>(std::unique_ptr<Widget>)>
                     callback) override;

 private:
  std::map<wstring, std::shared_ptr<OpenBuffer>> buffers_;
  std::unique_ptr<Widget> widget_;

  // Fields initialized by SetSize.
  LineColumnDelta size_;
  LineNumberDelta buffers_list_lines_;
  size_t buffers_per_line_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFERS_LIST_H__
