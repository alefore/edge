#ifndef __AFC_VM_VARIABLE_LOOKUP_H__
#define __AFC_VM_VARIABLE_LOOKUP_H__

#include <memory>
#include <string>

namespace afc {
namespace vm {

using std::string;
using std::unique_ptr;

class Compilation;
class Expression;

unique_ptr<Expression> NewVariableLookup(
    Compilation* compilation, const string& symbol);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VARIABLE_LOOKUP_H__
