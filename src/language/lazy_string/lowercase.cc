#include "src/language/lazy_string/lowercase.h"

#include <glog/logging.h>

#include <memory>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
namespace {
using CharTransformFn = wint_t (*)(wint_t);

template <CharTransformFn Transform>
class Impl : public LazyStringImpl {
  const LazyString input_;

 public:
  Impl(LazyString input) : input_(std::move(input)) {}

  wchar_t get(ColumnNumber pos) const override {
    return static_cast<wchar_t>(Transform(input_.get(pos)));
  }

  ColumnNumberDelta size() const override { return input_.size(); }

  bool Every(std::function<bool(wchar_t)> callback, ColumnNumber start,
             ColumnNumberDelta size) const override {
    return input_.Every(
        [callback](wchar_t c) {
          return callback(static_cast<wchar_t>(Transform(c)));
        },
        start, size);
  }
};

const bool lower_case_tests_registration = tests::Register(
    L"LowerCaseTests", {{.name = L"EmptyString",
                         .callback =
                             [] {
                               CHECK_EQ(LowerCase(LazyString{}).size(),
                                        ColumnNumberDelta());
                             }},
                        {.name = L"SimpleString", .callback = [] {
                           CHECK_EQ(LowerCase(LazyString{L"Alejandro Forero"}),
                                    LazyString(L"alejandro forero"));
                         }}});

const bool upper_case_tests_registration = tests::Register(
    L"UpperCaseTests", {{.name = L"EmptyString",
                         .callback =
                             [] {
                               CHECK_EQ(UpperCase(LazyString{}).size(),
                                        ColumnNumberDelta());
                             }},
                        {.name = L"SimpleString", .callback = [] {
                           CHECK_EQ(UpperCase(LazyString{L"Alejandro Forero"}),
                                    LazyString(L"ALEJANDRO FORERO"));
                         }}});
}  // namespace

LazyString LowerCase(LazyString input) {
  return LazyString(MakeNonNullShared<Impl<std::towlower>>(std::move(input)));
}

LazyString UpperCase(LazyString input) {
  return LazyString(MakeNonNullShared<Impl<std::towupper>>(std::move(input)));
}

SingleLine LowerCase(SingleLine input) {
  return SingleLine{SingleLineValidator::SkipValidationKey{},
                    LowerCase(input.read())};
}

struct Custom {
  struct SkipValidationKey {};
};

SingleLine UpperCase(SingleLine input) {
  return SingleLine{SingleLineValidator::SkipValidationKey{},
                    UpperCase(input.read())};
}

NonEmptySingleLine LowerCase(NonEmptySingleLine input) {
  return NonEmptySingleLine{LowerCase(input.read())};
}

NonEmptySingleLine UpperCase(NonEmptySingleLine input) {
  return NonEmptySingleLine{UpperCase(input.read())};
}
}  // namespace afc::language::lazy_string
