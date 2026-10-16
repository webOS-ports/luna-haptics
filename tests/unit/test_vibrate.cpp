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

#include <climits>
#include <vector>

namespace {

typedef HapticsTest Vibrate;

TEST_F(Vibrate, PassesPeriodAndDurationToNyx)
{
    start();
    EXPECT_EQ(call("vibrate", "{\"period\":500,\"duration\":4000}"), kOk);
    ASSERT_EQ(fakenyx::vibrations().size(), 1u);
    const nyx_haptics_configuration_t& c = fakenyx::vibrations()[0];
    EXPECT_EQ(c.type, NYX_HAPTICS_EFFECT_UNDEFINED);
    EXPECT_EQ(c.period, 500);
    EXPECT_EQ(c.duration, 4000);
    EXPECT_EQ(tracked(), 0u) << "a finite vibration is not tied to the call";
}

// What the callers in the tree send: the Settings preview, cardshell's
// notification buzz and the dialpad's blip (shorter than its period).
TEST_F(Vibrate, TheCallersPayloads)
{
    start();
    for (const char* p : { "{\"period\": 200, \"duration\": 500}", "{\"period\":500,\"duration\":200}",
                           "{\"period\":100,\"duration\":10}" })
        EXPECT_EQ(call("vibrate", p), kOk) << p;
    EXPECT_EQ(fakenyx::vibrations().size(), 3u);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Vibrate, NoDurationIsInfiniteAndTiedToTheCall)
{
    start();
    for (const char* p : { "{\"period\":200}", "{\"period\":200,\"duration\":0}" }) {
        EXPECT_EQ(call("vibrate", p), kOk) << p;
        EXPECT_EQ(fakenyx::vibrations().back().duration, INT_MAX) << p;
    }
    EXPECT_EQ(tracked(), 2u);
}

TEST_F(Vibrate, PeriodZeroIsAllowed)
{
    // The timedoutput module takes it as one buzz for the whole duration.
    start();
    EXPECT_EQ(call("vibrate", "{\"period\":0,\"duration\":300}"), kOk);
    EXPECT_EQ(fakenyx::vibrations().back().period, 0);
}

// Negative values reached nyx, where the modules disagree on them (hybris
// refuses them, timedoutput plays a negative period as one buzz): they are
// refused before nyx sees them.
TEST_F(Vibrate, RefusesNegativeValues)
{
    start();
    for (const char* p : { "{\"period\":200,\"duration\":-1000}", "{\"period\":-1,\"duration\":500}",
                           "{\"period\":-200}", "{\"period\":200,\"duration\":-1}",
                           "{\"period\":200,\"duration\":-0.5}", "{\"period\":-1e300}",
                           "{\"period\":200,\"duration\":-2147483649}" })
        EXPECT_EQ(call("vibrate", p), kInvalid) << p;
    EXPECT_TRUE(fakenyx::vibrations().empty());
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Vibrate, RefusesWhatIsNotANumber)
{
    start();
    for (const char* p : { "{\"period\":\"500\"}", "{\"period\":true}", "{\"period\":null}",
                           "{\"period\":[500]}", "{\"period\":{}}", "{\"period\":500,\"duration\":\"4000\"}",
                           "{\"period\":500,\"duration\":false}", "{\"period\":500,\"duration\":[]}",
                           "{\"duration\":500}", "{}" })
        EXPECT_EQ(call("vibrate", p), kInvalid) << p;
    EXPECT_TRUE(fakenyx::vibrations().empty());
}

TEST_F(Vibrate, RefusesWhatIsNotAnObject)
{
    start();
    std::string deep(100000, '[');
    for (const char* p : { (const char*) nullptr, "", "[]", "null", "5", "\"period\"", "{", "{\"period\":",
                           "[{\"period\":500}]", deep.c_str() })
        EXPECT_EQ(call("vibrate", p), kInvalid) << (p ? std::string(p).substr(0, 20) : "(no payload)");
    EXPECT_TRUE(fakenyx::vibrations().empty());
}

TEST_F(Vibrate, NumbersOutOfRangeAreClampedNotWrapped)
{
    start();
    EXPECT_EQ(call("vibrate", "{\"period\":500.9,\"duration\":1e20}"), kOk);
    EXPECT_EQ(call("vibrate", "{\"period\":4294967796,\"duration\":9223372036854775807}"), kOk);
    ASSERT_EQ(fakenyx::vibrations().size(), 2u);
    EXPECT_EQ(fakenyx::vibrations()[0].period, 500);
    EXPECT_EQ(fakenyx::vibrations()[0].duration, INT_MAX);
    EXPECT_EQ(fakenyx::vibrations()[1].period, INT_MAX);
    EXPECT_EQ(fakenyx::vibrations()[1].duration, INT_MAX);
}

// json-c parses these; converting NaN to an int is undefined behaviour,
// which UBSan reports.
TEST_F(Vibrate, NonFiniteNumbers)
{
    start();
    EXPECT_EQ(call("vibrate", "{\"period\":NaN}"), kInvalid);
    EXPECT_EQ(call("vibrate", "{\"period\":200,\"duration\":-Infinity}"), kInvalid);
    std::string r = call("vibrate", "{\"period\":Infinity,\"duration\":100}");
    if (r == kOk)
        EXPECT_EQ(fakenyx::vibrations().back().period, INT_MAX);
    else
        EXPECT_EQ(r, kInvalid);
}

TEST_F(Vibrate, UnknownMembersAreIgnored)
{
    start();
    EXPECT_EQ(call("vibrate", "{\"period\":500,\"duration\":100,\"subscribe\":true,\"x\":[1,2]}"), kOk);
}

TEST_F(Vibrate, NoDeviceIsAFailureNotACrash)
{
    fakenyx::failNext("nyx_device_open");
    start();
    EXPECT_EQ(call("vibrate", "{\"period\":500,\"duration\":100}"), kFailed);
    EXPECT_EQ(call("vibrate", "{\"period\":500}"), kFailed);
    EXPECT_EQ(fakenyx::calls("nyx_haptics_vibrate"), 0);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Vibrate, NyxRefusingIsAFailure)
{
    start();
    fakenyx::failNext("nyx_haptics_vibrate");
    EXPECT_EQ(call("vibrate", "{\"period\":500}"), kFailed);
    EXPECT_EQ(tracked(), 0u);
}

// Playing, but no id to cancel it by: stopped, not left running behind a
// failure reply.
TEST_F(Vibrate, NoEffectIdStopsTheEffect)
{
    start();
    fakenyx::failNext("nyx_haptics_get_effect_id");
    EXPECT_EQ(call("vibrate", "{\"period\":500}"), kFailed);
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

// A caller that has gone by the time the daemon subscribes it: luna-service2
// refuses the subscription, and nothing could ever cancel the vibration.
TEST_F(Vibrate, InfiniteVibrationForAGoneCallerIsStopped)
{
    start();
    fakebus::Caller gone;
    gone.connected = false;
    EXPECT_EQ(call("vibrate", "{\"period\":200}", gone), kFailed);
    EXPECT_EQ(fakenyx::calls("nyx_haptics_vibrate"), 1);
    EXPECT_EQ(fakenyx::playing(), 0) << "an infinite vibration nobody can cancel";
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Vibrate, InfiniteVibrationThatCannotBeSubscribedIsStopped)
{
    start();
    fakebus::failNext("LSSubscriptionAdd");
    EXPECT_EQ(call("vibrate", "{\"period\":200}"), kFailed);
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(Vibrate, AFinishedCallerDoesNotMatterForAFiniteVibration)
{
    start();
    fakebus::Caller gone;
    gone.connected = false;
    EXPECT_EQ(call("vibrate", "{\"period\":200,\"duration\":400}", gone), kOk);
    EXPECT_NE(fakenyx::playing(), 0);
}

TEST_F(Vibrate, AFailedReplyIsFreed)
{
    start();
    fakebus::failNext("LSMessageReply");
    EXPECT_EQ(call("vibrate", "{\"period\":200,\"duration\":400}"), kOk);
    // TearDown: the LSError was freed.
}

} // namespace
