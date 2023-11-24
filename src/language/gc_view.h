#ifndef __AFC_LANGUAGE_GC_VIEW_H__
#define __AFC_LANGUAGE_GC_VIEW_H__

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::language::gc {
template <typename Range>
class RootValueRange;

template <typename Iterator>
class RootValueIterator {
  Iterator iterator_;

  template <typename Range>
  friend class RootValueRange;

  explicit RootValueIterator(Iterator iterator) : iterator_(iterator) {}

 public:
  using value_type =
      typename std::iterator_traits<Iterator>::value_type::value_type;
  using reference = value_type&;

  reference operator*() const { return iterator_->ptr().value(); }

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
    RootValueIterator tmp = this;
    ++*this;
    return tmp;
  }
};

template <typename Range>
class RootValueRange {
  Range& range_;

 public:
  explicit RootValueRange(Range& range) : range_(range) {}

  auto begin() { return RootValueIterator(std::begin(range_)); }

  auto end() { return RootValueIterator(std::end(range_)); }
};

template <typename Range>
RootValueRange<Range> RootValueView(Range& range) {
  return RootValueRange<Range>(range);
}
}  // namespace afc::language::gc
#endif  // __AFC_LANGUAGE_GC_VIEW_H__