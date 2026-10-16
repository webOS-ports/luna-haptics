/* @@@LICENSE
*
*      Copyright (c) 2008-2013 LG Electronics, Inc.
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

#ifndef __HAPTICS_CONTROLLER_H__
#define __HAPTICS_CONTROLLER_H__

#include <glib.h>
#include <luna-service2/lunaservice.h>

#include <string>
#include <vector>

/**
 * Serves com.palm.vibrate: vibrate (period/duration) and vibrateNamedEffect.
 *
 * An infinite vibration (duration 0, or continous:true) is tied to the
 * caller's subscription: LSCallCancel from the client, or the client going
 * away, cancels the effect (see endSubscription()).
 *
 * Split out of luna-sysmgr when that component was dissolved into
 * per-function daemons. The backend below this interface is nyx
 * (NYX_DEVICE_HAPTICS); see NyxHapticsController.
 */
class HapticsController
{
public:
    static HapticsController *instance();

    //! Unregisters from the bus (a partial registration too) and deletes
    //! the instance; the next instance() makes a new one.
    static void destroy();

    virtual ~HapticsController();

    virtual int vibrate(int /*period*/, int /*duration*/) { return -1; }
    virtual int vibrate(const char * /*name*/) { return -1; }
    virtual int cancel(int /*id*/) { return -1; }
    virtual void cancelAll() {}

    //! Ties effect id to the subscription with this token.
    void addSubscription(const char *token, int id);

    /**
     * Ends the subscription with this token, and cancels its effect - unless
     * a subscription added after it is still open. The nyx modules play one
     * effect at a time, a new one replacing the last, and cancel whatever
     * is playing whatever id they are given; cancelling for an older
     * subscription would stop the newer one's vibration.
     */
    void endSubscription(const char *token);

    //! Subscriptions still open.
    size_t subscriptionCount() const { return m_subscriptions.size(); }

    bool startService(GMainLoop* mainLoop);

protected:
    HapticsController();
    LSHandle *m_service;

private:
    struct Subscription {
        std::string token;
        int id;
    };

    static HapticsController *s_instance;
    // Oldest first. The tokens are copies: a token string belongs to its
    // message, and an entry has to outlive the message that created it.
    std::vector<Subscription> m_subscriptions;
};

#endif
