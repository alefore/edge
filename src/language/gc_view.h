#ifndef __AFC_LANGUAGE_GC_VIEW_H__
#define __AFC_LANGUAGE_GC_VIEW_H__

#include <memory>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::language::gc {
// Concept to check if an iterator type supports operator-
template <typename Iterator>
concept Subtractable = requires(Iterator a, Iterator b) {
  {
    a - b
  } -> std::convertible_to<
      typename std::iterator_traits<Iterator>::difference_type>;
};

// Concept to check if an iterator type supports operator+
template <typename Iterator>
concept Addable = requires(
    Iterator a, typename std::iterator_traits<Iterator>::difference_type n) {
  { a + n } -> std::convertible_to<Iterator>;
};

class GetPtrRoot {
 public:
  template <typename Iterator>
  static auto Adjust(const Iterator& iterator) {
    return (*iterator).ToRoot();
  }
};

class GetRootValue {
 public:
  template <typename Iterator>
  static auto& Adjust(const Iterator& iterator) {
    return (*iterator).ptr().value();
  }
};

class GetPtr {
 public:
  template <typename Iterator>
  static auto& Adjust(const Iterator& iterator) {
    return (*iterator).ptr();
  }
};

class GetPtrValue {
 public:
  template <typename Iterator>
  static auto& Adjust(const Iterator& iterator) {
    return (*iterator).value();
  }
};

class GetObjectMetadata {
 public:
  template <typename Iterator>
  static language::NonNull<std::shared_ptr<ObjectMetadata>> Adjust(
      const Iterator& iterator) {
    return (*iterator).object_metadata();
  }
};

class LockWeakPtr {
 public:
  template <typename Iterator>
  static auto Adjust(const Iterator& iterator) {
    return (*iterator).Lock();
  }
};

template <typename Adapter, typename Iterator>
class RootValueIterator {
  Iterator iterator_;

 public:
  explicit RootValueIterator(Adapter, Iterator iterator)
      : iterator_(iterator) {}
  explicit RootValueIterator() {}

  using iterator_category =
      typename std::iterator_traits<Iterator>::iterator_category;
  using difference_type =
      typename std::iterator_traits<Iterator>::difference_type;
  using value_type =
      std::decay_t<decltype(Adapter::Adjust(std::declval<const Iterator&>()))>;
  using reference = value_type&;

  decltype(Adapter::Adjust(std::declval<const Iterator&>())) operator*() const {
    return Adapter::Adjust(iterator_);
  }

  bool operator!=(const RootValueIterator& other) const {
    return iterator_ != other.iterator_;
  }

  bool operator==(const RootValueIterator& other) const {
    return iterator_ == other.iterator_;
  }

  RootValueIterator& operator++() {  // Prefix increment.
    ++iterator_;
    return *this;
  }

  RootValueIterator operator++(int) {  // Postfix increment.
    RootValueIterator tmp = *this;
    ++*this;
    return tmp;
  }

  // Conditional operator- definition.
  // Define this only if the underlying iterator supports it
  template <typename It = Iterator>
    requires Subtractable<It>
  difference_type operator-(const RootValueIterator& other) const {
    return iterator_ - other.iterator_;
  }

  // Conditional operator+ definition.
  // Define this only if the underlying iterator supports it
  template <typename It = Iterator>
    requires Addable<It>
  friend It operator+(const RootValueIterator& iter,
                      typename std::iterator_traits<It>::difference_type n) {
    return iter.iterator_ + n;
  }

  // Conditional operator+ definition.
  // Define this only if the underlying iterator supports it
  template <typename It = Iterator>
    requires Addable<It>
  friend It operator+(typename std::iterator_traits<It>::difference_type n,
                      const RootValueIterator& iter) {
    return n + iter.iterator_;
  }
};

namespace view {
template <typename Adapter>
class RootValueViewAdapter;
template <typename Adapter>
class RootValueFilterViewAdapter;

template <typename Adapter, typename Range>
class RootValueRange
    : public std::ranges::view_interface<RootValueRange<Adapter, Range>> {
  // Not `const` to enable move construction.
  Range range_;

  friend class RootValueViewAdapter<Adapter>;
  friend class RootValueFilterViewAdapter<Adapter>;

  template <typename R>
  explicit RootValueRange(R&& range) : range_(std::forward<R>(range)) {}

 public:
  using value_type =
      RootValueIterator<Adapter, decltype(std::begin(
                                     std::declval<Range>()))>::value_type;

  auto begin() { return RootValueIterator(Adapter{}, std::begin(range_)); }
  auto begin() const {
    return RootValueIterator(Adapter{}, std::begin(range_));
  }

  auto end() { return RootValueIterator(Adapter{}, std::end(range_)); }
  auto end() const { return RootValueIterator(Adapter{}, std::end(range_)); }

  size_t size() const { return range_.size(); }
};

template <typename Adapter>
class RootValueViewAdapter {
 public:
  // Call operator to take a range and return a RootValueRange
  template <typename Range>
  auto operator()(Range&& range) const {
    return RootValueRange<Adapter, Range>(std::forward<Range>(range));
  }
};

template <typename Adapter>
class RootValueFilterViewAdapter {
 public:
  // Call operator to take a range and return a RootValueRange
  template <typename Range>
  auto operator()(Range&& range) const {
    return RootValueRange<Adapter, std::remove_cvref_t<Range>>(
               std::forward<Range>(range)) |
           std::views::filter(
               [](const auto& root) { return root.has_value(); }) |
           std::views::transform([](const auto& root) { return root.value(); });
  }
};

struct LockAdaptorClosure
    : std::ranges::range_adaptor_closure<LockAdaptorClosure> {
  template <std::ranges::viewable_range R>
  constexpr auto operator()(R&& r) const {
    return gc::view::RootValueFilterViewAdapter<gc::LockWeakPtr>{}(
        std::forward<R>(r));
  }
};

// Global instance of the adapters:
inline constexpr RootValueViewAdapter<GetRootValue> Value{};
inline constexpr RootValueViewAdapter<GetPtrRoot> Root{};
inline constexpr RootValueViewAdapter<GetObjectMetadata> ObjectMetadata{};
inline constexpr RootValueViewAdapter<GetPtr> Ptr{};
inline constexpr RootValueViewAdapter<GetPtrValue> PtrValue{};

inline constexpr LockAdaptorClosure Lock{};

// Overload the pipe operator for range and RootValueViewAdapter
template <typename Adapter, typename Range>
auto operator|(Range&& r, const RootValueViewAdapter<Adapter>& adapter) {
  return adapter(std::forward<Range>(r));
}

template <typename Range>
auto operator|(Range&& r, const LockAdaptorClosure& adapter) {
  return adapter(std::forward<Range>(r));
}
}  // namespace view
}  // namespace afc::language::gc
namespace std::ranges {
template <typename Adapter, typename Range>
inline constexpr bool enable_borrowed_range<
    afc::language::gc::view::RootValueRange<Adapter, Range>> = true;
}

#endif  // __AFC_LANGUAGE_GC_VIEW_H__
