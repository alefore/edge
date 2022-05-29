#include "src/vm/internal/compilation.h"

#include "src/vm/public/vm.h"

namespace afc::vm {
using language::Error;
namespace gc = language::gc;
Compilation::Compilation(gc::Pool& pool, gc::Root<Environment> environment)
    : pool(pool), environment(std::move(environment)) {}

void Compilation::AddError(Error error) {
  // TODO: Enable this logging statement.
  // LOG(INFO) << "Compilation error: " << error;
  std::wstring prefix;
  for (auto it = source_.begin(); it != source_.end(); ++it) {
    Source& source = *it;
    std::wstring location =
        (source.path.has_value() ? (source.path->read() + L":") : L"") +
        std::to_wstring(source.line + 1) + L":" +
        std::to_wstring(source.column + 1);
    if (std::next(it) == source_.end())
      prefix += location + L": ";
    else
      prefix += L"Include from " + location + L": ";
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
  source_.back().line++;
}

void Compilation::SetSourceColumnInLine(size_t column) {
  CHECK(!source_.empty());
  source_.back().column = column;
}

std::optional<infrastructure::Path> Compilation::current_source_path() const {
  CHECK(!source_.empty());
  return source_.back().path;
}

}  // namespace afc::vm
