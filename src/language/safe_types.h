#ifndef __AFC_EDITOR_SAFE_TYPES_H__
#define __AFC_EDITOR_SAFE_TYPES_H__

#include <glog/logging.h>

#include <optional>

namespace afc::language {
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

template <typename P>
class NonNull {};

template <typename T>
class NonNull<std::unique_ptr<T>> {
 public:
  NonNull() : value_(std::make_unique<T>()) {}

  explicit NonNull(std::unique_ptr<T> value) : value_(std::move(value)) {
    CHECK(value_ != nullptr);
  };

  // Use the `Other` type for types where `std::unique_ptr<Other>` can be
  // converted to `std::unique_ptr<T>`.
  template <typename Other>
  NonNull(NonNull<std::unique_ptr<Other>> value)
      : value_(std::move(value.get_unique())) {
    CHECK(value_ != nullptr);
  }

  template <typename Arg>
  explicit NonNull(Arg arg) : value_(std::make_unique<T>(std::move(arg))) {}

  T* operator->() const { return value_.get(); }
  T* get() const { return value_.get(); }

  std::unique_ptr<T>& get_unique() { return value_; }

 private:
  std::unique_ptr<T> value_;
};

template <typename T>
class NonNull<std::shared_ptr<T>> {
 public:
  NonNull() : value_(std::make_shared<T>()) {}

  explicit NonNull(std::unique_ptr<T> value)
      : NonNull(std::shared_ptr<T>(std::move(value))) {}

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  NonNull(std::shared_ptr<Other> value) : value_(std::move(value)) {
    CHECK(value_ != nullptr);
  }

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  NonNull(NonNull<std::shared_ptr<Other>> value)
      : value_(std::move(value.get_shared())) {
    CHECK(value_ != nullptr);
  }

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  NonNull(NonNull<std::unique_ptr<Other>> value)
      : value_(std::move(value.get_unique())) {
    CHECK(value_ != nullptr);
  }

  template <typename Other>
  NonNull operator=(const NonNull<std::shared_ptr<Other>>& value) {
    value_ = value.get_shared();
    return *this;
  }

  template <typename Other>
  NonNull operator=(NonNull<std::unique_ptr<Other>> value) {
    value_ = std::move(value.get_unique());
    return *this;
  }

  T* operator->() const { return value_.get(); }
  T& operator*() const { return *value_; }
  T* get() const { return value_.get(); }

  const std::shared_ptr<T>& get_shared() const { return value_; }
  std::shared_ptr<T>& get_shared() { return value_; }

 private:
  std::shared_ptr<T> value_;
};

template <typename T>
bool operator==(const NonNull<std::shared_ptr<T>>& a,
                const NonNull<std::shared_ptr<T>>& b) {
  return a.get() == b.get();
}

template <typename T>
NonNull<std::shared_ptr<T>> MakeNonNull(std::shared_ptr<T> obj) {
  return NonNull<std::shared_ptr<T>>(std::move(obj));
}

template <typename T>
NonNull<std::unique_ptr<T>> MakeNonNull(std::unique_ptr<T> obj) {
  return NonNull<std::unique_ptr<T>>(std::move(obj));
}

template <typename T, typename... Arg>
NonNull<std::shared_ptr<T>> MakeNonNullShared(Arg... arg) {
  return MakeNonNull(std::make_shared<T>(arg...));
}

}  // namespace afc::language
#endif  // __AFC_EDITOR_SAFE_TYPES_H__
