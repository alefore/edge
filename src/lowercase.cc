#include "src/lowercase.h"

#include <glog/logging.h>

#include <memory>

#include "src/char_buffer.h"
#include "src/language/wstring.h"
#include "src/line_column.h"
#include "src/tests/tests.h"

namespace afc::editor {

using language::NonNull;

namespace {
class LowerCaseImpl : public LazyString {
 public:
  LowerCaseImpl(NonNull<std::shared_ptr<LazyString>> input)
      : input_(std::move(input)) {}

  wchar_t get(ColumnNumber pos) const override {
    return towlower(input_->get(pos));
  }

  ColumnNumberDelta size() const override { return input_->size(); }

 private:
  const NonNull<std::shared_ptr<LazyString>> input_;
};

const bool lower_case_tests_registration = tests::Register(
    L"LowerCaseTests",
    {{.name = L"EmptyString",
      .callback =
          [] {
            CHECK_EQ(LowerCaseImpl(EmptyString()).size(), ColumnNumberDelta());
          }},
     {.name = L"SimpleString", .callback = [] {
        // TODO: Why can't we use CHECK_EQ? Why can't the compiler find
        // the operator<<?
        CHECK(LowerCaseImpl(NewLazyString(L"Alejandro Forero")).ToString() ==
              L"alejandro forero");
      }}});
}  // namespace

NonNull<std::shared_ptr<LazyString>> LowerCase(
    NonNull<std::shared_ptr<LazyString>> input) {
  return MakeNonNullShared<LowerCaseImpl>(std::move(input));
}

}  // namespace afc::editor
