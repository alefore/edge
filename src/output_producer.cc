#include "src/output_producer.h"

#include <glog/logging.h>

#include "src/hash.h"
#include "src/line.h"
#include "src/line_column.h"

namespace afc::editor {
namespace {
class ConstantProducer : public OutputProducer {
 public:
  // TODO: Add a hash.
  ConstantProducer(LineWithCursor line)
      : generator_(
            {hash_combine(line.line->GetHash(),
                          line.cursor.has_value()
                              ? std::hash<ColumnNumber>{}(line.cursor.value())
                              : 0),
             [line] { return line; }}) {}

  Generator Next() override { return generator_; }

 private:
  const Generator generator_;
};
}  // namespace

/* static */ OutputProducer::LineWithCursor
OutputProducer::LineWithCursor::Empty() {
  return OutputProducer::LineWithCursor{std::make_shared<Line>(), std::nullopt};
}

/* static */ std::unique_ptr<OutputProducer> OutputProducer::Empty() {
  return OutputProducer::Constant(LineWithCursor::Empty());
}

/* static */ std::unique_ptr<OutputProducer> OutputProducer::Constant(
    LineWithCursor output) {
  return std::make_unique<ConstantProducer>(output);
}

}  // namespace afc::editor
