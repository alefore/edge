#ifndef __AFC_EDITOR_SAFE_TYPES_H__
#define __AFC_EDITOR_SAFE_TYPES_H__

namespace afc::editor {
template <typename Extractor>
class BoundPointer {
 public:
  BoundPointer(Extractor extractor) : extractor_(std::move(extractor)) {}

  auto& Reference() {
    auto value = extractor_();
    CHECK(value != nullptr);
    return *value;
  }

  template <typename Callable>
  auto& IfNotNull(Callable callable) {
    if (auto value = extractor_(); value != nullptr) callable(*value);
    return *this;
  }

 private:
  Extractor extractor_;
};

template <typename T>
auto Pointer(std::weak_ptr<T> p) {
  return BoundPointer([p] { return p.lock(); });
}

template <typename T>
auto Pointer(std::shared_ptr<T> p) {
  return BoundPointer([p] { return p; });
}

template <typename T>
auto Pointer(T* p) {
  return BoundPointer([p] { return p; });
}

template <typename T>
auto Pointer(std::unique_ptr<T>& p) {
  return Pointer(p.get());
}

template <typename T, typename Callable>
void IfObj(std::weak_ptr<T> p, Callable callable) {
  auto value = p.lock();
  if (value != nullptr) callable(*value);
}

template <typename T, typename Callable>
void IfObj(std::optional<T> p, Callable callable) {
  if (p.has_value()) callable(*p);
}

}  // namespace afc::editor
#endif  // __AFC_EDITOR_SAFE_TYPES_H__
