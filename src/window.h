#pragma once
#include "core_connection.h"
#include <tvterm/termwnd.h>
#include <tvterm/termctrl.h>
#include <tvterm/consts.h>
#include <string>

class TerminalWindow final : public tvterm::BasicTerminalWindow {
    tvterm::TerminalController &controller;
    std::shared_ptr<agentvision::SessionEndpoint> endpoint;
    const char label;
    agentvision::RequestId closing {0};
    bool closeCompleted {false}, finished {false}, resizeFailed {false};
    std::string caption;
    void finish();
public:
    static const tvterm::TVTermConstants appConsts;
    TerminalWindow(const TRect &, tvterm::TerminalController &,
                   std::shared_ptr<agentvision::SessionEndpoint>, char label) noexcept;
    static std::string captionFor(char, const agentvision::SessionMetadata &, bool closing = false,
                                  bool resizeFailed = false);
    bool isLive() const noexcept;
    const char *getTitle(short) override;
    void handleEvent(TEvent &) override;
    void close() override;
};
