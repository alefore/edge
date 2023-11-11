#include "src/vm/file_system.h"

#include "src/infrastructure/dirname.h"
#include "src/infrastructure/dirname_vm.h"
#include "src/vm/callbacks.h"

using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::language::EmptyValue;

namespace afc::vm {
void RegisterFileSystemFunctions(
    language::gc::Pool& pool,
    language::NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver,
    Environment& environment) {
  environment.Define(
      L"Unlink",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [file_system_driver](Path target_path)
                          -> futures::ValueOrError<EmptyValue> {
                        return file_system_driver->Unlink(target_path);
                      }));
}
}  // namespace afc::vm
