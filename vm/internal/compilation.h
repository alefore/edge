#ifndef __AFC_VM_COMPILATION_H__
#define __AFC_VM_COMPILATION_H__

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

class VMType;
class Expression;
class Environment;

struct Compilation {
  unique_ptr<Expression> expr;
  vector<string> errors;

  // A stack: we push_back when starting compilation of a new function.
  list<VMType> return_types;
  Environment* environment;
  string last_token;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_COMPILATION_H__
