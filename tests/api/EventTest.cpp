/*
 * Tests for the facade API's event subscriptions: the three removals, dispatch order,
 * consumption, removal from inside a handler, and subscriptions dying with their target.
 */

#include "api/Context.h"
#include "api/MassifApi.h"
#include "components/Exceptions.h"
#include "api/PropertyTable.h"
#include "components/FogOptions.h"
#include "ui/MapMoveInfo.h"

#include <chrono>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

namespace {

    struct Record {
        std::vector<std::string> calls;
        Context* context = nullptr;
        Subscription toRemove = NULL_SUBSCRIPTION;
        bool consumeResult = false;
    };

    int recordingHandler(void* userData, std::uint32_t, const char* event, std::uint32_t payload) {
        Record* record = static_cast<Record*>(userData);
        record->calls.push_back(std::string(event) + ":" + std::to_string(payload));
        return record->consumeResult;
    }

    /** Removes another subscription from inside a dispatch, which is the crash-prone case. */
    int removingHandler(void* userData, std::uint32_t, const char* event, std::uint32_t) {
        Record* record = static_cast<Record*>(userData);
        record->calls.push_back(std::string("remover:") + event);
        record->context->unsubscribe(record->toRemove);
        return false;
    }

    Handle registerFog(const std::shared_ptr<Context>& context, const std::string& id) {
        Handle handle = NULL_HANDLE;
        context->registerObject("options", id, std::make_shared<FogOptions>(),
                                "massif::FogOptions", handle);
        return handle;
    }

}

void testEvents() {
    auto context = std::make_shared<Context>();
    Handle target = registerFog(context, "a");
    Handle other = registerFog(context, "b");

    Record first, second;

    // Subscribe and deliver.
    Subscription s1 = context->subscribe(target, "click", &recordingHandler, &first, false);
    TEST_CHECK(s1 != NULL_SUBSCRIPTION, "subscribe returns a handle");
    TEST_CHECK(context->getSubscriptionCount() == 1, "one live subscription");
    context->emit(target, "click", 42);
    TEST_CHECK(first.calls.size() == 1 && first.calls[0] == "click:42", "the handler ran with the payload");

    // Only the matching target and event.
    context->emit(other, "click", 1);
    context->emit(target, "move", 1);
    TEST_CHECK(first.calls.size() == 1, "another target or event does not reach it");

    TEST_CHECK(context->subscribe(target, "click", nullptr, nullptr, false) == NULL_SUBSCRIPTION,
               "a null handler is refused");
    TEST_CHECK(context->subscribe(target + 7777, "click", &recordingHandler, &first, false) ==
               NULL_SUBSCRIPTION, "a stale target is refused");

    // Registration order, so which consuming handler wins is the app's choice, not a hash order.
    Subscription s2 = context->subscribe(target, "click", &recordingHandler, &second, true);
    first.calls.clear();
    second.consumeResult = true;
    TEST_CHECK(context->emit(target, "click", 7), "a consuming handler reports the event consumed");
    TEST_CHECK(first.calls.size() == 1 && second.calls.size() == 1, "both ran, in order");

    // A non-consuming subscription cannot stop anything even when it returns true.
    first.consumeResult = true;
    second.consumeResult = false;
    first.calls.clear();
    second.calls.clear();
    TEST_CHECK(!context->emit(target, "click", 8), "a non-consuming handler cannot consume");
    TEST_CHECK(second.calls.size() == 1, "so the later handler still ran");
    first.consumeResult = false;

    // Consumption stops the rest.
    second.consumeResult = true;
    Record third;
    Subscription s3 = context->subscribe(target, "click", &recordingHandler, &third, false);
    third.calls.clear();
    TEST_CHECK(context->emit(target, "click", 9), "consumed");
    TEST_CHECK(third.calls.empty(), "a handler after the consuming one is skipped");

    // off, and off twice.
    TEST_CHECK(context->unsubscribe(s3), "unsubscribe reports it was live");
    TEST_CHECK(!context->unsubscribe(s3), "and not the second time");
    TEST_CHECK(!context->unsubscribe(NULL_SUBSCRIPTION), "the null subscription is not live");

    // Removing from inside a handler must not disturb the pass in progress.
    Record remover;
    remover.context = context.get();
    second.consumeResult = false;
    context->unsubscribe(s1);
    context->unsubscribe(s2);
    Record late;
    Subscription toRemove = context->subscribe(target, "click", &recordingHandler, &late, false);
    remover.toRemove = toRemove;
    // The remover is registered FIRST, so it kills a subscription the pass has not reached yet.
    Subscription removerSub = context->subscribe(target, "click", &removingHandler, &remover, false);
    TEST_CHECK(context->unsubscribe(removerSub), "reorder: drop and re-add so the remover is first");
    removerSub = NULL_SUBSCRIPTION;
    context->unsubscribe(toRemove);
    remover.calls.clear();
    late.calls.clear();
    removerSub = context->subscribe(target, "click", &removingHandler, &remover, false);
    toRemove = context->subscribe(target, "click", &recordingHandler, &late, false);
    remover.toRemove = toRemove;
    context->emit(target, "click", 5);
    TEST_CHECK(remover.calls.size() == 1, "the remover ran");
    TEST_CHECK(late.calls.empty(), "a handler removed during the pass is skipped, not called");

    // off_event and off_all.
    context->unsubscribeAll(target);
    TEST_CHECK(context->getSubscriptionCount() == 0, "off_all clears the target");

    Record x, y, z;
    context->subscribe(target, "click", &recordingHandler, &x, false);
    context->subscribe(target, "move", &recordingHandler, &y, false);
    context->subscribe(other, "click", &recordingHandler, &z, false);
    TEST_CHECK(context->unsubscribeEvent(target, "click") == 1, "off_event removes just that event");
    TEST_CHECK(context->getSubscriptionCount() == 2, "and leaves the others");
    TEST_CHECK(context->unsubscribeAll(target) == 1, "off_all removes the target's remainder");
    TEST_CHECK(context->getSubscriptionCount() == 1, "and leaves another target alone");

    // Subscriptions die with their target, or the first destroy is a use-after-free.
    context->unregisterObject("options", "b");
    TEST_CHECK(context->getSubscriptionCount() == 0, "destroying a target drops its subscriptions");
}

