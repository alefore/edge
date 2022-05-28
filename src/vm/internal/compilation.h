#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <glog/logging.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/vm/public/types.h"

namespace afc::vm {
// TODO(easy, 2022-05-28): Get rid of these declarations.
using std::string;
using std::unique_ptr;
using std::vector;
using std::wstring;

class VMType;
class Expression;
class Environment;

struct Compilation {
  Compilation(language::gc::Pool& pool,
              language::gc::Root<Environment> environment);

  void AddError(std::wstring error);

  const std::vector<std::wstring>& errors() const;
  std::vector<std::wstring>& errors();

  language::gc::Pool& pool;

  // The directory containing the file currently being compiled. Used for
  // resolving relative paths (that are relative to this directory, rather than
  // to cwd).
  //
  // TODO(easy, 2022-05-28): Remove this and use source_ instead.
  std::string directory;

  std::unique_ptr<Expression> expr;

  std::vector<std::wstring> current_namespace = {};
  std::vector<VMType> current_class = {};
  language::gc::Root<Environment> environment;
  std::wstring last_token = L"";

  struct Source {
    std::optional<infrastructure::Path> path;
    size_t line = 0;
  };

  void PushSource(std::optional<infrastructure::Path> path);
  void PopSource();
  void IncrementLine();

 private:
  // Stack of files from which we're reading, used for error reports.
  std::vector<Source> source_;

  std::vector<std::wstring> errors_ = {};
};

}  // namespace afc::vm

#endif  // __AFC_VM_COMPILATION_H__
