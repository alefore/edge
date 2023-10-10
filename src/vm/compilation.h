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
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/types.h"

namespace afc::vm {
class Expression;
class Environment;

struct Compilation {
  Compilation(language::gc::Pool& input_pool,
              language::gc::Root<Environment> input_environment);

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

  language::gc::Pool& pool;

  std::unique_ptr<Expression> expr;

  Namespace current_namespace;
  std::vector<Type> current_class = {};
  language::gc::Root<Environment> environment;
  std::wstring last_token = L"";

  struct Source {
    std::optional<infrastructure::Path> path;
    size_t line = 0;
    size_t column = 0;
  };

  void PushSource(std::optional<infrastructure::Path> path);
  void PopSource();
  void IncrementLine();
  void SetSourceColumnInLine(size_t column);
  std::optional<infrastructure::Path> current_source_path() const;

 private:
  // Stack of files from which we're reading, used for error reports.
  std::vector<Source> source_;

  std::vector<language::Error> errors_ = {};
};

}  // namespace afc::vm

#endif  // __AFC_VM_COMPILATION_H__