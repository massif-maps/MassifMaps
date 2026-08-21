/*
 * Tests for the facade API's event subscriptions: the three removals, dispatch order,
 * consumption, removal from inside a handler, and subscriptions dying with their target.
 */

#include "api/Context.h"
#include "components/FogOptions.h"

#include <memory>
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

    bool recordingHandler(void* userData, std::uint32_t, const char* event, std::uint32_t payload) {
        Record* record = static_cast<Record*>(userData);
        record->calls.push_back(std::string(event) + ":" + std::to_string(payload));
        return record->consumeResult;
    }

    /** Removes another subscription from inside a dispatch, which is the crash-prone case. */
    bool removingHandler(void* userData, std::uint32_t, const char* event, std::uint32_t) {
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
