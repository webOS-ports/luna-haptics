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

#include <csignal>

#include "Daemon.h"

namespace {

typedef HapticsTest Daemon;

struct Script {
    int signal;
    std::string reply;      // what vibrate answered while the loop ran
    int32_t playing;        // the effect playing then
};

// Runs once the loop is up: starts an infinite vibration as a client
// would, then signals.
gboolean whileRunning(gpointer data)
{
    Script* s = static_cast<Script*>(data);
    if (fakebus::callMethod("vibrate", "{\"period\":200}") && !fakebus::replies().empty())
        s->reply = fakebus::replies().back();
    s->playing = fakenyx::playing();
    raise(s->signal);
    return G_SOURCE_REMOVE;
}

int runUntil(int signal, Script* out = nullptr)
{
    Script s = { signal, std::string(), 0 };
    g_timeout_add(10, whileRunning, &s);
    int status = runHaptics();
    if (out)
        *out = s;
    return status;
}

TEST_F(Daemon, ServesUntilSigtermAndStopsTheMotor)
{
    Script s;
    EXPECT_EQ(runUntil(SIGTERM, &s), 0);
    EXPECT_EQ(s.reply, kOk);
    EXPECT_NE(s.playing, 0) << "vibrate did not play while the loop ran";
    EXPECT_EQ(fakenyx::playing(), 0) << "an infinite vibration outlived the daemon";
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
    EXPECT_EQ(fakenyx::devicesOpen(), 0);
}

TEST_F(Daemon, StopsOnSigint)
{
    EXPECT_EQ(runUntil(SIGINT), 0);
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
    EXPECT_EQ(fakenyx::playing(), 0);
}

// Each start failure: exit status 1, and the partial registration dropped
// and the device closed (TearDown checks no handle, LSError or device is
// left).
class DaemonStartFailure : public HapticsTest, public ::testing::WithParamInterface<const char*> {};

TEST_P(DaemonStartFailure, ExitsOneAndCleansUp)
{
    fakebus::failNext(GetParam());
    EXPECT_EQ(runHaptics(), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0) << "handle left registered after " << GetParam() << " failed";
    EXPECT_EQ(fakenyx::devicesOpen(), 0);
}

INSTANTIATE_TEST_SUITE_P(EveryStep, DaemonStartFailure,
    ::testing::Values("LSRegister", "LSRegisterCategory", "LSCategorySetData",
                      "LSSubscriptionSetCancelFunction", "LSGmainAttach"));

// Nothing is left on the default main context after a run: a signal source
// from an earlier run, still pointing at its freed main loop, would be
// dispatched by the next run's signal.
TEST_F(Daemon, NoSignalSourceOutlivesARun)
{
    EXPECT_EQ(runUntil(SIGTERM), 0);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGINT), 0);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGTERM), 0);
}

TEST_F(Daemon, NoSignalSourceOutlivesAFailedStart)
{
    fakebus::failNext("LSRegister");
    EXPECT_EQ(runHaptics(), 1);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGTERM), 0);
    fakebus::reset();
    EXPECT_EQ(runUntil(SIGINT), 0);
}

TEST_F(Daemon, ContextIsEmptyAfterwards)
{
    EXPECT_EQ(runUntil(SIGTERM), 0);
    // Anything still attached would be pending or dispatchable here.
    EXPECT_FALSE(g_main_context_pending(nullptr));
}

} // namespace
