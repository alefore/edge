#include "src/buffer_tree.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
BufferTreeLeaf::BufferTreeLeaf(ConstructorAccessTag,
                               std::weak_ptr<OpenBuffer> buffer)
    : leaf_(buffer) {}

/* static */
std::unique_ptr<BufferTreeLeaf> BufferTreeLeaf::New(
    std::weak_ptr<OpenBuffer> buffer) {
  return std::make_unique<BufferTreeLeaf>(ConstructorAccessTag(), buffer);
}

std::shared_ptr<OpenBuffer> BufferTreeLeaf::LockActiveLeaf() const {
  return leaf_.lock();
}

void BufferTreeLeaf::SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) {
  leaf_ = std::move(buffer);
}

void BufferTreeLeaf::SetActiveLeaf(size_t) {}

void BufferTreeLeaf::AdvanceActiveLeaf(int) {}

size_t BufferTreeLeaf::CountLeafs() const { return 1; }

wstring BufferTreeLeaf::Name() const {
  auto buffer = LockActiveLeaf();
  return buffer == nullptr ? L"" : buffer->Read(buffer_variables::name());
}

wstring BufferTreeLeaf::ToString() const {
  auto buffer = leaf_.lock();
  return L"[buffer tree leaf" +
         (buffer == nullptr ? L"nullptr"
                            : buffer->Read(buffer_variables::name())) +
         L"]";
}

std::unique_ptr<OutputProducer> BufferTreeLeaf::CreateOutputProducer() {
  auto buffer = LockActiveLeaf();
  if (buffer == nullptr) {
    return nullptr;
  }
  return std::make_unique<BufferOutputProducer>(buffer);
}

std::ostream& operator<<(std::ostream& os, const BufferTree& lc) {
  os << lc.ToString();
  return os;
}

}  // namespace editor
}  // namespace afc
