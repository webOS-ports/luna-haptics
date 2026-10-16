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

#include <utility>
#include <vector>

namespace {

typedef HapticsTest NamedEffect;

TEST_F(NamedEffect, EachNameIsItsEffect)
{
    start();
    const std::vector<std::pair<const char*, nyx_haptics_effect_type_t> > names = {
        { "ringtone", NYX_HAPTICS_EFFECT_RINGTONE }, { "alert", NYX_HAPTICS_EFFECT_ALERT },
        { "notification", NYX_HAPTICS_EFFECT_NOTIFICATION }, { "tapdown", NYX_HAPTICS_EFFECT_TAPDOWN },
        { "tapup", NYX_HAPTICS_EFFECT_TAPUP } };
    for (const auto& n : names) {
        std::string payload = std::string("{\"name\":\"") + n.first + "\"}";
        EXPECT_EQ(call("vibrateNamedEffect", payload.c_str()), kOk) << n.first;
        ASSERT_FALSE(fakenyx::vibrations().empty());
        EXPECT_EQ(fakenyx::vibrations().back().type, n.second) << n.first;
    }
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(NamedEffect, UnknownNameFails)
{
    start();
    for (const char* p : { "{\"name\":\"notifications\"}", "{\"name\":\"\"}", "{\"name\":\"Alert\"}",
                           "{\"name\":\"alert \"}" })
        EXPECT_EQ(call("vibrateNamedEffect", p), kFailed) << p;
    EXPECT_TRUE(fakenyx::vibrations().empty());
}

TEST_F(NamedEffect, NameMustBeAString)
{
    // json-c turns any value into a string on request ("5", "[ ]", "null"...);
    // none of those is a name.
    start();
    for (const char* p : { "{\"name\":5}", "{\"name\":null}", "{\"name\":[\"alert\"]}", "{\"name\":{}}",
                           "{\"name\":true}", "{}", "[]", "null", "\"alert\"", "{", "" })
        EXPECT_EQ(call("vibrateNamedEffect", p), kInvalid) << p;
    EXPECT_EQ(call("vibrateNamedEffect", nullptr), kInvalid);
    EXPECT_TRUE(fakenyx::vibrations().empty());
}

// The protocol's spelling, as the legacy audiod sends it for the ringtone.
TEST_F(NamedEffect, ContinousTiesTheEffectToTheCall)
{
    start();
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"ringtone\",\"continous\":true}"), kOk);
    EXPECT_EQ(tracked(), 1u);
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"ringtone\",\"continous\":false}"), kOk);
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"ringtone\"}"), kOk);
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"ringtone\",\"continuous\":true}"), kOk);
    EXPECT_EQ(tracked(), 1u);
}

// json-c's boolean of "false" is true: a non-empty string.
TEST_F(NamedEffect, ContinousMustBeABoolean)
{
    start();
    for (const char* p : { "{\"name\":\"alert\",\"continous\":\"false\"}", "{\"name\":\"alert\",\"continous\":1}",
                           "{\"name\":\"alert\",\"continous\":\"\"}", "{\"name\":\"alert\",\"continous\":[]}" })
        EXPECT_EQ(call("vibrateNamedEffect", p), kInvalid) << p;
    EXPECT_TRUE(fakenyx::vibrations().empty()) << "checked before vibrating";
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"alert\",\"continous\":null}"), kOk);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(NamedEffect, ContinousForAGoneCallerIsStopped)
{
    start();
    fakebus::Caller gone;
    gone.connected = false;
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"ringtone\",\"continous\":true}", gone), kFailed);
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

TEST_F(NamedEffect, NyxFailures)
{
    start();
    fakenyx::failNext("nyx_haptics_vibrate");
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"alert\",\"continous\":true}"), kFailed);
    fakenyx::failNext("nyx_haptics_get_effect_id");
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"alert\",\"continous\":true}"), kFailed);
    EXPECT_EQ(fakenyx::playing(), 0);
    EXPECT_EQ(tracked(), 0u);
}

} // namespace
