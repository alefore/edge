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

BufferTreeLeaf* BufferTreeLeaf::GetActiveLeaf() { return this; }

void BufferTreeLeaf::SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) {
  leaf_ = std::move(buffer);
  SetLines(lines_);  // Causes things to be recomputed.
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
  return std::make_unique<BufferOutputProducer>(buffer, lines_);
}

void BufferTreeLeaf::SetLines(size_t lines) {
  lines_ = lines;
  auto buffer = leaf_.lock();
  if (buffer != nullptr) {
    buffer->set_lines_for_zoomed_out_tree(lines);
  }
}

size_t BufferTreeLeaf::lines() const { return lines_; }

size_t BufferTreeLeaf::MinimumLines() {
  auto buffer = LockActiveLeaf();
  return buffer == nullptr
             ? 0
             : max(0,
                   buffer->Read(buffer_variables::buffer_list_context_lines()));
}

std::ostream& operator<<(std::ostream& os, const BufferTree& lc) {
  os << lc.ToString();
  return os;
}

}  // namespace editor
}  // namespace afc
