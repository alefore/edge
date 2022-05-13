#ifndef __AFC_VM_INTERNAL_NUMBERS_H__
#define __AFC_VM_INTERNAL_NUMBERS_H__

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class Environment;
void RegisterNumberFunctions(language::gc::Pool& pool,
                             Environment& environment);
}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_NUMBERS_H__
