#pragma once
#define Uses_TApplication
#include <tvision/tv.h>
class AgentVisionApp final : public TApplication {
    void openMenu();
    bool addTerminal(const TRect &, char label);
public:
    bool ready {false};
    AgentVisionApp();
    static TStatusLine *initStatusLine(TRect);
    static TDeskTop *initDeskTop(TRect);
    void handleEvent(TEvent &) override;
    void idle() override;
    Boolean valid(ushort) override;
};
