#ifndef __AFC_EDITOR_CONCURRENT_VERSION_PROPERTY_RECEIVER_H__
#define __AFC_EDITOR_CONCURRENT_VERSION_PROPERTY_RECEIVER_H__

#include <map>
#include <memory>

#include "src/infrastructure/audio.h"
#include "src/infrastructure/time.h"
#include "src/language/error/log.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"

namespace afc::concurrent {

// The key that uniquely identifies a given property.
class VersionPropertyKey
    : public language::GhostType<VersionPropertyKey,
                                 language::lazy_string::LazyString> {
  friend class VersionPropertyReceiver;
};

// This class is thread-safe.
class VersionPropertyReceiver {
 public:
  using Key = VersionPropertyKey;
  enum class VersionExecution {
    // MarkVersionDone hasn't executed for the last value of version_.
    kRunning,
    // MarkVersionDone has run with the last value of version_.
    kDone
  };

  using VersionPropertyValue = std::variant<std::wstring, int, size_t>;

 private:
  struct Data {
    struct VersionValue {
      int version_id;
      VersionPropertyValue value;
    };
    std::map<Key, VersionValue> information = {};

    int version_id = 0;
    VersionExecution last_version_state = VersionExecution::kDone;
  };

 public:
  class Version {
    struct ConstructorAccessKey {};

   public:
    Version(ConstructorAccessKey,
            std::weak_ptr<concurrent::Protected<Data>> data, int version_id);
    ~Version();
    bool IsExpired() const;
    void SetValue(Key key, VersionPropertyValue value);

   private:
    friend VersionPropertyReceiver;

    const std::weak_ptr<concurrent::Protected<Data>> data_;
    const int version_id_;
  };

  language::NonNull<std::unique_ptr<Version>> StartNewVersion();

  struct PropertyValues {
    VersionExecution last_version_state;

    struct Value {
      enum class Status { kExpired, kCurrent };
      Status status;
      VersionPropertyValue value;
    };
    std::map<Key, Value> property_values;
  };
  PropertyValues GetValues() const;

 private:
  const language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data_ =
      language::MakeNonNullShared<concurrent::Protected<Data>>(Data{});
};

}  // namespace afc::concurrent

#endif  // __AFC_EDITOR_CONCURRENT_VERSION_PROPERTY_RECEIVER_H__
