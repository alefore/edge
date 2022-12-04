// Module implementing incremental garbage collection.
//
// The main motivation is to support hierarchies of objects that may contain
// loops. By (1) explicitly keeping track of "roots" (entry points that should
// not be collected), and (2) requiring that contained types can be queried
// about their references, we can detect unreachable objects (including cycles)
// and collect them. In the absence of cycles, objects are deleted as soon as
// they become unreachable.
//
// Exposes the following public types:
//
// * `Pool`: A container for pointers, through which collection can be triggered
//   through `Pool::Collect`. References inside objects can't cross pool
//   boundaries: they should only refer to objects in the same pool.
//
// * `Ptr<>`: A pointer to a managed object. The value can be obtained through
//   `Ptr::value` or the `->` operator. This is similar to `std::shared_ptr<>`.
//
// * `Root<>`: Similar to `Ptr`, but ensures that objects pointed to by roots
//   are always kept alive, even if they aren't otherwise reachable. A `Root`
//   for a `Ptr` can be created through `Ptr::ToRoot`; conversely, the `Ptr`
//   corresponding to a `Root` retrieved through `Root::Ptr`.
//
// * `WeakPtr<>: Similar to `Ptr`, but won't keep objects alive (i.e., if all
//   references to an object are of this type, allows the object to be deleted).
//   A `WeakPtr` corresponding to a given Ptr through `Ptr::ToWeakPtr`. To read
//   the value, the customer must call `WeakPtr::Lock` to attempt to obtain a
//   `Root` for the object.
//
// The expected usage is that values in the stack should be stored as `Root`
// pointers. When a reference to an object is stored inside another object, the
// reference should be stored as a `Ptr` or `WeakPtr`.
//
// There are two requirements on a type T for managed objects:
//
// 1. An "expand callback" function must be defined, which receives a T instance
//    t and returns a vector with an ObjectMetadata instance for each object
//    referenced by t. For example:
//
//    class T {
//     public:
//      std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand() const {
//        std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> output;
//        for (auto& foo : dependencies_)
//          output.push_back(foo.object_metadata());
//        return output;
//      }
//
//     private:
//      // The dependencies of this instance. This example assumes that `Foo`
//      // instances may themselves contain references back to this T.
//      std::vector<Ptr<Foo>> dependencies_;
//    };
//
//    std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand(const T& t) {
//      return t.Expand();
//    }
//
// 2. When a new reference is added, we must call `Protect` in it. This is
//    needed in order to support incremental collection (i.e., allow a
//    long-running collection to be interrupted and resumed). In the example
//    above, we'd do this whenever we insert items into `dependencies_`. See
//    the notes on `Ptr::Protect`.
#ifndef __AFC_LANGUAGE_GC_H__
#define __AFC_LANGUAGE_GC_H__
#include <glog/logging.h>

#include <functional>
#include <list>
#include <memory>
#include <unordered_set>
#include <variant>
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
// `Expand` functions for the types of the managed objects.
//
// This is agnostic to the type of the contained value. In fact, it can't even
// retrieve the contained value.
//
// The only `std::shared_ptr<>` reference (in the entire hierarchy of classes in
// this module, `afc::language::gc`) to the objects managed is kept inside
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
  enum State { kExpanded, kScheduled, kLost };
  struct Data {
    ExpandCallback expand_callback;
    State state = State::kLost;
  };
  concurrent::Protected<Data> data_;
};

