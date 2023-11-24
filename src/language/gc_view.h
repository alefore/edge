#ifndef __AFC_LANGUAGE_GC_VIEW_H__
#define __AFC_LANGUAGE_GC_VIEW_H__

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::language::gc {
template <typename Iterator>
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

  reference operator*() const { return (*iterator_).ptr().value(); }

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

template <typename Range>
class RootValueRange {
  const Range range_;

 public:
  template <typename R>
  explicit RootValueRange(R&& range) : range_(std::forward<R>(range)) {}

  auto begin() { return RootValueIterator(std::begin(range_)); }
  auto begin() const { return RootValueIterator(std::begin(range_)); }

  auto end() { return RootValueIterator(std::end(range_)); }
  auto end() const { return RootValueIterator(std::end(range_)); }

  size_t size() { return range_.size(); }
};

template <typename Range>
RootValueRange<Range> RootValueView(Range&& range) {
  return RootValueRange<Range>(std::forward<Range>(range));
}
}  // namespace afc::language::gc
namespace std::ranges {
template <typename Range>
inline constexpr bool
    enable_borrowed_range<afc::language::gc::RootValueRange<Range>> = true;
}

#endif  // __AFC_LANGUAGE_GC_VIEW_H__