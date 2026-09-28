#pragma once
#include "core_connection.h"
#include <tvterm/termwnd.h>
#include <tvterm/termctrl.h>
#include <tvterm/consts.h>
#include <string>

class TerminalWindow final : public tvterm::BasicTerminalWindow {
    tvterm::TerminalController &controller;
    std::shared_ptr<agentvision::SessionEndpoint> endpoint;
    const std::string label;
    std::string displayTitle;
    agentvision::RequestId closing {0};
    bool closeCompleted {false}, finished {false}, resizeFailed {false}, authorityLost {false};
    unsigned eventDepth {0};
    std::string caption;
    void finish();
public:
    static const tvterm::TVTermConstants appConsts;
    TerminalWindow(const TRect &, tvterm::TerminalController &,
                   std::shared_ptr<agentvision::SessionEndpoint>, std::string label) noexcept;
    static std::string captionFor(const std::string &, const agentvision::SessionMetadata &, bool closing = false,
                                  bool resizeFailed = false, bool authorityLost = false,
                                  size_t maxCells = 0);
    static std::string formatCaption(const std::string &, const agentvision::SessionMetadata &,
                                     bool closing = false, bool resizeFailed = false,
                                     bool authorityLost = false, size_t maxCells = 0);
    static std::string captionFor(char label, const agentvision::SessionMetadata &metadata,
                                  bool closing = false, bool resizeFailed = false,
                                  bool authorityLost = false, size_t maxCells = 0) {
        return captionFor(std::string(1, label), metadata, closing, resizeFailed, authorityLost, maxCells);
    }
    static bool validDisplayTitle(const std::string &) noexcept;
    bool setDisplayTitle(const std::string &);
    const std::string &viewId() const noexcept { return label; }
    const std::string &title() const noexcept { return displayTitle; }
    std::string displayCaption(size_t maxCells = 0) const;
    static std::string labelFor(uint64_t localId);
    bool ownsSession(agentvision::SessionId id) const noexcept;
    bool isLive() const noexcept;
    const char *getTitle(short) override;
    void handleEvent(TEvent &) override;
    void close() override;
};
