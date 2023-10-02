#ifndef __AFC_EDITOR_SAFE_TYPES_H__
#define __AFC_EDITOR_SAFE_TYPES_H__

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <type_traits>

namespace afc::language {
// Wrapper that contains a pointer and information for how to de-reference it,
// and provides runtime checks to ensure that we'll crash if we de-reference a
// null/empty object.
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
  auto extractor = [p = std::move(p)] { return p.lock(); };
  return BoundPointer<decltype(extractor)>(std::move(extractor));
}

template <typename T>
auto Pointer(std::shared_ptr<T> p) {
  auto extractor = [p = std::move(p)] { return p; };
  return BoundPointer<decltype(extractor)>(std::move(extractor));
}

template <typename T>
auto Pointer(T* p) {
  auto extractor = [p] { return p; };
  return BoundPointer<decltype(extractor)>(std::move(extractor));
}

template <typename T>
auto Pointer(std::unique_ptr<T>& p) {
  auto extractor = [&p] { return p.get(); };
  return BoundPointer<decltype(extractor)>(std::move(extractor));
}

template <typename T, typename Callable>
void IfObj(std::weak_ptr<T> p, Callable callable) {
  auto value = p.lock();
  if (value != nullptr) callable(*value);
}

template <typename P>
class NonNull {};

template <typename T>
class NonNull<std::unique_ptr<T>>;
template <typename T>
class NonNull<std::shared_ptr<T>>;

// Generally, prefer using references (`T&`) over `NonNull<T*>`.
//
// One valid use case for `NonNull<T*>` is fields or variables that need to be
// mutable.
template <typename T>
class NonNull<T*> {
 public:
  static NonNull<T*> Unsafe(T* value) {
    CHECK(value != nullptr);
    return NonNull<T*>(value);
  }

  // Use the `Other` type for types that can be converted to `T`.
  template <typename Other>
  NonNull(NonNull<Other*> value) : value_(value.get()) {}

  template <typename Other>
  static NonNull<T*> AddressOf(Other& value) {
    return NonNull<T*>(&value);
  }

  T& value() const { return *value_; }
  T* operator->() const { return value_; }
  T* get() const { return value_; }

 private:
  friend class NonNull<std::unique_ptr<T>>;
  friend class NonNull<std::shared_ptr<T>>;

  explicit NonNull(T* value) : value_(value) {}

  T* value_;
};

template <typename T>
bool operator==(const NonNull<T*>& a, const NonNull<T*>& b) {
  return a.get() == b.get();
}

template <typename A>
bool operator<(const NonNull<A*>& a, const NonNull<A*>& b) {
  return a.get() < b.get();
}

template <typename T>
class NonNull<std::unique_ptr<T>> {
 public:
  using element_type = T;

  // Use the `Other` type for types where `std::unique_ptr<Other>` can be
  // converted to `std::unique_ptr<T>`.
  template <typename Other>
  static NonNull<std::unique_ptr<T>> Unsafe(std::unique_ptr<Other> value) {
    CHECK(value != nullptr);
    return NonNull(std::move(value));
  }

  NonNull() : value_(std::make_unique<T>()) {}

  // Use the `Other` type for types where `std::unique_ptr<Other>` can be
  // converted to `std::unique_ptr<T>`.
  template <typename Other>
  NonNull(NonNull<std::unique_ptr<Other>> value)
      : value_(std::move(value).get_unique()) {
    CHECK(value_ != nullptr);
  }

  template <typename... Arg>
  explicit NonNull(Arg&&... arg) : value_(std::make_unique<T>(arg...)) {}

  T& value() const { return *value_; }
  T* operator->() const { return value_.get(); }
  NonNull<T*> get() const { return NonNull<T*>(value_.get()); }
  NonNull<T*> release() { return NonNull<T*>(value_.release()); }
  std::unique_ptr<T> get_unique() && { return std::move(value_); }

 private:
  template <typename Other>
  explicit NonNull(std::unique_ptr<Other> value) : value_(std::move(value)) {}

  std::unique_ptr<T> value_;
};

template <typename T, typename D>
class NonNull<std::unique_ptr<T, D>> {
 public:
  using element_type = T;

  // Use the `Other` type for types where `std::unique_ptr<Other>` can be
  // converted to `std::unique_ptr<T>`.
  template <typename Other>
  static NonNull<std::unique_ptr<T, D>> Unsafe(
      std::unique_ptr<Other, D> value) {
    CHECK(value != nullptr);
    return NonNull(std::move(value));
  }

  NonNull() : value_(std::make_unique<T, D>()) {}

  // Use the `Other` type for types where `std::unique_ptr<Other>` can be
  // converted to `std::unique_ptr<T, D>`.
  template <typename Other>
  NonNull(NonNull<std::unique_ptr<Other>> value)
      : value_(std::move(value.get_unique())) {
    CHECK(value_ != nullptr);
  }

  template <typename... Arg>
  explicit NonNull(Arg&&... arg) : value_(std::make_unique<T, D>(arg...)) {}

