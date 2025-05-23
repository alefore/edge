// Module implementing concurrent incremental garbage collection.
//
// The main motivation is to support cyclic directed graphs of managed objects.
// By (1) explicitly tracking "roots" (entry points that should not be
// collected), and (2) requiring each managed type to expose a function listing
// all outgoing edges from a given object, this module can detect unreachable
// objects (including cycles) and collect them.
//
// Objects are deleted as soon as they have no incoming references (like regular
// `std::shared_ptr`) or, if they still have incoming references but aren't
// transitively reachable from a root, during garbage collection.
//
// The following public types are defined:
//
// * `Pool`: A managed graph. Destruction of unreachable cycles can be triggered
//   through `Pool::Collect` or `Pool::FullCollect`. References inside objects
//   can't cross pool boundaries: they should only refer to objects in the same
//   pool. We expect a single pool to be enough for most programs.
//
// * `Ptr<>`: A pointer to a managed object. The value can be obtained through
//   `Ptr::value` or the `->` operator. This is similar to `std::shared_ptr`.
//
// * `Root<>`: Similar to `Ptr`, but objects pointed to by roots (and their
//   transitive set of reachable objects) are always kept alive, even if they
//   aren't otherwise reachable. Roots are usually created by handling
//   management to the pool of an existing object through `Pool::NewRoot`. A
//   `Root` for a `Ptr` can be created through `Ptr::ToRoot`; conversely, the
//   `Ptr` corresponding to a `Root` can be read through `Root::Ptr`.
//
// * `WeakPtr<>: Similar to `Ptr`, but won't keep objects alive (i.e., if all
//   references to an object are of this type, allows the object to be deleted).
//   A `WeakPtr` corresponding to a given Ptr can be obtained through
//   `Ptr::ToWeakPtr`. To read the value, the customer must call `WeakPtr::Lock`
//   to attempt to obtain a `Root` for the object. This is similar to
//   `std::weak_ptr`.
//
// The expected usage is that values in the stack and in a few special entry
// points should be stored as `Root` pointers. Reference inside managed types
// should be stored as a `Ptr` or `WeakPtr`.
//
// The collection is incremental: `Pool::Collect` will return
// `UnfinishedCollectStats` occasionally. When this happens, the caller should
// try to call `Pool::Collect` again to resume the operation.
//
// DEFINING MANAGED TYPES
//
// An "expand callback" function must be defined for every type `T` for which
// objects will be managed. This function should receive a `T` instance `t` and
// return a vector with an `ObjectMetadata` instance for each managed object
// directly reachable from `t`. For example (here `Foo` would be another managed
// type):
//
//    class T {
//     public:
//      std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> MyExpand() const {
//        std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> output;
//        for (auto& foo : dependencies_)
//          output.push_back(foo.object_metadata());
//        return output;
//      }
//
//     private:
//      // The dependencies of this instance. This example assumes that `Foo`
//      // instances may themselves contain references back to this `T`.
//      std::vector<Ptr<Foo>> dependencies_;
//    };
//
//    std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand(const T& t) {
//      return t.MyExpandExpand();
//    }
//
// CREATING MANAGED INSTANCES
//
// To create new managed instances, use `Pool::NewRoot`. It is slightly more
// accurate to say that the life management of existing objects is *transferred*
// to the pool. For example:
//
//   Root<Foo> my_foo = pool.NewRoot(language::MakeNonNullUnique<Foo>(...));
//
// We would then typically either:
//
// 1. Store a corresponding `Ptr` in a managed container object. For example:
//
//    t.AddFoo(my_foo.ptr());
//
// 2. Retain the root in the stack or in the heap inside a non-managed object.
#ifndef __AFC_LANGUAGE_GC_H__
#define __AFC_LANGUAGE_GC_H__
#include <glog/logging.h>

#include <functional>
#include <list>
#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

