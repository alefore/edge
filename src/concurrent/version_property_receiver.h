#ifndef __AFC_EDITOR_CONCURRENT_VERSION_PROPERTY_RECEIVER_H__
#define __AFC_EDITOR_CONCURRENT_VERSION_PROPERTY_RECEIVER_H__

#include <map>
#include <memory>

#include "src/infrastructure/audio.h"
#include "src/infrastructure/time.h"
#include "src/language/error/log.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"

namespace afc::concurrent {

// The key that uniquely identifies a given property.
// TODO(easy, 2023-08-24): Why is this not just a single-line definition?
class VersionPropertyKey {
 public:
  GHOST_TYPE_CONSTRUCTOR(VersionPropertyKey, std::wstring, value);
  GHOST_TYPE_EQ(VersionPropertyKey, value);
  GHOST_TYPE_ORDER(VersionPropertyKey, value);
  GHOST_TYPE_OUTPUT_FRIEND(VersionPropertyKey, value);
  GHOST_TYPE_HASH_FRIEND(VersionPropertyKey, value);

  const std::wstring& read() const { return value; }

 private:
  friend class VersionPropertyReceiver;
  std::wstring value;
};

using ::operator<<;
GHOST_TYPE_OUTPUT(VersionPropertyKey, value);
}  // namespace afc::concurrent

GHOST_TYPE_HASH(afc::concurrent::VersionPropertyKey);

namespace afc::concurrent {
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

 private:
  struct Data {
    struct Value {
      int version_id;
      std::wstring value;
    };
    std::map<Key, Value> information = {};

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
    void SetValue(Key key, std::wstring value);
    void SetValue(Key key, int value);

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
      std::wstring value;
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
