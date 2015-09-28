#ifndef __AFC_VM_VARIABLE_LOOKUP_H__
#define __AFC_VM_VARIABLE_LOOKUP_H__

#include <memory>
#include <string>

namespace afc {
namespace vm {

using std::unique_ptr;
using std::wstring;

class Compilation;
class Expression;

unique_ptr<Expression> NewVariableLookup(
    Compilation* compilation, const wstring& symbol);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_VARIABLE_LOOKUP_H__