// All the objects managed must be allocated within a given pool. Objects in a
// given pool may only reference objects in the same pool.
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

  size_t count_objects() const;

  struct FullCollectStats {
    size_t roots = 0;
    size_t begin_total = 0;
    size_t eden_size = 0;
    size_t end_total = 0;
    size_t generations = 0;
  };
  struct LightCollectStats {
    size_t begin_eden_size = 0;
    size_t end_eden_size = 0;
  };
  struct UnfinishedCollectStats {};
  using CollectOutput =
      std::variant<FullCollectStats, LightCollectStats, UnfinishedCollectStats>;
  CollectOutput Collect();
  FullCollectStats FullCollect();

  using RootRegistration = std::shared_ptr<bool>;

 private:
  template <typename T>
  friend class Root;

  template <typename T>
  friend class Ptr;

  CollectOutput Collect(bool full);

  void MaybeScheduleExpandInEden(
      language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata);

  language::NonNull<std::shared_ptr<ObjectMetadata>> NewObjectMetadata(
      ObjectMetadata::ExpandCallback expand_callback);

  RootRegistration AddRoot(std::weak_ptr<ObjectMetadata> object_metadata);

  using ObjectMetadataList = std::list<std::weak_ptr<ObjectMetadata>>;
  using ObjectExpandList =
      std::list<language::NonNull<std::shared_ptr<ObjectMetadata>>>;

  // The eden area holds information about recent activity. This is optimized to
  // be locked only briefly, so as to avoid blocking progress.
  struct Eden {
    static Eden NewWithExpandList();

    ObjectMetadataList object_metadata = {};

    // This is a unique_ptr to allow us to move it into Survivors preserving all
    // iterators.
    language::NonNull<std::unique_ptr<ObjectMetadataList>> roots = {};

    // A set of roots that have been deleted recently. This will allow us to
    // update Survivors::roots (in UpdateRoots).
    struct RootDeleted {
      ObjectMetadataList& roots_list;
      ObjectMetadataList::iterator it;
    };
    std::vector<RootDeleted> roots_deleted = {};

    // Normally is absent. If a `Collect` operation is interrupted, set to a
    // list to which `MaybeScheduleExpandInEden` will add objects, so that when
    // the collection is resumed, those expansions happen. See the notes on
    // `ObjectMetadata::Protect`.
    std::optional<ObjectExpandList> expand_list = std::nullopt;
  };

  // Survivors holds all the information of objects that have survived a
  // collection. This should only be locked by `Collect` (and may be held for
  // a long interval, as collection progresses). Should never be locked while
  // holding eden locked.
  struct Survivors {
    ObjectMetadataList object_metadata;
    std::list<language::NonNull<std::unique_ptr<ObjectMetadataList>>> roots;

    // After inserting from the eden and updating roots, we copy objects from
    // `roots` into `expand_list` (in `ScheduleExpandRoots`). We then
    // recursively visit all objects here. Once the list is empty, we'll know
    // a set of unreachable objects.
    //
    // If we reach a deadline while traversing this list, we stop. The next
    // execution will avoid (most of) the work we've done (because it will
    // quickly skip already scheduled or expanded objects).
    ObjectExpandList expand_list;
  };

  // Insert new roots from Eden::roots; removes expired roots from
  // Eden::roots_deleted; and inserts from Eden::object_metadata into
  // Survivors::object_metadata.
  static void ConsumeEden(Eden eden, Survivors& survivors);
  static bool IsEmpty(const Eden& eden);

  // Inserts all not-yet-reached objects from survivors.roots into
  // survivors.expand_list.
  static void ScheduleExpandRoots(Survivors& survivors);

  // If the object's state is kLost, appends it to `expand_list`. The caller
  // must ensure that `expand_list` will eventually be expanded (before allowing
  // unexpanded objects to be deleted).
  static void MaybeScheduleExpand(
      ObjectExpandList& expand_list,
      language::NonNull<std::shared_ptr<ObjectMetadata>> object);

  // Recursively expand all objects in `survivors.expand_list`. May stop early
  // if the timeout is reached.
  static void Expand(Survivors& survivors, std::optional<double> timeout);
  static void UpdateSurvivorsList(
      Survivors& survivors,
      std::vector<ObjectMetadata::ExpandCallback>& expired_objects_callbacks);

  concurrent::Protected<Eden> eden_;

  concurrent::Protected<Survivors> survivors_;
};

std::ostream& operator<<(std::ostream& os, const Pool::FullCollectStats& stats);
std::ostream& operator<<(std::ostream& os,
                         const Pool::LightCollectStats& stats);

// A mutable pointer with shared ownership of a managed object. Behaves very
// much like `std::shared_ptr<T>`: when the number of references drops to 0, the
// object is reclaimed. The object will also be reclaimed by Pool::Collect when
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

  // This is only exposed in order to allow implementation of `ExpandCallback`
  // functions.
  language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata() const {
    return object_metadata_;
  }

  // When an object C0 (that implements an `Expand` function) receives and
  // stores a Ptr<> P instance, it must call `Protect` on it. This will ensure
  // that if a collection is ongoing, P is "protected": gets scheduled to be
  // expanded.
  //
  // If we didn't do this, there could be races where the ownership of P could
  // be transfered from a yet-to-be-expanded container C1 to an already-expanded
  // container C0, which would allow P to be incorrectly dropped (when C1 gets
  // expanded, P is no longer reached).
  void Protect() const {
    CHECK(value_.lock() != nullptr);
    return pool().MaybeScheduleExpandInEden(object_metadata_);
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
