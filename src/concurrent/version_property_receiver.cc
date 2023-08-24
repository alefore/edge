#include "src/concurrent/version_property_receiver.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/line.h"

namespace afc::concurrent {
namespace gc = language::gc;
namespace error = language::error;

using language::Error;
using language::MakeNonNullShared;
using language::NonNull;
using language::VisitPointer;

language::NonNull<std::unique_ptr<VersionPropertyReceiver::Version>>
VersionPropertyReceiver::StartNewVersion() {
  data_->version_id++;
  data_->last_version_state = VersionExecution::kRunning;
  return MakeNonNullUnique<Version>(Version::ConstructorAccessKey{}, data_);
}

VersionPropertyReceiver::Version::Version(
    ConstructorAccessKey,
    const NonNull<std::shared_ptr<VersionPropertyReceiver::Data>>& data)
    : data_(data.get_shared()), version_id_(data->version_id) {}

VersionPropertyReceiver::Version::~Version() {
  VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Data>> data) {
        std::erase_if(data->information,
                      [&](const std::pair<Key, Data::Value>& entry) {
                        return entry.second.version_id < version_id_;
                      });
        if (data->version_id == version_id_) {
          data->last_version_state = VersionExecution::kDone;
        }
      },
      [] {});
}

bool VersionPropertyReceiver::Version::IsExpired() const {
  return VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Data>> data) {
        return version_id_ < data->version_id;
      },
      [] { return true; });
}

void VersionPropertyReceiver::Version::SetValue(Key key, std::wstring value) {
  VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Data>> data) {
        if (auto& entry = data->information[key];
            entry.version_id <= version_id_) {
          entry = {.version_id = version_id_, .value = value};
        }
      },
      [] {});
}

void VersionPropertyReceiver::Version::SetValue(Key key, int value) {
  return SetValue(key, std::to_wstring(value));
}

VersionPropertyReceiver::PropertyValues VersionPropertyReceiver::GetValues()
    const {
  PropertyValues output{.last_version_state = data_->last_version_state,
                        .property_values = {}};
  for (const auto& [key, value] : data_->information)
    output.property_values.insert(
        {key, PropertyValues::Value{
                  .status = value.version_id < data_->version_id
                                ? PropertyValues::Value::Status::kExpired
                                : PropertyValues::Value::Status::kCurrent,
                  .value = value.value}});
  return output;
}

}  // namespace afc::concurrent