namespace {
    int posted = 0;
    void countingDispatcher(void*, void (*)(void*), void*) { posted++; }
}

void testDelivery() {
    auto context = std::make_shared<Context>();
    Handle target = registerFog(context, "a");
    Handle payload = registerFog(context, "p");

    Record record;
    TEST_CHECK(context->subscribe(target, "click", &recordingHandler, &record, true,
                                  DELIVERY_UI, false) == NULL_SUBSCRIPTION,
               "a consuming handler cannot be queued");

    // Without a dispatcher a queued subscription runs inline rather than being dropped.
    Subscription inline_ = context->subscribe(target, "click", &recordingHandler, &record, false,
                                              DELIVERY_UI, false);
    context->emit(target, "click", 1);
    TEST_CHECK(record.calls.size() == 1, "with no dispatcher it delivers inline");
    context->unsubscribe(inline_);

    posted = 0;
    context->setUiDispatcher(&countingDispatcher, nullptr);
    record.calls.clear();

    Subscription queued = context->subscribe(target, "click", &recordingHandler, &record, false,
                                             DELIVERY_UI, false);
    context->emit(target, "click", payload);
    TEST_CHECK(record.calls.empty(), "a queued handler does not run during emit");
    TEST_CHECK(context->getQueuedCount() == 1, "it is waiting");
    TEST_CHECK(posted == 1, "and the dispatcher was asked once");

    context->emit(target, "click", payload);
    TEST_CHECK(context->getQueuedCount() == 2, "a second event queues too");
    TEST_CHECK(posted == 1, "but the drain is only posted once per batch");

    TEST_CHECK(context->drainQueue() == 2, "the drain delivers both");
    TEST_CHECK(record.calls.size() == 2, "so the handler ran twice");
    TEST_CHECK(context->getQueuedCount() == 0, "and the queue is empty");

    // Coalescing keeps one pending event per subscription.
    context->unsubscribe(queued);
    record.calls.clear();
    Subscription merged = context->subscribe(target, "move", &recordingHandler, &record, false,
                                             DELIVERY_UI, true);
    context->emit(target, "move", payload);
    context->emit(target, "move", payload);
    context->emit(target, "move", payload);
    TEST_CHECK(context->getQueuedCount() == 1, "a coalescing subscription keeps one pending event");
    TEST_CHECK(context->drainQueue() == 1 && record.calls.size() == 1, "and delivers it once");
    context->unsubscribe(merged);

    // A payload is retained while queued, so destroying its id does not free it early.
    Subscription late = context->subscribe(target, "click", &recordingHandler, &record, false,
                                           DELIVERY_UI, false);
    context->emit(target, "click", payload);
    context->unregisterObject("options", "p");
    TEST_CHECK(context->findObject("options", "p") == NULL_HANDLE, "the payload's id is gone");
    PropertyValue value;
    TEST_CHECK(context->getProperty(payload, "rangeStart", value) == RESULT_OK,
               "but the queued payload handle still resolves");
    context->drainQueue();
    TEST_CHECK(context->getProperty(payload, "rangeStart", value) == RESULT_BAD_HANDLE,
               "and is freed once the handler has run");

    // Unsubscribing between the emit and the drain must still release the payload.
    Handle second = registerFog(context, "q");
    context->emit(target, "click", second);
    context->unsubscribe(late);
    record.calls.clear();
    context->drainQueue();
    TEST_CHECK(record.calls.empty(), "a handler unsubscribed before the drain does not run");
    context->unregisterObject("options", "q");
    TEST_CHECK(context->getProperty(second, "rangeStart", value) == RESULT_BAD_HANDLE,
               "and its payload is still released");
}

