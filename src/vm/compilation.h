#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/infrastructure/dirname.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/overload.h"
#include "src/language/text/line_column.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"

namespace afc::vm {
class Expression;
class Environment;

struct Compilation {
 private:
  struct Source {
    std::optional<infrastructure::Path> path;
    language::text::LineColumn line_column = {};
  };

  // Stack of files from which we're reading, used for error reports.
  std::vector<Source> source_;

  std::vector<language::Error> errors_ = {};

 public:
  size_t numbers_precision = 5;

  language::gc::Pool& pool;

  std::unique_ptr<Expression> expr;

  Namespace current_namespace;
  std::vector<Type> current_class = {};
  language::gc::Root<Environment> environment;
  std::wstring last_token = L"";

  Compilation(language::gc::Pool& input_pool,
              language::gc::Root<Environment> input_environment);

  // Enable move construction/assignment.
  Compilation(Compilation&&) = default;
  Compilation& operator=(Compilation&&) = default;
  // Disable copy construction/assignment.
  Compilation(const Compilation&) = delete;
  Compilation& operator=(const Compilation&) = delete;

  void AddError(language::Error error);

  template <typename T>
  language::ValueOrError<T> RegisterErrors(language::ValueOrError<T> value) {
    std::visit(
        language::overload{[&](language::Error error) { AddError(error); },
                           [](const T&) {}},
        value);
    return value;
  }

  const std::vector<language::Error>& errors() const;
  std::vector<language::Error>& errors();

  void PushSource(std::optional<infrastructure::Path> path);
  void PopSource();
  void IncrementLine();
  void SetSourceColumnInLine(language::lazy_string::ColumnNumber column);
  std::optional<infrastructure::Path> current_source_path() const;
};

}  // namespace afc::vm

#endif  // __AFC_VM_COMPILATION_H__
