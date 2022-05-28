#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "src/language/gc.h"
#include "src/vm/public/types.h"

namespace afc::vm {
// TODO(easy, 2022-05-28): Get rid of these declarations.
using std::list;
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

  // TODO(easy, 2022-05-28): Move to compilation.cc?
  void AddError(std::wstring error) {
    // TODO: Enable this logging statement.
    // LOG(INFO) << "Compilation error: " << error;
    errors_.push_back(L":" + std::to_wstring(source_line) + L": " +
                      std::move(error));
  }

  const std::vector<std::wstring>& errors() const;
  std::vector<std::wstring>& errors();

  language::gc::Pool& pool;

  // The directory containing the file currently being compiled. Used for
  // resolving relative paths (that are relative to this directory, rather than
  // to cwd).
  std::string directory;

  std::unique_ptr<Expression> expr;

  std::vector<std::wstring> current_namespace = {};
  std::vector<VMType> current_class = {};
  language::gc::Root<Environment> environment;
  std::wstring last_token = L"";
  size_t source_line = 0;

 private:
  std::vector<std::wstring> errors_ = {};
};

}  // namespace afc::vm

#endif  // __AFC_VM_COMPILATION_H__
