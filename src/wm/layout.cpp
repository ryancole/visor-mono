#include "wm/layout.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace visor::wm {

struct DwindleLayout::Node
{
    Node *parent = nullptr;
    std::unique_ptr<Node> first;  // left or top
    std::unique_ptr<Node> second; // right or bottom
    Window window = 0;            // set on leaves only
    bool sideBySide = true;       // split: children left|right, else top|bottom
    double ratio = 1.0;
    Rect box;                     // from the last layout, before gaps

    bool isLeaf() const { return !first; }
};

DwindleLayout::DwindleLayout() = default;
DwindleLayout::~DwindleLayout() = default;

bool DwindleLayout::contains(Window window) const
{
    return find(m_root.get(), window) != nullptr;
}

QList<DwindleLayout::Window> DwindleLayout::windows() const
{
    QList<Window> list;
    const auto collect = [&list](const auto &self, const Node *node) -> void {
        if (!node)
            return;
        if (node->isLeaf()) {
            list.append(node->window);
            return;
        }
        self(self, node->first.get());
        self(self, node->second.get());
    };
    collect(collect, m_root.get());
    return list;
}

DwindleLayout::Node *DwindleLayout::find(Node *node, Window window) const
{
    if (!node)
        return nullptr;
    if (node->isLeaf())
        return node->window == window ? node : nullptr;
    if (Node *found = find(node->first.get(), window))
        return found;
    return find(node->second.get(), window);
}

// With force_split 2 every new window lands right/bottom of the split, so
// the last leaf is the most recently added one.
DwindleLayout::Node *DwindleLayout::lastLeaf() const
{
    Node *node = m_root.get();
    while (node && !node->isLeaf())
        node = node->second.get();
    return node;
}

void DwindleLayout::insert(Window window, Window target, const Rect &area, const Options &options, int cursorX,
                           int cursorY)
{
    if (!window || contains(window))
        return;

    auto leaf = std::make_unique<Node>();
    leaf->window = window;
    if (!m_root) {
        m_root = std::move(leaf);
        return;
    }

    Node *split = target ? find(m_root.get(), target) : nullptr;
    if (!split)
        split = lastLeaf();

    // Boxes may be stale (or unset, before the first layout).
    computeBoxes(m_root.get(), area, options);

    // The target leaf becomes a split node holding the old window and the
    // new one, so the node keeps its place (and box) in the tree.
    auto old = std::make_unique<Node>();
    old->window = split->window;
    split->window = 0;
    split->sideBySide = split->box.width() > split->box.height();
    split->ratio = options.splitRatio;

    bool newFirst = false;
    if (options.forceSplit == 1) {
        newFirst = true;
    } else if (options.forceSplit == 0) {
        newFirst = split->sideBySide ? cursorX < split->box.left + split->box.width() / 2
                                     : cursorY < split->box.top + split->box.height() / 2;
    }
    old->parent = split;
    leaf->parent = split;
    if (newFirst) {
        split->first = std::move(leaf);
        split->second = std::move(old);
    } else {
        split->first = std::move(old);
        split->second = std::move(leaf);
    }
}

void DwindleLayout::remove(Window window)
{
    Node *leaf = find(m_root.get(), window);
    if (!leaf)
        return;
    Node *parent = leaf->parent;
    if (!parent) {
        m_root.reset();
        return;
    }

    // The sibling takes the parent's place.
    std::unique_ptr<Node> sibling =
        parent->first.get() == leaf ? std::move(parent->second) : std::move(parent->first);
    Node *grandparent = parent->parent;
    sibling->parent = grandparent;
    if (!grandparent)
        m_root = std::move(sibling); // destroys the old root (parent) and leaf
    else if (grandparent->first.get() == parent)
        grandparent->first = std::move(sibling);
    else
        grandparent->second = std::move(sibling);
}

void DwindleLayout::computeBoxes(Node *node, const Rect &box, const Options &options)
{
    if (!node)
        return;
    node->box = box;
    if (node->isLeaf())
        return;
    if (!options.preserveSplit)
        node->sideBySide = box.width() > box.height();

    const double ratio = std::clamp(node->ratio, 0.1, 1.9);
    Rect a = box;
    Rect b = box;
    if (node->sideBySide) {
        const int split = box.left + int(std::lround(box.width() * ratio / 2));
        a.right = split;
        b.left = split;
    } else {
        const int split = box.top + int(std::lround(box.height() * ratio / 2));
        a.bottom = split;
        b.top = split;
    }
    computeBoxes(node->first.get(), a, options);
    computeBoxes(node->second.get(), b, options);
}

QList<DwindleLayout::Placement> DwindleLayout::arrange(const Rect &area, int gapsIn, int gapsOut,
                                                       const Options &options)
{
    // Shrinking the area by (out - in) and then every window by `in` leaves
    // `out` at the edges and 2 * `in` between windows, as Hyprland does.
    const int edge = gapsOut - gapsIn;
    const Rect inner{area.left + edge, area.top + edge, area.right - edge, area.bottom - edge};
    computeBoxes(m_root.get(), inner, options);

    QList<Placement> placements;
    const auto collect = [&](const auto &self, const Node *node) -> void {
        if (!node)
            return;
        if (node->isLeaf()) {
            const Rect &b = node->box;
            placements.append(Placement{node->window, Rect{b.left + gapsIn, b.top + gapsIn, b.right - gapsIn, b.bottom - gapsIn}});
            return;
        }
        self(self, node->first.get());
        self(self, node->second.get());
    };
    collect(collect, m_root.get());
    return placements;
}

} // namespace visor::wm
