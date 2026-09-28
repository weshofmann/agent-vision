#pragma once
#define Uses_TApplication
#include <tvision/tv.h>
#include "core_connection.h"
#include "frontend_cleanup.h"
#include "startup_adoption.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace tvterm { class TerminalController; }

class AgentVisionApp final : public TApplication {
    std::shared_ptr<agentvision::CoreConnection> connection;
    const std::thread::id uiThread {std::this_thread::get_id()};
    std::mutex cleanupMutex;
    std::condition_variable cleanupChanged;
    agentvision::RestorationTarget cleanupTarget;
    agentvision::StartupAdoption startupAdoption;
    bool cleanupRequested {false}, cleanupAcknowledged {false};
    bool suspended {false}, stopped {false}, cleanupTargetMiss {false};
    bool admissionClosed {false}, servicingCoreEvents {false};
    uint64_t nextViewId {2};
    struct PendingCreate { TRect bounds; std::string label; };
    struct PreparedCreate {
        TRect bounds;
        std::string label;
        std::shared_ptr<agentvision::SessionEndpoint> endpoint;
        tvterm::TerminalController *controller;
    };
    std::map<agentvision::RequestId, PendingCreate> pendingCreates;
    std::vector<PreparedCreate> preparedCreates;
    std::string creationFailure;
    std::string startupFailure, cleanupFailure;
    void openMenu();
    bool addTerminal(const TRect &, char label);
    bool await(agentvision::ConnectionEvent::Kind, agentvision::ConnectionEvent &,
               agentvision::RequestId = 0);
    bool startupAllowed();
    void localStopped();
    void serviceCleanup();
    void stopPresentations();
    void acknowledgeCleanup();
    void newTerminal();
    void serviceCoreEvents();
    void adoptPrepared();
    void clearCreates();
    bool mayInsertTerminal();
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
