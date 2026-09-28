#include "window_list.h"
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool condition, const char *why) {
    if (!condition) throw std::runtime_error(why);
}

int main() {
    WindowListModel model;
    model.replace({});
    check(model.size() == 0 && model.selectedId().empty(), "empty desktop has no selection");
    model.replace({{"A", "Alpha [live]"}});
    check(model.size() == 1 && model.selectedId() == "A", "one retained view is selectable");
    std::vector<WindowListRow> sixteen;
    for (int i = 0; i < 16; ++i)
        sixteen.push_back({std::to_string(i), "Terminal " + std::to_string(i) + " [live]"});
    model.replace(sixteen);
    check(model.size() == 16 && model.select(15) && model.selectedId() == "15",
          "sixteenth view is reachable");
    auto reordered = sixteen;
    std::swap(reordered[0], reordered[15]);
    model.replace(reordered);
    check(model.selectedId() == "15" && model.selectedIndex() == 0,
          "selection follows stable ID across collection replacement");
    reordered.erase(reordered.begin());
    model.replace(reordered);
    check(model.selectedId() != "15" && model.size() == 15,
          "retired selected target is not returned");
    check(model.select(13) && model.selectedId() == "14", "surviving target is selectable");
    check(!model.select(15) && model.selectedId() == "14", "out-of-range selection cannot escape bound");
    return 0;
}
