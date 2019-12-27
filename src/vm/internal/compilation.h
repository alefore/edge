#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>
#include <string>
#include <vector>

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

  // The directory containing the file currently being compiled. Used for
  // resolving relative paths (that are relative to this directory, rather than
  // to cwd).
  string directory;

  unique_ptr<Expression> expr;
  vector<wstring> errors;

  Environment* environment;
  wstring last_token;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_COMPILATION_H__
