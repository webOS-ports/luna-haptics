/* @@@LICENSE
*
*      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#include "fixture.h"

namespace {

typedef HapticsTest Subscriptions;

const char* const kInfinite = "{\"period\":200}";

//! Starts an infinite vibration for caller; its unique token.
std::string startInfinite(HapticsTest* t, const fakebus::Caller& caller = fakebus::Caller())
{
    (void) t;
    EXPECT_TRUE(fakebus::callMethod("vibrate", kInfinite, caller));
    EXPECT_EQ(fakebus::replies().back(), kOk);
    return fakebus::lastToken();
}

TEST_F(Subscriptions, CancelStopsTheVibration)
{
    start();
    std::string token = startInfinite(this);
    int32_t id = fakenyx::playing();
    ASSERT_NE(id, 0);
    EXPECT_TRUE(fakebus::cancel(token));
    ASSERT_EQ(fakenyx::cancels().size(), 1u);
    EXPECT_EQ(fakenyx::cancels()[0], id);
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Subscriptions, CallerGoingAwayStopsTheVibration)
{
    start();
    fakebus::Caller app;
    app.sender = ":1.42";
    startInfinite(this, app);
    EXPECT_EQ(fakebus::disconnect(":1.42"), 1);
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Subscriptions, ContinousEffectIsCancelledToo)
{
    start();
    ASSERT_TRUE(fakebus::callMethod("vibrateNamedEffect", "{\"name\":\"ringtone\",\"continous\":true}"));
    EXPECT_TRUE(fakebus::cancel(fakebus::lastToken()));
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

// The modules play one effect at a time and cancel whatever plays. A
// subscription whose effect a later one replaced must not stop that later
// one when it ends.
TEST_F(Subscriptions, OlderSubscriptionEndingLeavesTheNewerVibration)
{
    start();
    fakebus::Caller a, b;
    a.sender = ":1.10";
    b.sender = ":1.20";
    std::string first = startInfinite(this, a);
    std::string second = startInfinite(this, b);
    int32_t newer = fakenyx::playing();

    EXPECT_TRUE(fakebus::cancel(first));
    EXPECT_TRUE(fakenyx::cancels().empty());
    EXPECT_EQ(fakenyx::playing(), newer) << "a stale cancel stopped someone else's vibration";
    EXPECT_EQ(tracked(), 1u);

    EXPECT_TRUE(fakebus::cancel(second));
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Subscriptions, OlderGoneCallerLeavesTheNewerVibration)
{
    start();
    fakebus::Caller a, b;
    a.sender = ":1.10";
    b.sender = ":1.20";
    startInfinite(this, a);
    startInfinite(this, b);
    int32_t newer = fakenyx::playing();
    EXPECT_EQ(fakebus::disconnect(":1.10"), 1);
    EXPECT_EQ(fakenyx::playing(), newer);
    EXPECT_EQ(fakebus::disconnect(":1.20"), 1);
    EXPECT_EQ(fakenyx::playing(), 0);
}

// Newest first: the newest one's end stops the motor; the one before it is
// then the newest open, and its end cancels again (whatever plays by then).
TEST_F(Subscriptions, NewestEndingFirst)
{
    start();
    std::string first = startInfinite(this);
    std::string second = startInfinite(this);
    EXPECT_TRUE(fakebus::cancel(second));
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 1u);
    EXPECT_TRUE(fakebus::cancel(first));
    EXPECT_EQ(fakenyx::cancels().size(), 2u);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Subscriptions, CallerWithSeveralSubscriptionsGoingAway)
{
    start();
    fakebus::Caller app;
    app.sender = ":1.7";
    startInfinite(this, app);
    startInfinite(this, app);
    startInfinite(this, app);
    EXPECT_EQ(fakebus::disconnect(":1.7"), 3);
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

// The tokens the daemon keeps are its own copies: each message is freed
// when its subscription ends, which the next comparison would read if not.
TEST_F(Subscriptions, ManyCallersInAnyOrder)
{
    start();
    std::vector<std::string> tokens;
    for (int i = 0; i < 50; ++i) {
        fakebus::Caller c;
        c.sender = ":1." + std::to_string(100 + i);
        tokens.push_back(startInfinite(this, c));
    }
    for (size_t i = 0; i < tokens.size(); i += 2)
        EXPECT_TRUE(fakebus::cancel(tokens[i]));
    EXPECT_EQ(tracked(), 25u);
    EXPECT_NE(fakenyx::playing(), 0) << "the newest (odd) one still runs";
    for (size_t i = tokens.size() - 1; i < tokens.size(); i -= 2)
        EXPECT_TRUE(fakebus::cancel(tokens[i]));
    EXPECT_EQ(tracked(), 0u);
    EXPECT_EQ(fakenyx::playing(), 0);
}

TEST_F(Subscriptions, FiniteCallsAreNotSubscriptions)
{
    start();
    ASSERT_TRUE(fakebus::callMethod("vibrate", "{\"period\":200,\"duration\":300}"));
    EXPECT_FALSE(fakebus::cancel(fakebus::lastToken()));
    EXPECT_TRUE(fakenyx::cancels().empty());
}

TEST_F(Subscriptions, EndingAnUnknownTokenDoesNothing)
{
    start();
    startInfinite(this);
    HapticsController::instance()->endSubscription(":1.999.1");
    HapticsController::instance()->endSubscription(nullptr);
    EXPECT_TRUE(fakenyx::cancels().empty());
    EXPECT_EQ(tracked(), 1u);
}

TEST_F(Subscriptions, NyxCancelFailingStillEndsTheSubscription)
{
    start();
    std::string token = startInfinite(this);
    fakenyx::failNext("nyx_haptics_cancel");
    EXPECT_TRUE(fakebus::cancel(token));
    EXPECT_EQ(tracked(), 0u);
    // TearDown: destroy() stops it with cancel_all.
}

// luna-service2 drops a handle's subscriptions on LSUnregister without
// calling the cancel function; destroy() stops the motor itself.
TEST_F(Subscriptions, DestroyStopsAnOpenOne)
{
    start();
    startInfinite(this);
    HapticsController::destroy();
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_GE(fakenyx::calls("nyx_haptics_cancel_all"), 1);
}

} // namespace
