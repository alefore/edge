#include "src/vm/internal/compilation.h"

#include "src/vm/public/vm.h"

namespace afc::vm {
namespace gc = language::gc;
Compilation::Compilation(gc::Pool& pool, gc::Root<Environment> environment)
    : pool(pool), environment(std::move(environment)) {}

void Compilation::AddError(std::wstring error) {
  // TODO: Enable this logging statement.
  // LOG(INFO) << "Compilation error: " << error;
  if (!source_.empty()) {
    Source last_source = source_.back();
    error = (last_source.path.has_value() ? (last_source.path->read() + L":")
                                          : L"") +
            std::to_wstring(last_source.line + 1) + L":" +
            std::to_wstring(last_source.column + 1) + L": " + error;
  }
  errors_.push_back(std::move(error));
}

const std::vector<std::wstring>& Compilation::errors() const { return errors_; }
std::vector<std::wstring>& Compilation::errors() { return errors_; }

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
