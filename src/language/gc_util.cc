#include "src/language/gc_util.h"

#include <vector>

#include "src/futures/delete_notification.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

using afc::futures::DeleteNotification;

namespace afc::language::gc {
struct TestData;

struct TestData {
  NonNull<std::unique_ptr<DeleteNotification>> delete_notification;
  std::string entry;
  std::optional<gc::Ptr<TestData>> next;

  TestData(std::string entry_input) : entry(entry_input) {}

  TestData(const TestData&) = delete;

  TestData(TestData&& o)
      : delete_notification(std::move(o.delete_notification)),
        entry(std::move(o.entry)),
        next(std::move(o.next)) {
    LOG(INFO) << "Move: " << entry;
  }

  ~TestData() { LOG(INFO) << "Delete: " << entry; }

  std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand() const {
    LOG(INFO) << "Expand: " << entry;
    if (next.has_value()) return {next->object_metadata()};
    return {};
  }
};

namespace {
const bool tests_registration = tests::Register(
    L"gc::BindFront",
    {{.name = L"Expand", .callback = [] {
        Pool pool{Pool::Options()};
        LOG(INFO) << "Creating roots.";
        std::optional<gc::Root<TestData>> a =
            pool.NewRoot(MakeNonNullUnique<TestData>(TestData("a")));
        std::optional<gc::Root<TestData>> b =
            pool.NewRoot(MakeNonNullUnique<TestData>(TestData("b")));

        LOG(INFO) << "Capturing notifications.";
        auto a_notification = a->ptr()->delete_notification->listenable_value();
        auto b_notification = b->ptr()->delete_notification->listenable_value();

        LOG(INFO) << "Creating a loop.";
        a->ptr()->next = b->ptr();
        b->ptr()->next = a->ptr();

        LOG(INFO) << "Building callable.";
        auto callable = std::make_optional(BindFront(
            pool,
            [](const gc::Ptr<TestData>& data, std::string x) {
              return data->entry + ":" + x;
            },
            a->ptr()));

        LOG(INFO) << "Releasing roots.";
        a = std::nullopt;
        b = std::nullopt;

        LOG(INFO) << "Collecting.";
        pool.FullCollect();

        CHECK(!a_notification.has_value());
        CHECK(!b_notification.has_value());

        LOG(INFO) << "Releasing callable.";

        CHECK_EQ(callable->ptr().value()("foo").value(), "a:foo");
        callable = std::nullopt;

        LOG(INFO) << "Collecting.";
        pool.FullCollect();

        CHECK(a_notification.has_value());
        CHECK(b_notification.has_value());
      }}});
}  // namespace
}  // namespace afc::language::gc
