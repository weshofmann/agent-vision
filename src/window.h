#pragma once
#include <tvterm/termwnd.h>
#include <tvterm/termctrl.h>
#include <tvterm/consts.h>
#include <string>

class TerminalWindow final : public tvterm::BasicTerminalWindow {
    tvterm::TerminalController &controller;
    const char label;
    bool completed {false};
    std::string caption;
    void finish();
public:
    static const tvterm::TVTermConstants appConsts;
    TerminalWindow(const TRect &, tvterm::TerminalController &, char label) noexcept;
    bool isLive() const noexcept;
    const char *getTitle(short) override;
    void handleEvent(TEvent &) override;
    void close() override;
};
