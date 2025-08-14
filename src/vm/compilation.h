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
#include "src/language/lazy_string/lazy_string.h"
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
  struct ConstructorAccessTag {};

  struct Source {
    std::optional<infrastructure::Path> path;
    language::text::LineColumn line_column = {};
  };

  // Stack of files from which we're reading, used for error reports.
  std::vector<Source> source_;

  std::vector<language::Error> errors_ = {};

  std::vector<StackFrameHeader> stack_headers_ = {};

 public:
  static language::gc::Root<Compilation> New(
      language::gc::Ptr<Environment> environment);

  size_t numbers_precision = 5;

  language::gc::Pool& pool;

  std::optional<language::gc::Root<Expression>> expr;

  Namespace current_namespace;
  std::vector<Type> current_class = {};
  language::gc::Ptr<Environment> environment;
  language::lazy_string::LazyString last_token;

  Compilation(ConstructorAccessTag,
              language::gc::Ptr<Environment> input_environment);

  // Enable move construction/assignment.
  Compilation(Compilation&&) = default;
  Compilation& operator=(Compilation&&) = default;
  // Disable copy construction/assignment.
  Compilation(const Compilation&) = delete;
  Compilation& operator=(const Compilation&) = delete;

  language::Error AddError(language::Error error);

  void PushStackFrameHeader(StackFrameHeader);
  void PopStackFrameHeader();
  std::optional<std::reference_wrapper<StackFrameHeader>>
  CurrentStackFrameHeader();

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

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};

}  // namespace afc::vm

#endif  // __AFC_VM_COMPILATION_H__