  T& value() const { return *value_; }
  T* operator->() const { return value_.get(); }
  NonNull<T*> get() const { return NonNull<T*>(value_.get()); }
  NonNull<T*> release() { return NonNull<T*>(value_.release()); }
  std::unique_ptr<T, D>& get_unique() { return value_; }

 private:
  template <typename Other>
  explicit NonNull(std::unique_ptr<Other> value) : value_(std::move(value)) {}

  std::unique_ptr<T, D> value_;
};

template <typename T>
class NonNull<std::shared_ptr<T>> {
 public:
  NonNull() : value_(std::make_shared<T>()) {}

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  static NonNull<std::shared_ptr<T>> Unsafe(std::shared_ptr<Other> value) {
    CHECK(value != nullptr);
    return NonNull(std::move(value));
  }

  template <typename Other>
  static std::optional<NonNull<std::shared_ptr<T>>> DynamicCast(
      NonNull<std::shared_ptr<Other>> other_value) {
    return VisitPointer(
        dynamic_pointer_cast<T>(std::move(other_value.get_shared())),
        [](NonNull<std::shared_ptr<T>> value) {
          return std::optional<NonNull<std::shared_ptr<T>>>(value);
        },
        [] { return std::optional<NonNull<std::shared_ptr<T>>>(); });
  }

  template <typename Other>
  static NonNull<std::shared_ptr<T>> UnsafeStaticCast(
      NonNull<std::shared_ptr<Other>> value) {
    return NonNull<std::shared_ptr<T>>::Unsafe(
        static_pointer_cast<T>(std::move(value.get_shared())));
  }

  // Use the `Other` type for types where `std::shared_ptr<Other>` can be
  // converted to `std::shared_ptr<T>`.
  template <typename Other>
  static NonNull<std::shared_ptr<T>> Unsafe(std::unique_ptr<Other> value) {
    CHECK(value != nullptr);
    return NonNull(std::shared_ptr<Other>(std::move(value)));
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
      : value_(std::move(value).get_unique()) {}

  template <typename Other, typename Deleter>
  NonNull(NonNull<std::unique_ptr<Other, Deleter>> value)
      : value_(std::move(value).get_unique()) {}

  template <typename Other>
  NonNull operator=(const NonNull<std::shared_ptr<Other>>& value) {
    value_ = value.get_shared();
    return *this;
  }

  template <typename Other>
  NonNull operator=(NonNull<std::unique_ptr<Other>> value) {
    value_ = std::move(value).get_unique();
    return *this;
  }

  template <typename Q = T>
  typename std::enable_if<!std::is_same<Q, void>::value, Q&>::type value()
      const {
    return *value_;
  }
  T* operator->() const { return value_.get(); }
  NonNull<T*> get() const { return NonNull<T*>(value_.get()); }

  const std::shared_ptr<T>& get_shared() const { return value_; }
  std::shared_ptr<T>& get_shared() { return value_; }

 private:
  template <typename Other>
  explicit NonNull(std::shared_ptr<Other> value) : value_(std::move(value)) {}

  std::shared_ptr<T> value_;
};

template <typename T>
bool operator==(const NonNull<std::shared_ptr<T>>& a,
                const NonNull<std::shared_ptr<T>>& b) {
  return a.get() == b.get();
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
decltype(std::declval<Callable>()(std::declval<NonNull<T>>())) VisitPointer(
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

template <typename T, typename Callable, typename NullCallable>
auto VisitPointer(std::weak_ptr<T> t, Callable callable,
                  NullCallable null_callable) {
  return VisitPointer(t.lock(), std::move(callable), std::move(null_callable));
}

template <typename T, typename Callable, typename NullCallable>
decltype(std::declval<Callable>()(std::declval<T>())) VisitOptional(
    Callable callable, NullCallable null_callable, std::optional<T> t) {
  // Most of the time the non-null case returns a more generic type (typically a
  // ValueOrError vs an Error), so we assume that VisitPointer will return the
  // type of the `callable` (and let the return value of `null_callable` be
  // converted).
  if (t.has_value()) {
    return callable(std::move(t.value()));
  } else {
    return null_callable();
  }
}

template <typename T, typename Callable, typename NullCallable>
decltype(std::declval<Callable>()(std::declval<T>())) VisitPointer(
    std::optional<T> t, Callable callable, NullCallable null_callable) {
  return VisitOptional(callable, null_callable, t);
}

template <typename Overloads>
auto VisitOptionalCallback(Overloads overloads) {
  return [overloads](
             auto value) -> decltype(std::declval<Overloads>()(value.value())) {
    if (value.has_value()) {
      return overloads(std::move(value.value()));
    } else {
      return overloads();
    }
  };
}

}  // namespace afc::language
namespace std {
template <typename T>
struct hash<afc::language::NonNull<T*>> {
  std::size_t operator()(const afc::language::NonNull<T*>& ptr) const {
    return hash<T*>()(ptr.get());
  }
};
}  // namespace std
#endif  // __AFC_EDITOR_SAFE_TYPES_H__
