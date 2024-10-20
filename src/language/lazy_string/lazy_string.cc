#include "src/language/lazy_string/lazy_string.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/const_tree.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
ColumnNumberDelta ColumnNumber::ToDelta() const {
  CHECK_LE(read(), std::numeric_limits<ColumnNumberDelta>::max());
  return ColumnNumberDelta{static_cast<int>(read())};
}

namespace {
using ::operator<<;

class EmptyStringImpl : public LazyStringImpl {
 public:
  wchar_t get(ColumnNumber) const override {
    LOG(FATAL) << "Attempt to read from empty string.";
    return 0;
  }
  ColumnNumberDelta size() const override { return ColumnNumberDelta(0); }
};

template <typename Container>
class StringFromContainer : public LazyStringImpl {
 public:
  StringFromContainer(Container data) : data_(std::move(data)) {}

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos, ColumnNumber(data_.size()));
    return data_.at(pos.read());
  }

  ColumnNumberDelta size() const { return ColumnNumberDelta(data_.size()); }

 protected:
  const Container data_;
};

class RepeatedChar : public LazyStringImpl {
 public:
  RepeatedChar(ColumnNumberDelta times, wchar_t c) : times_(times), c_(c) {
    CHECK_GE(times_, ColumnNumberDelta(0));
  }

  wchar_t get(ColumnNumber pos) const {
    CHECK_LT(pos.ToDelta(), times_);
    return c_;
  }

  ColumnNumberDelta size() const { return times_; }

 protected:
  const ColumnNumberDelta times_;
  const wchar_t c_;
};

class SubstringImpl : public LazyStringImpl {
 public:
  SubstringImpl(NonNull<std::shared_ptr<const LazyStringImpl>> buffer,
                ColumnNumber column, ColumnNumberDelta delta)
      : buffer_(std::move(buffer)), column_(column), delta_(delta) {}

  wchar_t get(ColumnNumber pos) const override {
    return buffer_->get(column_ + pos.ToDelta());
  }

  ColumnNumberDelta size() const override { return delta_; }

 private:
  const NonNull<std::shared_ptr<const LazyStringImpl>> buffer_;
  // First column to read from.
  const ColumnNumber column_;
  const ColumnNumberDelta delta_;
};
}  // namespace

// Why isn't this in the anonymous namespace? We need to make it public so that
// we can declare it as friend of LazyString (so that it can get access to the
// `data_` field.
class AppendImpl : public LazyStringImpl {
 public:
  using Tree = ConstTree<VectorBlock<wchar_t, 64>, 64>;

  AppendImpl(Tree::Ptr tree) : tree_(std::move(tree)) {}

  wchar_t get(ColumnNumber pos) const { return tree_->Get(pos.read()); }

  ColumnNumberDelta size() const {
    return ColumnNumberDelta(Tree::Size(tree_));
  }

  const Tree::Ptr& tree() const { return tree_; }

  static AppendImpl::Tree::Ptr TreeFrom(LazyString a) {
    if (auto a_cast = dynamic_cast<const AppendImpl*>(a.data_.get().get());
        a_cast != nullptr) {
      return a_cast->tree();
    }
    AppendImpl::Tree::Ptr output;
    ForEachColumn(a, [&output](ColumnNumber, wchar_t c) {
      output = AppendImpl::Tree::PushBack(output, c).get_shared();
    });
    return output;
  }

 private:
  const Tree::Ptr tree_;
};

LazyString::LazyString() : data_(NonNull<std::shared_ptr<EmptyStringImpl>>()) {}

LazyString::LazyString(std::wstring input)
    : data_(MakeNonNullShared<StringFromContainer<std::wstring>>(
          std::move(input))) {}

LazyString::LazyString(ColumnNumberDelta times, wchar_t c)
    : data_(MakeNonNullShared<RepeatedChar>(times, c)) {
  CHECK_EQ(size(), times);
}

wchar_t LazyString::get(ColumnNumber pos) const { return data_->get(pos); }

ColumnNumberDelta LazyString::size() const { return data_->size(); }

bool LazyString::empty() const { return data_->size().IsZero(); }

std::wstring LazyString::ToString() const {
  TRACK_OPERATION(LazyString_ToString);
  std::wstring output(size().read(), 0);
  ForEachColumn(*this,
                [&output](ColumnNumber i, wchar_t c) { output[i.read()] = c; });
  return output;
}

LazyString LazyString::Substring(ColumnNumber column) const {
  return Substring(column, size() - column.ToDelta());
}

LazyString LazyString::Substring(ColumnNumber column,
                                 ColumnNumberDelta delta) const {
  if (column.IsZero() && delta == ColumnNumberDelta(size()))
    return LazyString(data_);  // Optimization.
  CHECK_GE(delta, ColumnNumberDelta(0));
  CHECK_LE(column, ColumnNumber(0) + size());
  CHECK_LE(column + delta, ColumnNumber(0) + size());
  return LazyString(MakeNonNullShared<SubstringImpl>(data_, column, delta));
}