#include "src/concurrent/bag.h"
#include "src/concurrent/operation.h"
#include "src/concurrent/protected.h"
#include "src/infrastructure/time.h"
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

  ~ObjectMetadata();

  Pool& pool() const;

  bool IsAlive() const;

 private:
  // Adds `shared_this` to `bag` and updates `container_bag_` and
  // `container_bag_registration_`.
  //
  // Customers (Pool) must synchronize access to this (which typically happens
  // by virtue of having synchronized the class that holds `bag`).
  //
  // An object can be added at most to one bag (but see `Orhpan`).
  static void AddToBag(NonNull<std::shared_ptr<ObjectMetadata>> shared_this,
                       concurrent::Bag<std::weak_ptr<ObjectMetadata>>& bag);

  // Removes the object from the bag it was added to with `AddToBag`.
  void Orphan();

  Pool& pool_;

  // The state of this object during a garbage collection operation.
  enum ExpandState {
    // This is the initial state that all objects have before the garbage
    // operation starts. At the end, when the collection finishes, all objects
    // remaining in this state will be deleted (and the state of all surviving
    // objects will be switched to this).
    kUnreached,

    // The object has been reached and has been scheduled for expansion.
    kScheduled,

    // The object has been reached and expanded: all its outgoing references
    // have been scheduled.
    kDone
  };
  struct Data {
    ExpandCallback expand_callback;
    ExpandState expand_state = ExpandState::kUnreached;
  };

  // `container_bag_registration_` is used to eagerly remove this object from
  // the corresponding bag during its deletion. The corresponding bag must only
  // be deleted after all objects stored in it have been deleted.
  std::optional<concurrent::Bag<std::weak_ptr<ObjectMetadata>>::Registration>
      container_bag_registration_;

  concurrent::Protected<Data> data_;
};

template <typename T, typename Enable = void>
struct ExpandHelper {};

// All the objects managed must be allocated within a given pool. Objects in a
// given pool may only reference objects in the same pool.
class Pool {
 public:
  struct Options {
    // If a call to `Collect` runs longer than this duration, we abort it (and
    // return `UnfinishedCollectStats`). A subsequent call to `Collect` will
    // resume it.
    //
    // When this happens, `Collect` is likely to actually run for slightly
    // longer than this threshold. This may be because some operations, such as
    // generating the set of references from an object, may take significantly
    // longer, or because we deliberately increase the threshold as collections
    // keep getting interrupted (in order to ensure that we make progress).
    std::optional<afc::infrastructure::Duration> collect_duration_threshold;

    std::shared_ptr<concurrent::OperationFactory> operation_factory;

    size_t max_bag_shards = 64;
  };

  Pool(Options options);

  template <typename T>
  Root<T> NewRoot(language::NonNull<std::unique_ptr<T>> value_unique) {
    language::NonNull<std::shared_ptr<T>> value = std::move(value_unique);
    return Ptr<T>(std::weak_ptr<T>(value.get_shared()),
                  NewObjectMetadata(
                      [value] { return ExpandHelper<T>()(value.value()); }))
        .ToRoot();
  }

  ~Pool();

  // This is mostly useful for testing. Collection schedules asynchronous work
  // in order to return control as early as possible. This blocks the calling
  // thread until that work is done.
  void BlockUntilDone() const;

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

  void AddToEdenExpandList(
      language::NonNull<std::shared_ptr<ObjectMetadata>> object_metadata);

  language::NonNull<std::shared_ptr<ObjectMetadata>> NewObjectMetadata(
      ObjectMetadata::ExpandCallback expand_callback);

  RootRegistration AddRoot(std::weak_ptr<ObjectMetadata> object_metadata);

  using ObjectMetadataBag = concurrent::Bag<std::weak_ptr<ObjectMetadata>>;
  using ObjectExpandVector =
      std::vector<language::NonNull<std::shared_ptr<ObjectMetadata>>>;

  // The eden area holds information about recent activity. This is optimized to
  // be locked only very briefly, to avoid blocking progress.
  struct Eden {
    Eden(size_t bag_shards, size_t consecutive_unfinished_collect_calls);

    bool IsEmpty() const;

    ObjectMetadataBag object_metadata;

    ObjectMetadataBag roots;

    // Normally is absent. If a `Collect` operation is interrupted, set to a
    // list to which `AddToEdenExpandList` will add objects (so that when the
    // collection is resumed, those expansions happen). See `Ptr::Protect`.
    std::optional<ObjectExpandVector> expansion_schedule = std::nullopt;

    // Incremented each time a call to `Collect` stops without finishing, and
    // reset as soon as the call finishes. Used to adjust the execution
    // threshold dynamically.
    size_t consecutive_unfinished_collect_calls;
  };

  // Data holds all the information of objects that have survived a collection.
  // This should only be locked by `Collect` (and may be held for a long
  // interval, as collection progresses). Should never be locked while holding
  // eden locked.
  struct Data {
    std::list<ObjectMetadataBag> object_metadata_list = {};

    std::list<ObjectMetadataBag> roots_list = {};

