#ifndef __AFC_EDITOR_BUFFER_TREE_H__
#define __AFC_EDITOR_BUFFER_TREE_H__

#include <list>
#include <memory>

#include "src/lazy_string.h"
#include "src/output_producer.h"
#include "src/parse_tree.h"
#include "src/tree.h"
#include "src/vm/public/environment.h"

namespace afc {
namespace editor {

class BufferTree {
 public:
  ~BufferTree() = default;

  virtual std::shared_ptr<OpenBuffer> LockActiveLeaf() const = 0;

  virtual void SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) = 0;

  virtual void SetActiveLeaf(size_t position) = 0;

  // Move the active leaf by this number of positions.
  virtual void AdvanceActiveLeaf(int delta) = 0;

  virtual size_t CountLeafs() const = 0;

  virtual wstring Name() const = 0;
  virtual wstring ToString() const = 0;

  virtual std::unique_ptr<OutputProducer> CreateOutputProducer() = 0;
};

class BufferTreeLeaf : public BufferTree {
 private:
  struct ConstructorAccessTag {};

 public:
  static std::unique_ptr<BufferTreeLeaf> New(std::weak_ptr<OpenBuffer> buffer);

  BufferTreeLeaf(ConstructorAccessTag, std::weak_ptr<OpenBuffer> buffer);

  std::shared_ptr<OpenBuffer> LockActiveLeaf() const override;

  void SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) override;
  void SetActiveLeaf(size_t position) override;
  void AdvanceActiveLeaf(int delta) override;

  size_t CountLeafs() const override;

  wstring Name() const override;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

 private:
  std::weak_ptr<OpenBuffer> leaf_;
};

class BufferTreeHorizontal : public BufferTree {
 private:
  struct ConstructorAccessTag {};

 public:
  static std::unique_ptr<BufferTreeHorizontal> New(
      std::vector<std::unique_ptr<BufferTree>> children, size_t active);

  BufferTreeHorizontal(ConstructorAccessTag,
                       std::vector<std::unique_ptr<BufferTree>> children,
                       size_t active);

  static std::unique_ptr<BufferTree> AddHorizontalSplit(
      std::unique_ptr<BufferTree> tree);

  // `tree` may be of any type (not only BufferTreeHorizontal).
  static std::unique_ptr<BufferTree> RemoveActiveLeaf(
      std::unique_ptr<BufferTree> tree);

  std::shared_ptr<OpenBuffer> LockActiveLeaf() const override;

  void SetActiveLeafBuffer(std::shared_ptr<OpenBuffer> buffer) override;
  void SetActiveLeaf(size_t position) override;
  void AdvanceActiveLeaf(int delta) override;

  size_t CountLeafs() const override;

  wstring Name() const override;
  wstring ToString() const override;

  std::unique_ptr<OutputProducer> CreateOutputProducer() override;

  void PushChildren(std::unique_ptr<BufferTree> children);
  size_t children_count() const;

 private:
  // Doesn't wrap. Returns the number of steps pending.
  int AdvanceActiveLeafWithoutWrapping(int delta);
  static std::unique_ptr<BufferTree> RemoveActiveLeafInternal(
      std::unique_ptr<BufferTree> tree);

  std::vector<std::unique_ptr<BufferTree>> children_;
  size_t active_;
};

std::ostream& operator<<(std::ostream& os, const BufferTree& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TREE_H__
