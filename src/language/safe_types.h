#ifndef __AFC_EDITOR_SAFE_TYPES_H__
#define __AFC_EDITOR_SAFE_TYPES_H__

#include <glog/logging.h>

#include <memory>
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
  struct UnsafeConstructorKey {};

 public:
  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  static NonNull<std::unique_ptr<T>> Unsafe(std::unique_ptr<Other> value) {
    CHECK(value != nullptr);
    return NonNull(UnsafeConstructorKey(), std::move(value));
  }

  NonNull() : value_(std::make_unique<T>()) {}

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
  T& operator*() const { return *value_; }
  T* get() const { return value_.get(); }

  std::unique_ptr<T>& get_unique() { return value_; }

 private:
  template <typename Other>
  NonNull(UnsafeConstructorKey, std::unique_ptr<Other> value)
      : value_(std::move(value)) {}

  std::unique_ptr<T> value_;
};

template <typename T>
class NonNull<std::shared_ptr<T>> {
  struct UnsafeConstructorKey {};

 public:
  NonNull() : value_(std::make_shared<T>()) {}

  explicit NonNull(std::unique_ptr<T> value)
      : NonNull(std::shared_ptr<T>(std::move(value))) {}

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  static NonNull<std::shared_ptr<T>> Unsafe(std::shared_ptr<Other> value) {
    CHECK(value != nullptr);
    return NonNull(UnsafeConstructorKey(), std::move(value));
  }

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  static NonNull<std::shared_ptr<T>> Unsafe(std::unique_ptr<Other> value) {
    CHECK(value != nullptr);
    return NonNull(UnsafeConstructorKey(),
                   std::shared_ptr<Other>(std::move(value)));
  }

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  NonNull(NonNull<std::shared_ptr<Other>> value)
      : value_(std::move(value.get_shared())) {}

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  NonNull(NonNull<std::unique_ptr<Other>> value)
      : value_(std::move(value.get_unique())) {}

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
  template <typename Other>
  NonNull(UnsafeConstructorKey, std::shared_ptr<Other> value)
      : value_(std::move(value)) {}

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
NonNull<std::unique_ptr<T>> MakeNonNullUnique(Arg&&... arg) {
  return NonNull<std::unique_ptr<T>>::Unsafe(
      std::make_unique<T>(std::forward<Arg>(arg)...));
}

template <typename T, typename... Arg>
NonNull<std::shared_ptr<T>> MakeNonNullShared(Arg&&... arg) {
  return NonNull<std::shared_ptr<T>>::Unsafe(
      std::make_shared<T>(std::forward<Arg>(arg)...));
}

template <typename T, typename Callable, typename NullCallable>
decltype(std::declval<Callable>()(NonNull<T>::Unsafe(T()))) VisitPointer(
    T t, Callable callable, NullCallable null_callable) {
  // Most of the time the non-null case returns a more generic type (typically a
  // ValueOrError vs an Error), so we assume that VisitPointer will return the
  // type of the `callable` (and let the return value of `null_callable` be
  // converted).
  if (t == nullptr) {
    return null_callable();
  } else {
    return callable(NonNull<T>::Unsafe(std::move(t)));
  }
}

}  // namespace afc::language
#endif  // __AFC_EDITOR_SAFE_TYPES_H__
