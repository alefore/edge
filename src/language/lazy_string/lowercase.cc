#include "src/language/lazy_string/lowercase.h"

#include <glog/logging.h>

#include <memory>

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace afc::language::lazy_string {
namespace {
class LowerCaseImpl : public LazyStringImpl {
 public:
  LowerCaseImpl(LazyString input) : input_(std::move(input)) {}

  wchar_t get(ColumnNumber pos) const override {
    return towlower(input_.get(pos));
  }

  ColumnNumberDelta size() const override { return input_.size(); }

 private:
  const LazyString input_;
};

const bool lower_case_tests_registration = tests::Register(
    L"LowerCaseTests", {{.name = L"EmptyString",
                         .callback =
                             [] {
                               CHECK_EQ(LowerCaseImpl(LazyString()).size(),
                                        ColumnNumberDelta());
                             }},
                        {.name = L"SimpleString", .callback = [] {
                           // TODO: Why can't we use CHECK_EQ? Why can't the
                           // compiler find the operator<<?
                           CHECK(LowerCase(LazyString{L"Alejandro Forero"}) ==
                                 LazyString(L"alejandro forero"));
                         }}});
}  // namespace

LazyString LowerCase(LazyString input) {
  return LazyString(MakeNonNullShared<LowerCaseImpl>(std::move(input)));
}

}  // namespace afc::language::lazy_string
