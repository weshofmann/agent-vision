#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct WindowListRow {
    std::string id;
    std::string caption;
};

class WindowListModel {
    std::vector<WindowListRow> items;
    size_t selected {0};
public:
    void replace(std::vector<WindowListRow> rows);
    bool select(size_t index) noexcept;
    size_t size() const noexcept { return items.size(); }
    size_t selectedIndex() const noexcept { return selected; }
    std::string selectedId() const;
    const std::vector<WindowListRow> &rows() const noexcept { return items; }
};
