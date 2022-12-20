#include "src/concurrent/bag.h"

#include <glog/logging.h>

#include <set>

#include "src/concurrent/protected.h"
#include "src/concurrent/thread_pool.h"
#include "src/tests/tests.h"

namespace afc::concurrent {
namespace {
Bag<size_t> NumbersBag(size_t begin, size_t end, size_t shards = 10) {
  Bag<size_t> bag(BagOptions{.shards = shards});
  for (size_t i = begin; i < end; i++) bag.Add(i);
  return bag;
}

std::set<size_t> BagToSet(const Bag<size_t>& bag) {
  std::set<size_t> output;
  bag.ForEachSerial([&](const size_t& v) { output.insert(v); });
  return output;
}

const bool tests_registration = tests::Register(
    L"Concurrent::Bag",
    {
        {.name = L"Empty",
         .callback =
             [] {
               ThreadPool thread_pool(5, nullptr);
               Bag<size_t> bag(BagOptions{.shards = 10});
               CHECK(bag.empty());
               CHECK_EQ(bag.size(), 0ul);
               CHECK(BagToSet(bag) == std::set<size_t>());
             }},
        {.name = L"AddSomeAndClear",
         .callback =
             [] {
               ThreadPool thread_pool(5, nullptr);
               Bag<size_t> bag = NumbersBag(0, 1000);

               CHECK(!bag.empty());
               CHECK_EQ(bag.size(), 1000ul);

               std::set<size_t> values_expected;
               for (size_t i = 0; i < 1000; i++) values_expected.insert(i);

               Protected<std::set<size_t>> values_concurrent;
               bag.ForEachShard(thread_pool, [&](std::list<size_t>& list) {
                 for (size_t value : list)
                   values_concurrent.lock()->insert(value);
               });

               CHECK(BagToSet(bag) == values_expected);
               CHECK(*values_concurrent.lock() == BagToSet(bag));

               bag.Clear(thread_pool);
               CHECK(bag.empty());
               CHECK_EQ(bag.size(), 0ul);
               bag.ForEachSerial(
                   [&](size_t) { LOG(FATAL) << "Values found."; });
               bag.ForEachShard(thread_pool,
                                [&](std::list<size_t> l) { CHECK(l.empty()); });
             }},
        {.name = L"Erase",
         .callback =
             [] {
               ThreadPool thread_pool(5, nullptr);
               Bag<size_t> bag(BagOptions{.shards = 10});
               std::optional<Bag<size_t>::iterator> iterator;
               for (size_t i = 0; i < 1000; i++)
                 if (i == 257)
                   iterator = bag.Add(i);
                 else
                   bag.Add(i);

               bag.erase(*iterator);

               std::set<size_t> values_expected;
               for (size_t i = 0; i < 1000; i++)
                 if (i != 257) values_expected.insert(i);

               std::set<size_t> values_serial;
               bag.ForEachSerial([&](size_t v) { values_serial.insert(v); });
             }},
        {.name = L"Consume",
         .callback =
             [] {
               ThreadPool thread_pool(5, nullptr);
               Bag<size_t> bag = NumbersBag(0, 100, 10);
               bag.Consume(thread_pool, NumbersBag(100, 200, 27));
               bag.Consume(thread_pool, NumbersBag(200, 300, 93));
               bag.Consume(thread_pool, NumbersBag(300, 400, 1));
               bag.Consume(thread_pool, NumbersBag(300, 500, 100));

               std::set<size_t> expected = BagToSet(NumbersBag(0, 500));
               CHECK_EQ(expected.size(), 500ul);
               CHECK(BagToSet(bag) == expected);
             }},
        {.name = L"RemoveIf",
         .callback =
             [] {
               ThreadPool thread_pool(5, nullptr);

               std::set<size_t> expected = BagToSet(NumbersBag(0, 100));
               expected.erase(27);
               expected.erase(51);
               expected.erase(71);
               CHECK_EQ(expected.size(), 97ul);

               Bag<size_t> bag = NumbersBag(0, 100, 10);
               bag.remove_if(thread_pool, [&](size_t i) {
                 return expected.find(i) == expected.end();
               });

               CHECK(BagToSet(bag) == expected);
             }},
        {.name = L"IteratorMove",
         .callback =
             [] {
               ThreadPool thread_pool(5, nullptr);
               Bag<size_t> bag = NumbersBag(0, 5);
               CHECK_EQ(bag.size(), 5ul);

               Bag<size_t> bag_old = NumbersBag(0, 4);
               Bag<size_t>::iterator it_4 = bag_old.Add(4);
               Bag<size_t>::iterator it_5 = bag_old.Add(5);
               Bag<size_t>::iterator it_6 = bag_old.Add(6);
               CHECK_EQ(bag_old.size(), 7ul);

               bag = std::move(bag_old);
               CHECK_EQ(bag.size(), 7ul);

               bag.erase(it_4);
               bag.erase(it_5);
               bag.erase(it_6);
               CHECK_EQ(bag.size(), 4ul);
               CHECK(BagToSet(bag) == BagToSet(NumbersBag(0, 4)));
             }},
        {.name = L"IteratorsBagEmpty",
         .callback =
             [] {
               ThreadPool thread_pool(5, nullptr);
               Bag<size_t> bag = NumbersBag(0, 100);
               Operation operation(thread_pool);
               BagIterators(bag).erase(operation);
             }},
        {.name = L"IteratorsBagSimple",
         .callback =
             [] {
               Bag<size_t> bag = NumbersBag(0, 100);

               std::set<size_t> expected = BagToSet(bag);
               expected.erase(27);
               expected.erase(51);
               expected.erase(71);
               CHECK_EQ(expected.size(), 97ul);

               ThreadPool thread_pool(5, nullptr);
               bag.remove_if(thread_pool, [&](size_t i) {
                 return expected.find(i) == expected.end();
               });

               CHECK(BagToSet(bag) == expected);

               Bag<size_t>::Iterators iterators_bag = BagIterators(bag);
               iterators_bag.Add(bag.Add(27));
               iterators_bag.Add(bag.Add(51));
               iterators_bag.Add(bag.Add(71));

               CHECK_EQ(iterators_bag.size(), 3ul);
               CHECK_EQ(bag.size(), 100ul);
               CHECK(BagToSet(bag) == BagToSet(NumbersBag(0, 100)));

               {
                 Operation operation(thread_pool);
                 std::move(iterators_bag).erase(operation);
               }

               CHECK_EQ(bag.size(), 97ul);
               CHECK(BagToSet(bag) == expected);
             }},
        {.name = L"IteratorsBagComplex",
         .callback =
             [] {
               Bag<size_t> bag = NumbersBag(0, 0);

               std::set<size_t> expected;
               CHECK(BagToSet(bag) == expected);

               Bag<size_t>::Iterators iterators_bag = BagIterators(bag);
               for (size_t i = 0; i < 100; i++) iterators_bag.Add(bag.Add(i));

               CHECK_EQ(iterators_bag.size(), 100ul);
               CHECK_EQ(bag.size(), 100ul);
               CHECK(BagToSet(bag) == BagToSet(NumbersBag(0, 100)));

               {
                 ThreadPool thread_pool(5, nullptr);
                 Operation operation(thread_pool);
                 std::move(iterators_bag).erase(operation);
               }

               CHECK_EQ(bag.size(), 0ul);
               CHECK(bag.empty());
               CHECK(BagToSet(bag) == expected);
             }},

    });
}  // namespace
}  // namespace afc::concurrent
