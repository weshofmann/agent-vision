#pragma once
#define Uses_TApplication
#include <tvision/tv.h>
#include "core_connection.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

class AgentVisionApp final : public TApplication {
    std::shared_ptr<agentvision::CoreConnection> connection;
    const std::thread::id uiThread {std::this_thread::get_id()};
    std::mutex cleanupMutex;
    std::condition_variable cleanupChanged;
    bool cleanupRequested {false}, cleanupAcknowledged {false};
    bool suspended {false}, stopped {false}, cleanupTargetMiss {false};
    std::string startupFailure, cleanupFailure;
    void openMenu();
    bool addTerminal(const TRect &, char label);
    bool await(agentvision::ConnectionEvent::Kind, agentvision::ConnectionEvent &,
               agentvision::RequestId = 0);
    void localStopped();
    void serviceCleanup();
    void stopPresentations();
    void acknowledgeCleanup();
public:
    bool ready {false};
    AgentVisionApp();
    bool cleanupTargetMissed() const noexcept { return cleanupTargetMiss; }
    const std::string &cleanupDiagnostic() const noexcept { return cleanupFailure; }
    const std::string &failure() const noexcept { return startupFailure; }
    static TStatusLine *initStatusLine(TRect);
    static TDeskTop *initDeskTop(TRect);
    void getEvent(TEvent &) override;
    void handleEvent(TEvent &) override;
    void idle() override;
    void shutDown() override;
    Boolean valid(ushort) override;
};