    // After inserting from the eden and updating roots, we copy objects from
    // `roots` into `expansion_schedule` (in `ScheduleExpandRoots`). We then
    // recursively visit all objects here. Once the list is empty, we'll know
    // a set of unreachable objects.
    //
    // If we reach a deadline while traversing this list, we stop. The next
    // execution will avoid (most of) the work we've done: we remove objects
    // from here (marking them as already expanded).
    concurrent::Bag<ObjectExpandVector> expansion_schedule;
  };

  // Moves objects from `eden` into `data`.
  //
  // Specifically, objects from fields `Eden::roots`, `Eden::object_metadata`
  // and `Eden::expansion_schedule` are moved into the corresponding fields in
  // `data`.
  void ConsumeEden(Eden eden, Data& data);

  // Inserts all not-yet-scheduled objects from `roots_list` into
  // `expansion_schedule`.
  static void ScheduleExpandRoots(
      const concurrent::Operation& operation,
      const std::list<ObjectMetadataBag>& roots_list,
      concurrent::Bag<ObjectExpandVector>& schedule);

  static bool IsExpandAlreadyScheduled(
      const language::NonNull<std::shared_ptr<ObjectMetadata>>& object);

  // Recursively expand all objects in `data.expansion_schedule`. May stop early
  // if the timeout is reached.
  static void Expand(const concurrent::Operation& operation,
                     concurrent::Bag<ObjectExpandVector>& schedule,
                     const std::optional<afc::infrastructure::CountDownTimer>&
                         count_down_timer);
  void RemoveUnreachable(
      const concurrent::Operation& operation,
      std::list<ObjectMetadataBag>& object_metadata_list,
      concurrent::Bag<std::vector<ObjectMetadata::ExpandCallback>>&
          expired_objects_callbacks);

  const Options options_;
  concurrent::Protected<Eden> eden_;
  concurrent::Protected<Data> data_;

  // If VLOG(10) is on, we'll store back traces of creation of roots here. When
  // the pool is deleted, if roots are leaked, we'll show where they were
  // created.
  using Backtrace = std::unique_ptr<char*, decltype(std::free)*>;
  std::optional<concurrent::Bag<Backtrace>> root_backtrace_;

  // When then pool is deleted, we need to ensure that any pending background
  // work is done before we allow internal classes to be deleted.
  const language::NonNull<std::unique_ptr<concurrent::Operation>> async_work_;
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
      : value_(other.value_), object_metadata_(other.object_metadata_) {
    Protect();
  }

  template <typename U>
  Ptr<T>& operator=(const Ptr<U>& other) {
    value_ = other.value_;
    object_metadata_ = other.object_metadata_;
    Protect();
    return *this;
  }

  template <typename U>
  Ptr<T>& operator=(Ptr<U>&& other) {
    value_ = std::move(other.value_);
    object_metadata_ = std::move(other.object_metadata_);
    Protect();
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

 private:
  // When we create a new Ptr<> P instance or assign it into an existing
  // instance, we call `Protect` in it automatically. This ensures that if a
  // collection is ongoing, the instance is "protected": it gets scheduled to be
  // expanded.
  //
  // If we didn't do this, there could be races where the ownership of P could
  // be transfered from a yet-to-be-expanded container (or root) C to an
  // already-expanded container (or root), which would allow P to be incorrectly
  // dropped (when C gets expanded, P is no longer reached).
  void Protect() const {
    CHECK(value_.lock() != nullptr);
    return pool().AddToEdenExpandList(object_metadata_);
  }

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
    VLOG(10) << "Ptr(pool, value): " << object_metadata_.get_shared()
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
  using value_type = T;

  Root(const Root<T>& other) = default;

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

  Root<T>& operator=(const Root<T>& other) = delete;
  Pool& pool() const { return ptr_.pool(); }

  T* operator->() const { return ptr_.operator->(); }

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

// Convenience declarations for Expand methods /////////////////////////////////

template <typename T>
using ExpandMethodType = decltype(std::declval<const T&>().Expand());

template <typename T, typename = void>
struct HasExpandMethod : std::false_type {};

template <typename T>
struct HasExpandMethod<
    T, std::enable_if_t<std::is_same_v<
           decltype(std::declval<const T&>().Expand()),
           std::vector<NonNull<std::shared_ptr<ObjectMetadata>>>>>>
    : std::true_type {};

template <typename T>
struct ExpandHelper<T, std::enable_if_t<HasExpandMethod<T>::value>> {
  std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> operator()(
      const T& object) {
    return object.Expand();
  }
};

};  // namespace afc::language::gc
#endif
