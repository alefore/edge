#include "src/language/lazy_string/lazy_string.h"

#include <glog/logging.h>

#include "src/infrastructure/tracker.h"
#include "src/language/const_tree.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"

namespace afc::language::lazy_string {
namespace {
using infrastructure::Tracker;
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

std::wstring LazyString::ToString() const {
  static Tracker tracker(L"LazyString::ToString");
  auto call = tracker.Call();
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
  if (IsEmpty()) return suffix;
  if (suffix.IsEmpty()) return *this;
  return LazyString(MakeNonNullShared<AppendImpl>(AppendImpl::Tree::Append(
      AppendImpl::TreeFrom(*this), AppendImpl::TreeFrom(suffix))));
}

bool LazyString::operator<(const LazyString& x) const {
  for (ColumnNumber current; current.ToDelta() < size(); ++current) {
    if (current.ToDelta() == x.size()) return false;
    if (get(current) < x.get(current)) return true;
    if (get(current) > x.get(current)) return false;
  }
  return size() < x.size();
}

bool operator==(const LazyString& a, const LazyString& b) {
  return a.size() == b.size() &&
         !FindFirstColumnWithPredicate(a, [&](ColumnNumber column, wchar_t c) {
            return b.get(column) != c;
          }).has_value();
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

std::wstring to_wstring(const LazyString& s) { return s.ToString(); }

std::string LazyString::ToBytes() const { return ToByteString(ToString()); }

}  // namespace afc::language::lazy_string
