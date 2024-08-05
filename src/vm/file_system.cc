#include "src/vm/file_system.h"

#include "src/infrastructure/dirname.h"
#include "src/infrastructure/dirname_vm.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/vm/callbacks.h"
#include "src/vm/container.h"
#include "src/vm/string.h"

using afc::concurrent::MakeProtected;
using afc::concurrent::Protected;
using afc::infrastructure::FileSystemDriver;
using afc::infrastructure::Path;
using afc::language::EmptyValue;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
void RegisterFileSystemFunctions(
    language::gc::Pool& pool,
    language::NonNull<std::shared_ptr<FileSystemDriver>> file_system_driver,
    Environment& environment) {
  environment.Define(
      Identifier{LazyString{L"Unlink"}},
      vm::NewCallback(pool, PurityType{.writes_external_outputs = true},
                      [file_system_driver](Path target_path)
                          -> futures::ValueOrError<EmptyValue> {
                        return file_system_driver->Unlink(target_path);
                      }));
  environment.Define(
      Identifier{LazyString{L"Glob"}},
      vm::NewCallback(
          pool, kPurityTypeReader,
          [file_system_driver](LazyString pattern)
              -> futures::ValueOrError<NonNull<
                  std::shared_ptr<Protected<std::vector<std::wstring>>>>> {
            return file_system_driver->Glob(pattern).Transform(
                [](std::vector<Path> input)
                    -> language::ValueOrError<NonNull<std::shared_ptr<
                        Protected<std::vector<std::wstring>>>>> {
                  return language::Success(
                      MakeNonNullShared<Protected<std::vector<std::wstring>>>(
                          MakeProtected(language::container::MaterializeVector(
                              input | std::views::transform([](Path& path) {
                                // TODO(2024-08-04): Change VectorString to use
                                // LazyString and avoid the call to ToString.
                                return path.read().ToString();
                              })))));
                });
          }));
}
}  // namespace afc::vm
