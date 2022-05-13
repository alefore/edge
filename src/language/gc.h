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

  Ptr(const Ptr<T>& other)
      : value_(other.value_), control_frame_(other.control_frame_) {}

  Ptr<T>& operator=(const Ptr<T>& other) {
    value_ = other.value_;
    control_frame_ = other.control_frame_;
    return *this;
  }

  Ptr<T>& operator=(Ptr<T>&& other) {
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
  static Ptr<T> New(Pool& pool, language::NonNull<std::unique_ptr<T>> value) {
    language::NonNull<std::shared_ptr<T>> shared_value = std::move(value);
    std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> Expand(
        const T&);
    return Ptr(shared_value.get_shared(),
               language::MakeNonNullShared<ControlFrame>(
                   ControlFrame::ConstructorAccessKey(), pool,
                   [shared_value] { return Expand(shared_value.value()); }));
  }

  friend class Root<T>;

  Ptr(std::shared_ptr<T> value,
      language::NonNull<std::shared_ptr<ControlFrame>> control_frame)
      : value_(value), control_frame_(control_frame) {
    VLOG(5) << "Ptr(pool, value): " << control_frame.get_shared()
            << " (value: " << value << ")";
    pool().AddObj(control_frame);
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
class Root {
 public:
  Root(const Root<T>& other) : Root(other.ptr_) {}

  Root(Root<T>&& other)
      : ptr_(std::move(other.ptr_)),
        registration_(pool().AddRoot(ptr_.control_frame_.get_shared())) {}

  Root<T>& operator=(Root<T>&& other) {
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

  Root(Pool& pool, language::NonNull<std::unique_ptr<T>> value)
      : Root(Ptr<T>::New(pool, std::move(value))) {}

  Root(const Ptr<T>& ptr)
      : ptr_(ptr),
        registration_(ptr_.pool().AddRoot(ptr_.control_frame_.get_shared())) {}

  Ptr<T> ptr_;
  Pool::RootRegistration registration_;
};

};  // namespace afc::language::gc
#endif
