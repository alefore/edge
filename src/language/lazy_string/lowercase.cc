#include "src/language/lazy_string/lowercase.h"

#include <glog/logging.h>

#include <memory>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
namespace {
class LowerCaseImpl : public LazyStringImpl {
  const LazyString input_;

 public:
  LowerCaseImpl(LazyString input) : input_(std::move(input)) {}

  wchar_t get(ColumnNumber pos) const override {
    return towlower(input_.get(pos));
  }

  ColumnNumberDelta size() const override { return input_.size(); }
};

class UpperCaseImpl : public LazyStringImpl {
  const LazyString input_;

 public:
  UpperCaseImpl(LazyString input) : input_(std::move(input)) {}

  wchar_t get(ColumnNumber pos) const override {
    return towupper(input_.get(pos));
  }

  ColumnNumberDelta size() const override { return input_.size(); }
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
  return LazyString(MakeNonNullShared<LowerCaseImpl>(std::move(input)));
}

LazyString UpperCase(LazyString input) {
  return LazyString(MakeNonNullShared<UpperCaseImpl>(std::move(input)));
}

}  // namespace afc::language::lazy_string
