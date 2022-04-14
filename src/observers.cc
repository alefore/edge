#include "src/observers.h"

#include "src/tests/tests.h"

namespace afc::editor {
void Observers::Add(Observers::Observer observer) {
  new_observers_.lock()->push_back(std::move(observer));
}

void Observers::Notify() {
  std::vector<Observer> new_observers;
  new_observers.swap(*new_observers_.lock());

  auto observers = observers_.lock();
  observers->insert(observers->end(), new_observers.begin(),
                    new_observers.end());

  bool expired_observers = false;
  for (auto& o : *observers) {
    switch (o()) {
      case State::kAlive:
        break;
      case State::kExpired:
        o = nullptr;
        expired_observers = true;
    }
  }
  if (expired_observers)
    observers->erase(std::remove(observers->begin(), observers->end(), nullptr),
                     observers->end());
}

futures::Value<EmptyValue> Observers::NewFuture() {
  futures::Future<EmptyValue> output;
  Add(Observers::Once(
      [consumer = std::move(output.consumer)] { consumer(EmptyValue()); }));
  return std::move(output.value);
}

namespace {
bool observers_test_registration = tests::Register(
    L"Observers",
    {{.name = L"NotifyCanTriggerAdd", .callback = [] {
        Observers observers;
        int runs_top = 0;
        int runs_bottom = 0;
        observers.Add([&] {
          if (runs_top == 0)
            observers.Add(Observers::Once([&] { runs_bottom++; }));
          runs_top++;
          return runs_top == 2 ? Observers::State::kExpired
                               : Observers::State::kAlive;
        });
        observers.Notify();
        CHECK_EQ(runs_top, 1);
        CHECK_EQ(runs_bottom, 0);

        observers.Notify();
        CHECK_EQ(runs_top, 2);
        CHECK_EQ(runs_bottom, 1);

        observers.Notify();
        CHECK_EQ(runs_top, 2);
        CHECK_EQ(runs_bottom, 1);
      }}});
}
}  // namespace afc::editor
