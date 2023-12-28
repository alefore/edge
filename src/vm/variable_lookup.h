#ifndef __AFC_VM_VARIABLE_LOOKUP_H__
#define __AFC_VM_VARIABLE_LOOKUP_H__

#include <list>
#include <memory>
#include <string>

#include "src/vm/types.h"

namespace afc::vm {
struct Compilation;
class Expression;

// Symbols is a list of tokens, including namespace or class prefixes. The last
// item will be the final symbol to look up.
std::unique_ptr<Expression> NewVariableLookup(Compilation& compilation,
                                              std::list<Identifier> symbols);

}  // namespace afc::vm

#endif  // __AFC_VM_VARIABLE_LOOKUP_H__
