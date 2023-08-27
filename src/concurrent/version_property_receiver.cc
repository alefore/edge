#include "src/concurrent/version_property_receiver.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/text/line.h"

namespace afc::concurrent {
namespace gc = language::gc;
namespace error = language::error;

using concurrent::Protected;
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::VisitPointer;

language::NonNull<std::unique_ptr<VersionPropertyReceiver::Version>>
VersionPropertyReceiver::StartNewVersion() {
  return data_->lock([this](Data& data) {
    data.version_id++;
    data.last_version_state = VersionExecution::kRunning;
    return MakeNonNullUnique<Version>(Version::ConstructorAccessKey{},
                                      data_.get_shared(), data.version_id);
  });
}

VersionPropertyReceiver::Version::Version(
    ConstructorAccessKey,
    std::weak_ptr<Protected<VersionPropertyReceiver::Data>> data,
    int version_id)
    : data_(std::move(data)), version_id_(version_id) {}

VersionPropertyReceiver::Version::~Version() {
  VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Protected<Data>>> protected_data) {
        protected_data->lock([&](VersionPropertyReceiver::Data& data) {
          std::erase_if(data.information,
                        [&](const std::pair<Key, Data::VersionValue>& entry) {
                          return entry.second.version_id < version_id_;
                        });
          if (data.version_id == version_id_) {
            data.last_version_state = VersionExecution::kDone;
          }
        });
      },
      [] {});
}

bool VersionPropertyReceiver::Version::IsExpired() const {
  return VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Protected<Data>>> protected_data) {
        return protected_data->lock([&](VersionPropertyReceiver::Data& data) {
          return version_id_ < data.version_id;
        });
      },
      [] { return true; });
}

void VersionPropertyReceiver::Version::SetValue(Key key,
                                                VersionPropertyValue value) {
  VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Protected<Data>>> protected_data) {
        return protected_data->lock([&](VersionPropertyReceiver::Data& data) {
          if (auto& entry = data.information[key];
              entry.version_id <= version_id_) {
            entry = {.version_id = version_id_, .value = value};
          }
        });
      },
      [] {});
}

VersionPropertyReceiver::PropertyValues VersionPropertyReceiver::GetValues()
    const {
  return data_->lock([](const Data& data) {
    PropertyValues output{.last_version_state = data.last_version_state,
                          .property_values = {}};
    for (const auto& [key, value] : data.information)
      output.property_values.insert(
          {key, PropertyValues::Value{
                    .status = value.version_id < data.version_id
                                  ? PropertyValues::Value::Status::kExpired
                                  : PropertyValues::Value::Status::kCurrent,
                    .value = value.value}});
    return output;
  });
}

}  // namespace afc::concurrent
