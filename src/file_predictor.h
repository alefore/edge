#ifndef __AFC_EDITOR_FILE_PREDICTOR_H__
#define __AFC_EDITOR_FILE_PREDICTOR_H__

#include "src/futures/futures.h"
#include "src/predictor.h"

namespace afc::editor {
futures::Value<PredictorOutput> FilePredictor(PredictorInput input);
}

#endif  // __AFC_EDITOR_FILE_PREDICTOR_H__
