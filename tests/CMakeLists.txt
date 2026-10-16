# @@@LICENSE
#
#      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

# The daemon's own objects, linked with an in-process luna-service2
# (fakes/fakebus.cpp) and nyx haptics device (fakes/fakenyx.cpp) instead of
# the real libraries.

find_package(GTest REQUIRED)

set(HAPTICS_SANITIZE "" CACHE STRING
    "Sanitizers for the test build, as -fsanitize= takes them (address,undefined or thread)")
set(HAPTICS_NO_SANITIZE "" CACHE STRING
    "Checks to leave out of HAPTICS_SANITIZE, as -fno-sanitize= takes them")

add_executable(haptics-tests
    $<TARGET_OBJECTS:haptics-core>
    fakes/fakebus.cpp
    fakes/fakenyx.cpp
    unit/test_vibrate.cpp
    unit/test_named.cpp
    unit/test_subscriptions.cpp
    unit/test_lifecycle.cpp
    unit/test_daemon.cpp)

target_include_directories(haptics-tests PRIVATE fakes unit)
target_compile_definitions(haptics-tests PRIVATE HAPTICS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(haptics-tests PRIVATE -Wextra)

# No luna-service2 or nyx here: the fakes are the only ones.
target_link_libraries(haptics-tests
    GTest::gtest_main
    GTest::gtest
    ${GLIB2_LDFLAGS}
    ${JSON_LDFLAGS}
    pthread)

if (HAPTICS_SANITIZE)
    set(HAPTICS_SANITIZE_LINK -fsanitize=${HAPTICS_SANITIZE})
    if (HAPTICS_NO_SANITIZE)
        list(APPEND HAPTICS_SANITIZE_LINK -fno-sanitize=${HAPTICS_NO_SANITIZE})
    endif()
    foreach(t haptics-core haptics-tests)
        target_compile_options(${t} PRIVATE ${HAPTICS_SANITIZE_LINK} -fno-omit-frame-pointer
                               -fno-sanitize-recover=all)
    endforeach()
    target_link_options(haptics-tests PRIVATE ${HAPTICS_SANITIZE_LINK})
endif()

add_test(NAME haptics-tests COMMAND haptics-tests)