namespace {
    /** What a Java or Objective-C handler is on the C++ side: a director returning a bool. */
    struct CountingListener : EventListener {
        int calls = 0;
        bool claim = false;

        bool onEvent(int, const std::string&, int) override {
            calls++;
            return claim;
        }
    };
}

void testBindingListenerLifetime();

/**
 * MassifApi::on, the entry point every SWIG binding subscribes through.
 *
 * Its consume flag used to be a hardcoded false, so a handler could return true and nothing ever
 * read it - the bool travelled from Java or Objective-C all the way to Context and was dropped at
 * the last step. Context's own consumption was tested; this call was not, because MassifApi.cpp
 * needed the renderer to link. It is testable now that the subscription half is its own TU.
 */
void testBindingSubscriptions() {
    const std::shared_ptr<Context>& context = Context::GetDefault();
    Handle target = registerFog(context, "binding-target");

    bool threw = false;
    try {
        MassifApi::on(static_cast<int>(target), "click", nullptr, 0, false);
    } catch (const NullArgumentException&) {
        threw = true;
    }
    TEST_CHECK(threw, "a null listener is refused");

    // Returns true, but did not ask to consume, so the event is not claimed.
    auto loud = std::make_shared<CountingListener>();
    loud->claim = true;
    int quiet = MassifApi::on(static_cast<int>(target), "click", loud, 0, false);
    TEST_CHECK(quiet != 0, "a non-consuming subscription is accepted");
    TEST_CHECK(!context->emit(target, "click", 0), "a non-consuming handler cannot claim the event");
    TEST_CHECK(loud->calls == 1, "though it still ran");
    MassifApi::off(quiet);

    // The same listener, subscribed as consuming: now the return value is what the SDK acts on.
    auto claiming = std::make_shared<CountingListener>();
    claiming->claim = true;
    int consuming = MassifApi::on(static_cast<int>(target), "click", claiming, 0, false, "", true);
    TEST_CHECK(consuming != 0, "a consuming subscription is accepted");
    TEST_CHECK(context->emit(target, "click", 0), "and its true reaches the SDK");
    TEST_CHECK(claiming->calls == 1, "having run once");

    // False is not "no answer": a consuming handler that declines leaves the event unclaimed.
    claiming->claim = false;
    TEST_CHECK(!context->emit(target, "click", 0), "a consuming handler that returns false declines");
    MassifApi::off(consuming);

    // The SDK asks synchronously, so a consuming subscription cannot be queued for another thread.
    auto queued = std::make_shared<CountingListener>();
    TEST_CHECK(MassifApi::on(static_cast<int>(target), "click", queued, 1, false, "", true) == 0,
               "a consuming subscription cannot ask for another thread");

    TEST_CHECK(MassifApi::on(9999, "click", queued, 0, false) == 0, "a stale handle gives 0");

    context->unregisterObject("options", "binding-target");
    testBindingListenerLifetime();
}