LazyString LazyString::SubstringWithRangeChecks(ColumnNumber column,
                                                ColumnNumberDelta delta) const {
  column = std::min(column, ColumnNumber(0) + size());
  return Substring(column, std::min(delta, size() - column.ToDelta()));
}

LazyString LazyString::Append(LazyString suffix) const {
  if (empty()) return suffix;
  if (suffix.empty()) return *this;
  return LazyString(MakeNonNullShared<AppendImpl>(AppendImpl::Tree::Append(
      AppendImpl::TreeFrom(*this), AppendImpl::TreeFrom(suffix))));
}

std::strong_ordering LazyString::operator<=>(const LazyString& x) const {
  for (ColumnNumber current; current.ToDelta() < size(); ++current) {
    if (current.ToDelta() == x.size()) return std::strong_ordering::greater;
    if (auto cmp = get(current) <=> x.get(current); cmp != 0) return cmp;
  }
  return size() <=> x.size();
}

bool LazyString::operator==(const LazyString& other) const {
  return (*this <=> other) == std::strong_ordering::equal;
}

const LazyString& operator+=(LazyString& a, const LazyString& b) {
  a = std::move(a).Append(b);
  return a;
}

LazyString operator+(const LazyString& a, const LazyString& b) {
  return a.Append(b);
}

std::ostream& operator<<(std::ostream& os,
                         const afc::language::lazy_string::LazyString& obj) {
  // TODO(P2): Find another way to implement this.
  os << obj.ToString();
  return os;
}

LazyStringIterator LazyString::begin() const {
  return LazyStringIterator(*this, ColumnNumber{});
}

LazyStringIterator LazyString::end() const {
  return LazyStringIterator(*this, ColumnNumber{} + size());
}

std::wstring to_wstring(const LazyString& s) { return s.ToString(); }

LazyString ToLazyString(LazyString x) { return x; }

std::string LazyString::ToBytes() const { return ToByteString(ToString()); }

bool LazyStringIterator::operator!=(const LazyStringIterator& other) const {
  return !(*this == other);
}

bool LazyStringIterator::operator==(const LazyStringIterator& other) const {
  if (container_.data_ != other.container_.data_) {
    CHECK(IsAtEnd() && other.IsAtEnd());
    return false;
  }
  if (IsAtEnd() || other.IsAtEnd()) return IsAtEnd() && other.IsAtEnd();
  return position_ == other.position_;
}

LazyStringIterator& LazyStringIterator::operator++() {  // Prefix increment.
  ++position_;
  return *this;
}

LazyStringIterator LazyStringIterator::operator++(int) {  // Postfix increment.
  LazyStringIterator tmp = *this;
  ++*this;
  return tmp;
}

int LazyStringIterator::operator-(const LazyStringIterator& other) const {
  if (container_.data_ != other.container_.data_) {
    CHECK(IsAtEnd() && other.IsAtEnd());
    return 0;
  }
  return (position_ - other.position_).read();
}

LazyStringIterator LazyStringIterator::operator+(int n) const {
  return LazyStringIterator(container_, position_ + ColumnNumberDelta(n));
}

LazyStringIterator LazyStringIterator::operator+(int n) {
  return LazyStringIterator(container_, position_ + ColumnNumberDelta(n));
}

bool LazyStringIterator::IsAtEnd() const {
  return position_.ToDelta() >= container_.size();
}

namespace {
const bool iterator_tests_registration = tests::Register(
    L"LazyStringIterator",
    {{.name = L"EndComparisonOk",
      .callback =
          [] { CHECK(LazyString{L""}.end() != LazyString{L""}.end()); }},
     {.name = L"EmptyBeginComparisonOk",
      .callback =
          [] { CHECK(LazyString{L""}.begin() != LazyString{L""}.begin()); }},
     {.name = L"ComparisonEqual",
      .callback =
          [] {
            LazyString input{L"alejandro"};
            CHECK(input.begin() == input.begin());
          }},
     {.name = L"ComparisonDifferent",
      .callback =
          [] {
            LazyString input{L"alejandro"};
            CHECK(++input.begin() != input.begin());
          }},
     {.name = L"ComparisonDifferentContainersCrashes",
      .callback =
          [] {
            tests::ForkAndWaitForFailure(
                [] { LazyString{L"a"}.begin() == LazyString{L"a"}.begin(); });
          }},
     {.name = L"EventuallyReachesEnd", .callback = [] {
        LazyString input{L"foo"};
        LazyStringIterator it = input.begin();
        ++it;
        ++it;
        ++it;
        CHECK(it == input.end());
      }}});

}  // namespace

}  // namespace afc::language::lazy_string
