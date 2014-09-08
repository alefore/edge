#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace vm {

using std::string;
using std::unique_ptr;
using std::vector;

class Expression;
class Environment;

struct Compilation {
  unique_ptr<Expression> expr;
  vector<string> errors;

  Environment* environment;
  string last_token;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_COMPILATION_H__