/**
 * The listener registry must not outlive the subscriptions it exists for.
 *
 * `on` keeps a shared_ptr per subscription, because Context holds only a raw pointer to the
 * director. `off` dropped its entry, but `offEvent`, `offAll` and the death of a target remove
 * subscriptions WITHOUT naming them - so every listener taken that way stayed referenced for the
 * process. MassifMap.detach calls offAll, which made it one leak per screen.
 *
 * A weak_ptr is what makes it observable: the registry holds the only other strong reference, so
 * an expired weak_ptr means the entry is gone and a live one means it leaked.
 */
void testBindingListenerLifetime() {
    const std::shared_ptr<Context>& context = Context::GetDefault();
    Handle target = registerFog(context, "lifetime-target");
    Handle other = registerFog(context, "lifetime-other");

    std::weak_ptr<EventListener> byEvent, byTarget, byDestroy, survivor;
    {
        auto a = std::make_shared<CountingListener>();
        auto b = std::make_shared<CountingListener>();
        auto c = std::make_shared<CountingListener>();
        auto d = std::make_shared<CountingListener>();
        byEvent = a;
        byTarget = b;
        byDestroy = c;
        survivor = d;
        MassifApi::on(static_cast<int>(target), "click", a, 0, false);
        MassifApi::on(static_cast<int>(target), "move", b, 0, false);
        MassifApi::on(static_cast<int>(other), "click", c, 0, false);
        MassifApi::on(static_cast<int>(other), "move", d, 0, false);
    }
    TEST_CHECK(!byEvent.expired() && !survivor.expired(), "a live subscription keeps its listener");

    TEST_CHECK(MassifApi::offEvent(static_cast<int>(target), "click") == 1, "offEvent removed one");
    TEST_CHECK(byEvent.expired(), "and released its listener");
    TEST_CHECK(!byTarget.expired(), "leaving the other event on the same target alone");

    TEST_CHECK(MassifApi::offAll(static_cast<int>(target)) == 1, "offAll removed the remainder");
    TEST_CHECK(byTarget.expired(), "and released that listener too");
    TEST_CHECK(!byDestroy.expired(), "another target's listeners are untouched");

    // A destroyed target takes its subscriptions with it, and it does not name them either. The
    // sweep runs on the next add or remove, so the orphan cannot outlive one call.
    context->unregisterObject("options", "lifetime-other");
    TEST_CHECK(!byDestroy.expired(), "a destroy alone does not reach the registry");
    Handle sweeper = registerFog(context, "lifetime-sweeper");
    auto trigger = std::make_shared<CountingListener>();
    MassifApi::on(static_cast<int>(sweeper), "click", trigger, 0, false);
    TEST_CHECK(byDestroy.expired() && survivor.expired(),
               "and the next subscription sweeps what the destroy orphaned");

    MassifApi::offAll(static_cast<int>(sweeper));
    context->unregisterObject("options", "lifetime-sweeper");
}

namespace {

    /** The payload handle a map-move handler was given, so the test can read it back. */
    struct MoveRecord {
        std::vector<std::string> events;
        std::vector<long long> reasons;
        Context* context = nullptr;
    };

    int moveHandler(void* userData, std::uint32_t, const char* event, std::uint32_t payload) {
        MoveRecord* record = static_cast<MoveRecord*>(userData);
        record->events.push_back(event);
        PropertyValue value;
        // -1 rather than skipping: a reason that failed to read must fail the assert below, not
        // silently shorten the list and pass.
        record->reasons.push_back(record->context->getProperty(payload, "reason", value) == RESULT_OK
                                  ? value.asLong() : -1);
        return false;
    }

}

