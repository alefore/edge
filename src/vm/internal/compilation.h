#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "src/language/gc.h"

namespace afc {
namespace vm {

using std::list;
using std::string;
using std::unique_ptr;
using std::vector;
using std::wstring;

class VMType;
class Expression;
class Environment;

struct Compilation {
  void AddError(wstring error) {
    // TODO: Enable this logging statement.
    // LOG(INFO) << "Compilation error: " << error;
    errors.push_back(std::move(error));
  }

  language::gc::Pool& pool;

  // The directory containing the file currently being compiled. Used for
  // resolving relative paths (that are relative to this directory, rather than
  // to cwd).
  std::string directory;

  std::unique_ptr<Expression> expr = nullptr;
  std::vector<std::wstring> errors = {};

  std::vector<std::wstring> current_namespace = {};
  std::vector<VMType> current_class = {};
  language::gc::Root<Environment> environment;
  std::wstring last_token = L"";
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_COMPILATION_H__
