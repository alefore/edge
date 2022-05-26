#ifndef __AFC_LANGUAGE_GC_H__
#define __AFC_LANGUAGE_GC_H__
#include <glog/logging.h>

#include <functional>
#include <list>
#include <memory>
#include <unordered_set>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/language/safe_types.h"

namespace afc::language::gc {

template <typename T>
class Ptr;

template <typename T>
class WeakPtr;

template <typename T>
class Root;

class Pool;

// The object metadata, allocated once per object managed. This is an internal
// class; the reason we expose it here is to allow implementation of the
// `Expand` functions.
//
// This is agnostic to the type of the contained value. In fact, it can't even
// retrieve the original value.
//
// The only `std::shared_ptr<>` reference (in the entire hierarchy of classes in
// afc::language::gc) to the objects managed is kept inside
// `ObjectMetadata::expand_callback`. Everything else just keeps
// `std::weak_ptr<>` references. By clearing up the `expand_callback`, we
// effectively allow objects to be deleted.
class ObjectMetadata {
 private:
  friend Pool;

  using ExpandCallback = std::function<
      std::vector<language::NonNull<std::shared_ptr<ObjectMetadata>>>()>;

  struct ConstructorAccessKey {
   private:
    ConstructorAccessKey() = default;
    friend Pool;
  };

 public:
  ObjectMetadata(ConstructorAccessKey, Pool& pool,
                 ExpandCallback expand_callback);

  Pool& pool() const;

  bool IsAlive() const;

 private:
  Pool& pool_;
  struct Data {
    ExpandCallback expand_callback;
    bool reached = false;
  };
  concurrent::Protected<Data> data_;
};

class Pool {
 public:
  template <typename T>
  Root<T> NewRoot(language::NonNull<std::unique_ptr<T>> value_unique) {
    language::NonNull<std::shared_ptr<T>> value = std::move(value_unique);
    return Ptr<T>(
               std::weak_ptr<T>(value.get_shared()), NewObjectMetadata([value] {
                 std::vector<language::NonNull<std::shared_ptr<ObjectMetadata>>>
                 Expand(const T&);
                 return Expand(value.value());
               }))
        .ToRoot();
  }

  ~Pool();

  struct ReclaimObjectsStats {
    size_t roots = 0;
    size_t begin_total = 0;
    size_t begin_dead = 0;
    size_t end_total = 0;
    size_t generations = 0;
  };
  ReclaimObjectsStats Reclaim();

  using RootRegistration = std::shared_ptr<bool>;

 private:
  template <typename T>
  friend class Root;

  language::NonNull<std::shared_ptr<ObjectMetadata>> NewObjectMetadata(
      ObjectMetadata::ExpandCallback expand_callback);

  RootRegistration AddRoot(std::weak_ptr<ObjectMetadata> object_metadata);

  using ObjectsMetadataVector = std::vector<std::weak_ptr<ObjectMetadata>>;
  struct Generation {
    bool IsEmpty() const;

    // All the object metadata for all the objects allocated into this pool.
    ObjectsMetadataVector object_metadata;

    // Weak ownership of the object metadata for all the roots.
    std::list<std::weak_ptr<ObjectMetadata>> roots;

    struct RootDeleted {
      NonNull<Generation*> generation;
      std::list<std::weak_ptr<ObjectMetadata>>::iterator it;
    };
    std::vector<RootDeleted> roots_deleted;
  };

  std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>>
  RegisterAllRoots(
      const std::list<NonNull<std::unique_ptr<Generation>>>& generations);

  void RegisterRoots(
      const Generation& generation,
      std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>>& output);

  void MarkReachable(
      std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>> expand);

  concurrent::Protected<NonNull<std::unique_ptr<Generation>>>
      current_generation_;

  // generations_ holds a list of old generations. A new generation is created
  // on each call to `Reclaim`. We do this so that we don't have to take the
  // entire lock on `current_generation_` for the duration of the collection; in
  // the future, that may enable us to even run the collection asynchronously
  // (in a dedicated thread).
  concurrent::Protected<std::list<NonNull<std::unique_ptr<Generation>>>>
      generations_;
};

std::ostream& operator<<(std::ostream& os,
                         const Pool::ReclaimObjectsStats& stats);

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
      : value_(other.value_), object_metadata_(other.object_metadata_) {}

  template <typename U>
  Ptr<T>& operator=(const Ptr<U>& other) {
    value_ = other.value_;
    object_metadata_ = other.object_metadata_;
    return *this;
  }

  template <typename U>
  Ptr<T>& operator=(Ptr<U>&& other) {
    value_ = std::move(other.value_);
    object_metadata_ = std::move(other.object_metadata_);
    return *this;
  }

  Pool& pool() const { return object_metadata_->pool(); }

  T* operator->() const {
    std::shared_ptr<T> locked_value = value_.lock();
    CHECK(locked_value != nullptr);
    return locked_value.get();
  }
  T& value() const { return language::Pointer(value_.lock()).Reference(); }

  // This is only exposed in order to allow implementation of `Expand`
  // functions.
  language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata() const {
    return object_metadata_;
  }

 private:
  friend Pool;

  template <typename U>
  friend class Root;

  template <typename U>
  friend class WeakPtr;

  template <typename U>
  friend class Ptr;

  template <typename U>
  Ptr(std::weak_ptr<U> value,
      language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata)
      : value_(value), object_metadata_(object_metadata) {
    VLOG(5) << "Ptr(pool, value): " << object_metadata_.get_shared()
            << " (value: " << value_.lock() << ")";
  }

  // We keep only a weak reference to the value here, locking it each time. The
  // real reference is kept inside ObjectMetadata::expand_callback_. The
  // ownership is shared through the shared ownership of the object metadata.
  //
  // When the last Ptr instance to a given value and object metadata are
  // dropped, the ObjectMetadata is destroyed, allowing the object to be
  // collected.
  //
  // If the MemoryPool detects that an object is no longer reachable, it will
  // trigger its collection by overriding `ObjectMetadata::expand_callback_`.
  std::weak_ptr<T> value_;
  language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata_;
};

template <typename T>
class WeakPtr {
 public:
  WeakPtr() = default;

  std::optional<Root<T>> Lock() const {
    return VisitPointer(
        object_metadata_,
        [&](language::NonNull<std::shared_ptr<ObjectMetadata>>
                object_metadata) {
          return object_metadata->IsAlive()
                     ? Ptr<T>(value_, object_metadata).ToRoot()
                     : std::optional<Root<T>>();
        },
        [] { return std::optional<Root<T>>(); });
  }

 private:
  friend class Ptr<T>;
  WeakPtr(Ptr<T> ptr)
      : value_(ptr.value_),
        object_metadata_(ptr.object_metadata_.get_shared()) {}

  std::weak_ptr<T> value_;
  std::weak_ptr<ObjectMetadata> object_metadata_;
};

template <typename T>
class Root {
 public:
  template <typename U>
  Root(const Root<U>& other) : Root(other.ptr_) {}

  template <typename U>
  Root(Root<U>&& other)
      : ptr_(std::move(other.ptr_)),
        registration_(pool().AddRoot(ptr_.object_metadata_.get_shared())) {}

  template <typename U>
  Root<T>& operator=(Root<U>&& other) {
    CHECK(this != &other);
    std::swap(ptr_.value_, other.ptr_.value_);
    std::swap(ptr_.object_metadata_, other.ptr_.object_metadata_);
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

  Root(const Ptr<T>& ptr)
      : ptr_(ptr),
        registration_(ptr_.pool().AddRoot(ptr_.object_metadata_.get_shared())) {
  }

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
