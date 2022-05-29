#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/overload.h"
#include "src/language/value_or_error.h"
#include "src/vm/public/types.h"

namespace afc::vm {
class VMType;
class Expression;
class Environment;

struct Compilation {
  Compilation(language::gc::Pool& pool,
              language::gc::Root<Environment> environment);

  void AddError(language::Error error);

  template <typename T>
  language::ValueOrError<T> RegisterErrors(language::ValueOrError<T> value) {
    std::visit(
        language::overload{[&](language::Error error) { AddError(error); },
                           [](const T&) {}},
        value);
    return value;
  }

  const std::vector<std::wstring>& errors() const;
  std::vector<std::wstring>& errors();

  language::gc::Pool& pool;

  std::unique_ptr<Expression> expr;

  std::vector<std::wstring> current_namespace = {};
  std::vector<VMType> current_class = {};
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
  // TODO(easy, 2022-05-29): Turn this into Error
  std::vector<Source> source_;

  std::vector<std::wstring> errors_ = {};
};

}  // namespace afc::vm

#endif  // __AFC_VM_COMPILATION_H__
