#ifndef __AFC_LANGUAGE_GC_H__
#define __AFC_LANGUAGE_GC_H__
#include <glog/logging.h>

#include <functional>
#include <list>
#include <memory>
#include <vector>

#include "src/language/safe_types.h"

namespace afc::language::gc {

class ControlFrame;

template <typename T>
class Ptr;

template <typename T>
class WeakPtr;

template <typename T>
class Root;

class Pool {
 public:
  template <typename T>
  Root<T> NewRoot(language::NonNull<std::unique_ptr<T>> value) {
    return Root<T>(*this, std::move(value));
  }

  ~Pool();

  struct ReclaimObjectsStats {
    size_t roots = 0;
    size_t begin_total = 0;
    size_t begin_dead = 0;
    size_t end_total = 0;
  };
  ReclaimObjectsStats Reclaim();

  using RootRegistration = std::shared_ptr<bool>;

  RootRegistration AddRoot(std::weak_ptr<ControlFrame> control_frame);

  void AddObj(language::NonNull<std::shared_ptr<ControlFrame>> control_frame);

 private:
  // All the control frames for all the objects allocated into this pool.
  std::vector<std::weak_ptr<ControlFrame>> objects_;

  // Weak ownership of the control frames for all the roots.
  std::list<std::weak_ptr<ControlFrame>> roots_;
};

std::ostream& operator<<(std::ostream& os,
                         const Pool::ReclaimObjectsStats& stats);

// The control frame, allocated once per object managed. This is an internal
// class; the reason we expose it here is to allow implementation of the
// `Expand` functions.
//
// This is agnostic to the type of the contained value.
struct ControlFrame {
 private:
  friend Pool;

  template <typename T>
  friend class Ptr;

  template <typename T>
  friend class WeakPtr;

  struct ConstructorAccessKey {};

  using ExpandCallback = std::function<
      std::vector<language::NonNull<std::shared_ptr<ControlFrame>>>()>;

 public:
  ControlFrame(ConstructorAccessKey, Pool& pool, ExpandCallback expand_callback)
      : pool_(pool), expand_callback_(std::move(expand_callback)) {}

 private:
  Pool& pool() const { return pool_; }

  Pool& pool_;
  ExpandCallback expand_callback_;
  bool reached_ = false;
};

// A mutable pointer with shared ownership of a managed object. Behaves very
// much like `std::shared_ptr<T>`: when the number of references drops to 0, the
// object is reclaimed. The object will also be reclaimed by Pool::Reclaim when
// it's not reachable from a root.
//
// One notable difference with `std::shared_ptr` is that we deliberately don't
// support null ptr. If a customer needs some "ocassionally null" pointers, they
// can wrap `Ptr` in `std::optional` instead (e.g., `std::optional<Ptr<T>>`).
template <typename T>
class Ptr {
 public:
  Root<T> ToRoot() const { return Root<T>(*this); }

  WeakPtr<T> ToWeakPtr() const { return WeakPtr<T>(*this); }

  template <typename U>
  Ptr(const Ptr<U>& other)
      : value_(other.value_), control_frame_(other.control_frame_) {}

  template <typename U>
  Ptr<T>& operator=(const Ptr<U>& other) {
    value_ = other.value_;
    control_frame_ = other.control_frame_;
    return *this;
  }

  template <typename U>
  Ptr<T>& operator=(Ptr<U>&& other) {
    value_ = std::move(other.value_);
    control_frame_ = std::move(other.control_frame_);
    return *this;
  }

  Pool& pool() const { return control_frame_->pool(); }

  T* operator->() const { return value_.lock().get(); }
  T& value() const { return language::Pointer(value_.lock()).Reference(); }

  // This is only exposed in order to allow implementation of `Expand`
  // functions.
  language::NonNull<std::shared_ptr<ControlFrame>> control_frame() const {
    return control_frame_;
  }

