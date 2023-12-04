#ifndef __AFC_INFRASTRUCTURE_REGULAR_FILE_ADAPTER_H__
#define __AFC_INFRASTRUCTURE_REGULAR_FILE_ADAPTER_H__

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "src/futures/futures.h"
#include "src/infrastructure/file_adapter.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"

namespace afc::infrastructure {
class RegularFileAdapter : public FileAdapter {
 public:
  struct Options {
    concurrent::ThreadPoolWithWorkQueue& thread_pool;
    std::function<void(std::vector<language::NonNull<
                           std::shared_ptr<const language::text::Line>>>)>
        insert_lines;
  };

 private:
  const Options options_;

 public:
  RegularFileAdapter(Options options);
  void UpdateSize() override;

  std::optional<language::text::LineColumn> position() const override;
  void SetPositionToZero() override;

  futures::Value<language::EmptyValue> ReceiveInput(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
      const infrastructure::screen::LineModifierSet& modifiers) override;

  bool WriteSignal(infrastructure::UnixSignal signal) override;
};
}  // namespace afc::infrastructure
#endif  // __AFC_INFRASTRUCTURE_REGULAR_FILE_ADAPTER_H__
