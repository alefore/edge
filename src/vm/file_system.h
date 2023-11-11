#ifndef __AFC_VM_FILE_SYSTEM_H__
#define __AFC_VM_FILE_SYSTEM_H__

#include "src/infrastructure/file_system_driver.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/environment.h"

namespace afc::vm {
class Environment;
void RegisterFileSystemFunctions(
    language::gc::Pool& pool,
    language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>
        file_system_driver,
    Environment& environment);
}  // namespace afc::vm

#endif  // __AFC_VM_FILE_SYSTEM_H__
