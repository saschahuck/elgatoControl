/*
 * Copyright (c) 2022, Sascha Huck <sascha@wirrewelt.de>
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "AvahiBrowser.h"
#include "Log.h"
#include "../Config.h"

#include <algorithm>
#include <cassert>
#include <iostream>

#include <avahi-common/error.h>
#include <thread>
#include <future>
#include <regex>

void AvahiBrowser::resolveCallback(AvahiServiceResolver* resolver, [[maybe_unused]] AvahiIfIndex interface,
                                   [[maybe_unused]]AvahiProtocol protocol, AvahiResolverEvent event, const char* name,
                                   const char* type, const char* domain, [[maybe_unused]] const char* hostname,
                                   const AvahiAddress* address, uint16_t port,
                                   [[maybe_unused]] AvahiStringList* txt, [[maybe_unused]] AvahiLookupResultFlags flags,
                                   [[maybe_unused]] void* userdata) {
    assert(resolver);

#if DEBUG_BUILD
    std::clog << kLogDebug << "Resolving device " << name << " at " << hostname << std::endl;
#endif

    switch(event) {
        case AVAHI_RESOLVER_FAILURE:
            std::clog << kLogErr << "(AvahiResolver) Failed to resolve service '" << name << "' of type '" << type << "' in domain '" << domain << "': " << avahi_strerror(
                    avahi_client_errno(avahi_service_resolver_get_client(resolver))) << std::endl;
            break;
        case AVAHI_RESOLVER_FOUND:
            char strAddress[AVAHI_ADDRESS_STR_MAX];
            avahi_address_snprint(strAddress, sizeof(strAddress), address);

            auto discoveredLight = std::make_shared<ElgatoLight>(std::string(name), strAddress, port);
            getInstance().addIfUnknown(discoveredLight);
          break;
    }

    avahi_service_resolver_free(resolver);
}

void AvahiBrowser::browseCallback(AvahiServiceBrowser* browser, AvahiIfIndex interface, AvahiProtocol protocol,
                                  AvahiBrowserEvent event, const char* name, const char* type, const char * domain,
                                  [[maybe_unused]] AvahiLookupResultFlags flags, void* userdata) {
    auto *client = (AvahiClient*) userdata;
    assert(browser);

    switch(event) {
        case AVAHI_BROWSER_FAILURE:
            std::clog << kLogErr << "(AvahiBrowser) " << avahi_strerror(avahi_client_errno(
                    avahi_service_browser_get_client(browser))) << std::endl;
            avahi_simple_poll_quit(getInstance()._simple_poll);
            break;
        case AVAHI_BROWSER_NEW:
            if (!avahi_service_resolver_new(client, interface, protocol, name, type, domain, AVAHI_PROTO_INET, (AvahiLookupFlags) 0, resolveCallback, client))
                std::clog << kLogWarning << "(AvahiBrowser) Failed to resolve service '" << name << "': " << avahi_strerror(
                        avahi_client_errno(client)) << std::endl;
            break;
        case AVAHI_BROWSER_REMOVE:
            getInstance().removeByName(name);
            AvahiBrowser::getInstance().notifyObservers({ AvahiBrowserEventType::LIGHT_REMOVED, name});

            break;
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
        case AVAHI_BROWSER_ALL_FOR_NOW:
            break;
    }
}

void AvahiBrowser::clientCallback([[maybe_unused]] AvahiClient* client, AvahiClientState state, [[maybe_unused]] void* userData) {
    assert(client);

    if (state == AVAHI_CLIENT_FAILURE)
    {
        avahi_simple_poll_quit(getInstance()._simple_poll);
    }
}

void AvahiBrowser::threadStart() {
    std::clog << kLogNotice << "(Avahi) Starting browser..." << std::endl;

    int error;

    // Create the poll loop object
    if (!(getInstance()._simple_poll = avahi_simple_poll_new())) {
        std::clog << kLogErr << "(Avahi) Failed to create simple poll object." << std::endl;
        getInstance().cleanUp();
        return;
    }

    // Create a new client
    getInstance()._client = avahi_client_new(avahi_simple_poll_get(getInstance()._simple_poll), (AvahiClientFlags)0, clientCallback, NULL, &error);
    if (!getInstance()._client) {
        std::clog << kLogErr << "(Avahi) Failed to create client: " << avahi_strerror(avahi_client_errno(getInstance()._client)) << std::endl;
        getInstance().cleanUp();
        return;
    }

    // Create the service browser
    if (!(getInstance()._browser = avahi_service_browser_new(getInstance()._client, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET, "_elg._tcp", NULL, (AvahiLookupFlags)0, browseCallback, getInstance()._client))) {
        std::clog << kLogErr << "(Avahi) Failed to create browser: " << avahi_strerror(avahi_client_errno(getInstance()._client)) << std::endl;
        getInstance().cleanUp();
        return;
    }

    avahi_simple_poll_loop(getInstance()._simple_poll);
}

void AvahiBrowser::addIfUnknown(std::shared_ptr<ElgatoLight>& light) {
    auto item = std::find_if(_lights.begin(), _lights.end(),
                             [light](const std::shared_ptr<ElgatoLight>& item) {
        return item->name() == light->name();
    });

    if (item == _lights.end()) {
        _lights.push_back(light);
        notifyObservers({AvahiBrowserEventType::LIGHT_ADDED, light->name()});
    }
}

void AvahiBrowser::removeByName(std::string name) {
    _lights.erase(
            std::remove_if(_lights.begin(), _lights.end(),
                           [name](const std::shared_ptr<ElgatoLight>& item) {
                return item->name() == name;
            }), _lights.end());
}

std::shared_ptr<ElgatoLight> AvahiBrowser::firstByName(const std::string &regexPattern) {
    auto item = std::find_if(_lights.begin(), _lights.end(), [regexPattern](const auto& item) {
        const std::regex pattern(regexPattern);
        return std::regex_search(item->name(), pattern);
    });

    if (item == _lights.end())
        return nullptr;

    return *item;
}

std::vector<std::shared_ptr<ElgatoLight>> AvahiBrowser::allByName(const std::string &regexPattern) {
    std::vector<std::shared_ptr<ElgatoLight>> target;
    const std::regex pattern(regexPattern == "*" ? "." : regexPattern);

    for(auto& item : _lights){
        if (std::regex_search(item->name(), pattern))
            target.push_back(item);
    }

    return target;
}

void AvahiBrowser::cleanUp() {
    if (_workerThread) {
        pthread_cancel(_workerThread->native_handle());
        _workerThread->join();
        delete _workerThread;
    }

    if (_browser) avahi_service_browser_free(_browser);
    if (_client) avahi_client_free(_client);
    if (_simple_poll) avahi_simple_poll_free(_simple_poll);
}

void AvahiBrowser::start() {
    _workerThread = new std::thread(threadStart);
}

void AvahiBrowser::restart() {
    cleanUp();
    start();
}

void AvahiBrowser::registerCallback(const std::function<void(const AvahiBrowserEventArgs&)>& oberserver) {
    _callbacks.push_back(oberserver);
}

void AvahiBrowser::notifyObservers(const AvahiBrowserEventArgs& args) {
    if (_callbacks.empty()) return;

    std::thread caller([args, this]{
        for(const auto& callback : _callbacks) {
            callback(args);
        }
    });
    caller.detach();
}