void testMapMoveReason() {
    // The reason reaches the facade as an ordinary readable property on the payload, so a binding
    // needs no special case for it - the point of giving map.moved/map.stable a payload class.
    const ClassEntry* moveInfo = findClass("massif::MapMoveInfo");
    TEST_CHECK(moveInfo != nullptr, "the generated table has MapMoveInfo");
    const PropertyEntry* reason = findProperty(moveInfo, "reason");
    TEST_CHECK(reason && reason->type == PT_ENUM, "reason is typed as an enum, not an int");
    TEST_CHECK(reason && !reason->setter, "reason is read-only");

    auto context = std::make_shared<Context>();
    MoveRecord record;
    record.context = context.get();

    Handle target = NULL_HANDLE;
    context->registerObject("move", "a", std::make_shared<MapMoveInfo>(MapMoveReason::MAP_MOVE_REASON_GESTURE),
                            "massif::MapMoveInfo", target);
    PropertyValue value;
    TEST_CHECK(context->getProperty(target, "reason", value) == RESULT_OK &&
               value.asLong() == MapMoveReason::MAP_MOVE_REASON_GESTURE,
               "a GESTURE payload reads back as GESTURE");

    // Each reason is distinct, or an app filtering on one of them silently matches another.
    auto readReason = [&](MapMoveReason::MapMoveReason r) {
        Handle handle = NULL_HANDLE;
        context->registerObject("move", "r" + std::to_string(static_cast<int>(r)),
                                std::make_shared<MapMoveInfo>(r), "massif::MapMoveInfo", handle);
        PropertyValue out;
        context->getProperty(handle, "reason", out);
        return out.asLong();
    };
    long long gesture = readReason(MapMoveReason::MAP_MOVE_REASON_GESTURE);
    long long animation = readReason(MapMoveReason::MAP_MOVE_REASON_ANIMATION);
    long long api = readReason(MapMoveReason::MAP_MOVE_REASON_API);
    TEST_CHECK(gesture != animation && animation != api && gesture != api,
               "the three reasons are distinct values");

    // And through a subscription, which is how an app actually sees it.
    Subscription subscription = context->subscribe(target, "map.stable", &moveHandler, &record, false);
    TEST_CHECK(subscription != NULL_SUBSCRIPTION, "a map.stable subscription registers");
    context->unsubscribe(subscription);
}

void testThrottle() {
    auto context = std::make_shared<Context>();
    Handle target = NULL_HANDLE;
    context->registerObject("options", "throttled", std::make_shared<FogOptions>(),
                            "massif::FogOptions", target);

    Record record;
    Subscription subscription = context->subscribe(target, "map.moved", &recordingHandler, &record,
                                                   false, DELIVERY_ORIGIN, false, "", 50);
    TEST_CHECK(subscription != NULL_SUBSCRIPTION, "a throttled subscription registers");

    // The first event of a throttled subscription must get through - a window starting at "now"
    // would swallow it, which is the bug the epoch-initialised stamp exists to avoid.
    context->emit(target, "map.moved", NULL_HANDLE);
    TEST_CHECK(record.calls.size() == 1, "the first event is delivered");

    // A burst inside the window collapses to nothing more.
    for (int i = 0; i < 20; i++) {
        context->emit(target, "map.moved", NULL_HANDLE);
    }
    TEST_CHECK(record.calls.size() == 1, "events inside the window are dropped");

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    context->emit(target, "map.moved", NULL_HANDLE);
    TEST_CHECK(record.calls.size() == 2, "an event after the window is delivered");

    // Throttling is per subscription, not per event name: a second handler has its own window.
    Record other;
    Subscription second = context->subscribe(target, "map.moved", &recordingHandler, &other, false,
                                             DELIVERY_ORIGIN, false, "", 0);
    context->emit(target, "map.moved", NULL_HANDLE);
    TEST_CHECK(record.calls.size() == 2 && other.calls.size() == 1,
               "an unthrottled handler beside a throttled one still gets every event");

    // A consuming subscription cannot be throttled - the SDK is waiting for the answer now.
    Record consuming;
    TEST_CHECK(context->subscribe(target, "map.clicked", &recordingHandler, &consuming, true,
                                  DELIVERY_ORIGIN, false, "", 50) == NULL_SUBSCRIPTION,
               "a consuming subscription refuses to be throttled");

    context->unsubscribe(subscription);
    context->unsubscribe(second);
}
