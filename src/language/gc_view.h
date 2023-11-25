#ifndef __AFC_LANGUAGE_GC_VIEW_H__
#define __AFC_LANGUAGE_GC_VIEW_H__

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::language::gc {
class GetRootValue {
 public:
  template <typename Iterator>
  static auto& Adjust(const Iterator& iterator) {
    return (*iterator).ptr().value();
  }
};

class GetObjectMetadata {
 public:
  template <typename Iterator>
  static auto& Adjust(const Iterator& iterator) {
    return (*iterator).object_metadata();
  }
};

template <typename Adapter, typename Iterator>
class RootValueIterator {
  Iterator iterator_;

 public:
  explicit RootValueIterator(Iterator iterator) : iterator_(iterator) {}
  explicit RootValueIterator() {}

  using iterator_category =
      typename std::iterator_traits<Iterator>::iterator_category;
  using difference_type =
      typename std::iterator_traits<Iterator>::difference_type;
  using value_type =
      typename std::iterator_traits<Iterator>::value_type::value_type;
  using reference = value_type&;

  reference operator*() const { return Adapter::Adjust(iterator_); }

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
};

namespace view {
template <typename Adapter>
class RootValueViewAdapter;

template <typename Adapter, typename Range>
class RootValueRange
    : public std::ranges::view_interface<RootValueRange<Adapter, Range>> {
  // Not `const` to enable move construction.
  Range range_;

  friend class RootValueViewAdapter<Adapter>;

  template <typename R>
  explicit RootValueRange(R&& range) : range_(std::forward<R>(range)) {}

 public:
  auto begin() {
    return RootValueIterator<Adapter,
                             std::decay_t<decltype(std::begin(range_))>>(
        std::begin(range_));
  }
  auto begin() const {
    return RootValueIterator<Adapter,
                             std::decay_t<decltype(std::begin(range_))>>(
        std::begin(range_));
  }

  auto end() {
    return RootValueIterator<Adapter, std::decay_t<decltype(std::end(range_))>>(
        std::end(range_));
  }
  auto end() const {
    return RootValueIterator<Adapter, std::decay_t<decltype(std::end(range_))>>(
        std::end(range_));
  }

  size_t size() const { return range_.size(); }
};

template <typename Adapter>
class RootValueViewAdapter {
 public:
  // Call operator to take a range and return a RootValueRange
  template <typename Range>
  auto operator()(Range&& range) const {
    return RootValueRange<GetRootValue, Range>(std::forward<Range>(range));
  }
};

// Global instance of the adapters:
inline constexpr RootValueViewAdapter<GetRootValue> Value{};
inline constexpr RootValueViewAdapter<GetObjectMetadata> ObjectMetadata{};

// Overload the pipe operator for range and RootValueViewAdapter
template <typename Adapter, typename Range>
auto operator|(Range&& r, RootValueViewAdapter<Adapter> const& adapter) {
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
