#include "src/vm/compilation.h"

#include "src/language/lazy_string/lazy_string.h"

using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;

namespace afc::vm {
using language::Error;
namespace gc = language::gc;
Compilation::Compilation(gc::Pool& input_pool,
                         gc::Root<Environment> input_environment)
    : pool(input_pool), environment(std::move(input_environment)) {}

void Compilation::PushStackFrameHeader(StackFrameHeader header) {
  stack_headers_.push_back(std::move(header));
}

void Compilation::PopStackFrameHeader() {
  CHECK(!stack_headers_.empty());
  stack_headers_.pop_back();
}

std::optional<std::reference_wrapper<StackFrameHeader>>
Compilation::CurrentStackFrameHeader() {
  if (stack_headers_.empty()) return std::nullopt;
  return std::ref(stack_headers_.back());
}

void Compilation::AddError(Error error) {
  LOG(INFO) << "Compilation error: " << error;
  LazyString prefix;
  for (auto it = source_.begin(); it != source_.end(); ++it) {
    Source& source = *it;
    LazyString location =
        (source.path.has_value() ? (source.path->read() + LazyString{L":"})
                                 : LazyString{}) +
        LazyString{std::to_wstring(
            (source.line_column.line + LineNumberDelta(1)).read())} +
        LazyString{L":"} +
        LazyString{std::to_wstring(
            (source.line_column.column + ColumnNumberDelta(1)).read())};
    if (std::next(it) != source_.end()) prefix += LazyString{L"Include from "};
    prefix += location + LazyString{L": "};
  }

  errors_.push_back(AugmentError(prefix, std::move(error)));
}

const std::vector<Error>& Compilation::errors() const { return errors_; }
std::vector<Error>& Compilation::errors() { return errors_; }

void Compilation::PushSource(std::optional<infrastructure::Path> path) {
  source_.push_back(Source{.path = path});
}

void Compilation::PopSource() {
  CHECK(!source_.empty());
  source_.pop_back();
}

void Compilation::IncrementLine() {
  CHECK(!source_.empty());
  source_.back().line_column =
      LineColumn(LineNumber(source_.back().line_column.line.next()));
}

void Compilation::SetSourceColumnInLine(ColumnNumber column) {
  CHECK(!source_.empty());
  source_.back().line_column.column = column;
}

std::optional<infrastructure::Path> Compilation::current_source_path() const {
  CHECK(!source_.empty());
  return source_.back().path;
}

}  // namespace afc::vm
