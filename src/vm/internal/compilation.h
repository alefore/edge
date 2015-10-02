#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <list>
#include <memory>
#include <string>
#include <vector>

#include <glog/logging.h>

namespace afc {
namespace vm {

using std::list;
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

  unique_ptr<Expression> expr;
  vector<wstring> errors;

  // A stack: we push_back when starting compilation of a new function.
  list<VMType> return_types;
  Environment* environment;
  wstring last_token;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_COMPILATION_H__
