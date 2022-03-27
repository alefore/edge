#ifndef __AFC_EDITOR_STATUS_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_STATUS_OUTPUT_PRODUCER_H__

#include <cwchar>
#include <list>
#include <memory>
#include <string>

#include "src/modifiers.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class Status;
class OpenBuffer;

class StatusOutputProducerSupplier {
 public:
  StatusOutputProducerSupplier(const Status& status, const OpenBuffer* buffer,
                               Modifiers modifiers);

  LineNumberDelta lines() const;

  std::unique_ptr<OutputProducer> CreateOutputProducer(
      LineColumnDelta size) const;

  LineWithCursor::Generator::Vector Produce(LineColumnDelta size) const;

 private:
  bool has_info_line() const;

  const Status& status_;
  // `buffer` will be null if this status isn't associated with a specific
  // buffer (i.e., if it's the editor's status).
  const OpenBuffer* const buffer_;
  const Modifiers modifiers_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_STATUS_OUTPUT_PRODUCER_H__
