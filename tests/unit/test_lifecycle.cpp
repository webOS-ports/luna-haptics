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

#include <set>
#include <vector>

namespace {

typedef HapticsTest Lifecycle;

const char* const kName = "com.palm.vibrate";
const std::vector<std::string> kMethods = { "vibrate", "vibrateNamedEffect" };

TEST_F(Lifecycle, RegistersBothMethodsAtTheRoot)
{
    start();
    EXPECT_EQ(fakebus::registeredName(), kName);
    ASSERT_EQ(fakebus::categories().count("/"), 1u);
    EXPECT_EQ(fakebus::categories().at("/"), kMethods);
    EXPECT_EQ(fakebus::calls("LSSubscriptionSetCancelFunction"), 1);
    EXPECT_EQ(fakebus::calls("LSGmainAttach"), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 1);
    EXPECT_EQ(fakenyx::devicesOpen(), 1);
}

TEST_F(Lifecycle, InstanceIsOne)
{
    EXPECT_EQ(HapticsController::instance(), HapticsController::instance());
}

TEST_F(Lifecycle, DestroyUnregistersAndClosesOnce)
{
    start();
    HapticsController::destroy();
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
    EXPECT_EQ(fakenyx::calls("nyx_device_close"), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
    EXPECT_EQ(fakenyx::devicesOpen(), 0);
    HapticsController::destroy();     // and again, as TearDown does
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
    EXPECT_EQ(fakenyx::calls("nyx_device_close"), 1);
}

TEST_F(Lifecycle, DestroyStopsTheMotorBeforeClosing)
{
    start();
    ASSERT_TRUE(fakebus::callMethod("vibrate", "{\"period\":200,\"duration\":60000}"));
    ASSERT_NE(fakenyx::playing(), 0);
    HapticsController::destroy();
    EXPECT_EQ(fakenyx::playing(), 0);
}

TEST_F(Lifecycle, DestroyWithoutStartIsHarmless)
{
    HapticsController::instance();
    HapticsController::destroy();
    EXPECT_EQ(fakebus::calls("LSUnregister"), 0);
    EXPECT_EQ(fakenyx::devicesOpen(), 0);
}

TEST_F(Lifecycle, CanStartAgainAfterDestroy)
{
    start();
    HapticsController::destroy();
    start();
    EXPECT_EQ(fakebus::calls("LSRegister"), 2);
    EXPECT_EQ(fakenyx::devicesOpen(), 1);
}

TEST_F(Lifecycle, NoDeviceStillServes)
{
    fakenyx::failNext("nyx_device_open");
    start();
    EXPECT_EQ(fakenyx::devicesOpen(), 0);
    EXPECT_EQ(call("vibrateNamedEffect", "{\"name\":\"alert\"}"), kFailed);
    HapticsController::destroy();
    EXPECT_EQ(fakenyx::calls("nyx_device_close"), 0);
}

// Each step of startService() failing: it says so, the error is freed
// (TearDown), and a handle already obtained is unregistered by destroy(),
// as runHaptics() calls it on that path.
class StartFailure : public HapticsTest, public ::testing::WithParamInterface<const char*> {};

TEST_P(StartFailure, ReportedAndCleanedUp)
{
    fakebus::failNext(GetParam());
    EXPECT_FALSE(HapticsController::instance()->startService(m_loop));
    EXPECT_EQ(fakebus::errorsOutstanding(), 0) << "LSError from " << GetParam() << " not freed";
    HapticsController::destroy();
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
}

INSTANTIATE_TEST_SUITE_P(EveryStep, StartFailure,
    ::testing::Values("LSRegister", "LSRegisterCategory", "LSCategorySetData",
                      "LSSubscriptionSetCancelFunction", "LSGmainAttach"));

TEST_F(Lifecycle, FailedUnregisterFreesItsError)
{
    start();
    fakebus::failNext("LSUnregister");
    HapticsController::destroy();
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);
}

TEST_F(Lifecycle, FailedCloseIsOnlyLogged)
{
    start();
    fakenyx::failNext("nyx_device_close");
    HapticsController::destroy();
    EXPECT_EQ(fakenyx::devicesOpen(), 0);
}

// --- the installed files agree with the code ---------------------------------

// The source tree: where the build put it, or $HAPTICS_SOURCE_DIR when the
// tests run somewhere else (on a device, with files/ copied next to them).
std::string sourcePath(const char* relative)
{
    const char* dir = g_getenv("HAPTICS_SOURCE_DIR");
    return std::string(dir ? dir : HAPTICS_SOURCE_DIR) + "/" + relative;
}

std::string text(const char* relative)
{
    std::string path = sourcePath(relative);
    char* t = nullptr;
    EXPECT_TRUE(g_file_get_contents(path.c_str(), &t, nullptr, nullptr)) << path;
    std::string s = t ? t : "";
    g_free(t);
    return s;
}

JsonPtr readJson(const char* relative)
{
    JsonPtr j = parse(text(relative));
    EXPECT_NE(j, nullptr) << relative << " is not JSON";
    return j;
}

std::set<std::string> strings(json_object* array)
{
    std::set<std::string> s;
    for (size_t i = 0; array && i < json_object_array_length(array); ++i)
        s.insert(json_object_get_string(json_object_array_get_idx(array, i)));
    return s;
}

TEST_F(Lifecycle, ApiFileGroupsCoverExactlyTheRegisteredMethods)
{
    start();
    JsonPtr api = readJson("files/sysbus/luna-haptics.api.json");
    ASSERT_NE(api, nullptr);
    std::set<std::string> registered;
    for (const std::string& m : fakebus::categories().at("/"))
        registered.insert(fakebus::registeredName() + "/" + m);
    json_object_object_foreach(api.get(), group, entries) {
        ASSERT_TRUE(json_object_is_type(entries, json_type_array)) << group;
        EXPECT_EQ(strings(entries), registered) << group;
    }
}

TEST_F(Lifecycle, BusFilesNameTheRegisteredService)
{
    start();
    const std::string name = fakebus::registeredName();
    JsonPtr role = readJson("files/sysbus/luna-haptics.role.json.in");
    ASSERT_NE(role, nullptr);
    EXPECT_EQ(strings(member(role.get(), "allowedNames", json_type_array)), std::set<std::string>{ name });
    json_object* exe = member(role.get(), "exeName", json_type_string);
    ASSERT_NE(exe, nullptr);
    EXPECT_EQ(std::string(json_object_get_string(exe)), "@WEBOS_INSTALL_SBINDIR@/LunaHaptics");

    std::string service = text("files/sysbus/luna-haptics.service.in");
    EXPECT_NE(service.find("Name=" + name + "\n"), std::string::npos);
    EXPECT_NE(service.find("Exec=@WEBOS_INSTALL_SBINDIR@/LunaHaptics\n"), std::string::npos);
    EXPECT_NE(text("files/systemd/luna-haptics.service").find("ExecStart=/usr/sbin/LunaHaptics\n"),
              std::string::npos);

    // Every group the API file declares has a trust level in groups.json,
    // which may only name this service, and the service holds its own group.
    JsonPtr api = readJson("files/sysbus/luna-haptics.api.json");
    JsonPtr groups = readJson("files/sysbus/luna-haptics.groups.json");
    JsonPtr perm = readJson("files/sysbus/luna-haptics.perm.json");
    ASSERT_TRUE(api && groups && perm);
    json_object_object_foreach(api.get(), group, entries) {
        (void) entries;
        EXPECT_NE(member(groups.get(), group, json_type_array), nullptr) << group << " has no trust level";
    }
    EXPECT_EQ(strings(member(groups.get(), "allowedNames", json_type_array)), std::set<std::string>{ name });
    EXPECT_EQ(strings(member(perm.get(), name.c_str(), json_type_array)), std::set<std::string>{ "vibrate.operation" });
}

} // namespace