 private:
  static Ptr<T> New(Pool& pool, language::NonNull<std::shared_ptr<T>> value) {
    auto control_frame = language::MakeNonNullShared<ControlFrame>(
        ControlFrame::ConstructorAccessKey(), pool, [value] {
          std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> Expand(
              const T&);
          return Expand(value.value());
        });
    pool.AddObj(control_frame);
    return Ptr(std::weak_ptr<T>(value.get_shared()), control_frame);
  }

  template <typename U>
  friend class Root;

  template <typename U>
  friend class WeakPtr;

  template <typename U>
  friend class Ptr;

  template <typename U>
  Ptr(std::weak_ptr<U> value,
      language::NonNull<std::shared_ptr<ControlFrame>> control_frame)
      : value_(value), control_frame_(control_frame) {
    VLOG(5) << "Ptr(pool, value): " << control_frame_.get_shared()
            << " (value: " << value_.lock() << ")";
  }

  // We keep only a weak reference to the value here, locking it each time. The
  // real reference is kept inside ControlFrame::expand_callback_. The ownership
  // is shared through the shared ownership of the control frame.
  //
  // When the last Ptr instance to a given value and frame are dropped, the
  // ControlFrame is destroyed, allowing the object to be collected.
  //
  // If the MemoryPool detects that an object is no longer reachable, it will
  // trigger its collection by overriding `ControlFrame::expand_callback_`.
  std::weak_ptr<T> value_;
  language::NonNull<std::shared_ptr<ControlFrame>> control_frame_;
};

template <typename T>
class WeakPtr {
 public:
  WeakPtr() = default;

  std::optional<Root<T>> Lock() const {
    return VisitPointer(
        control_frame_,
        [&](language::NonNull<std::shared_ptr<ControlFrame>> control_frame) {
          return control_frame->expand_callback_ == nullptr
                     ? std::optional<Root<T>>()
                     : Ptr<T>(value_, control_frame).ToRoot();
        },
        [] { return std::optional<Root<T>>(); });
  }

 private:
  friend class Ptr<T>;
  WeakPtr(Ptr<T> ptr)
      : value_(ptr.value_), control_frame_(ptr.control_frame_.get_shared()) {}

  std::weak_ptr<T> value_;
  std::weak_ptr<ControlFrame> control_frame_;
};

template <typename T>
class Root {
 public:
  template <typename U>
  Root(const Root<U>& other) : Root(other.ptr_) {}

  template <typename U>
  Root(Root<U>&& other)
      : ptr_(std::move(other.ptr_)),
        registration_(pool().AddRoot(ptr_.control_frame_.get_shared())) {}

  template <typename U>
  Root<T>& operator=(Root<U>&& other) {
    CHECK(this != &other);
    std::swap(ptr_.value_, other.ptr_.value_);
    std::swap(ptr_.control_frame_, other.ptr_.control_frame_);
    std::swap(registration_, other.registration_);
    return *this;
  }

  Root<T>& operator=(const Root<T>& other) = default;

  Pool& pool() const { return ptr_.pool(); }

  Ptr<T>& ptr() { return ptr_; }
  const Ptr<T>& ptr() const { return ptr_; }

 private:
  friend class Pool;
  friend class Ptr<T>;

  template <typename U>
  friend class Root;

  Root(Pool& pool, language::NonNull<std::unique_ptr<T>> value)
      : Root(Ptr<T>::New(pool, std::move(value))) {}

  Root(const Ptr<T>& ptr)
      : ptr_(ptr),
        registration_(ptr_.pool().AddRoot(ptr_.control_frame_.get_shared())) {}

  Ptr<T> ptr_;
  Pool::RootRegistration registration_;
};

template <typename T>
bool operator==(const Root<T>& a, const Root<T>& b) {
  return &a.ptr().value() == &b.ptr().value();
}

template <typename T>
bool operator<(const Root<T>& a, const Root<T>& b) {
  return &a.ptr().value() < &b.ptr().value();
}
};  // namespace afc::language::gc
#endif
