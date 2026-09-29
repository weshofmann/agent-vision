#include "window_list.h"
#include <algorithm>
#include <utility>

void WindowListModel::replace(std::vector<WindowListRow> rows)
{
    const auto prior = selectedId();
    const auto priorIndex = selected;
    if (rows.size() > 16) rows.resize(16);
    items = std::move(rows);
    if (items.empty()) { selected = 0; return; }
    auto it = std::find_if(items.begin(), items.end(), [&](const WindowListRow &row) {
        return row.id == prior;
    });
    selected = it != items.end() ? size_t(it - items.begin()) :
        std::min(priorIndex, items.size() - 1);
}
bool WindowListModel::select(size_t index) noexcept
{
    if (index >= items.size()) return false;
    selected = index;
    return true;
}
std::string WindowListModel::selectedId() const
{
    return items.empty() ? std::string() : items[selected].id;
}
