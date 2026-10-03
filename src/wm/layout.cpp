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
    split->sideBySide = options.sideBySide(split->box);
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

void DwindleLayout::swap(Window a, Window b)
{
    Node *na = find(m_root.get(), a);
    Node *nb = find(m_root.get(), b);
    if (na && nb)
        std::swap(na->window, nb->window);
}

void DwindleLayout::move(Window window, Window target, const Rect &area, const Options &options, int x, int y)
{
    Node *leaf = find(m_root.get(), window);
    Node *other = find(m_root.get(), target);
    if (!leaf || !other || leaf == other)
        return;
    if (leaf->parent == other->parent) {
        std::swap(leaf->window, other->window);
        return;
    }
    remove(window);
    // The point decides the side, as the cursor does for force_split 0.
    Options atPoint = options;
    atPoint.forceSplit = 0;
    insert(window, target, area, atPoint, x, y);
}

bool DwindleLayout::moveToEnd(Window window, bool vertical, const Rect &area, const Options &options, int x, int y)
{
    Node *leaf = find(m_root.get(), window);
    if (!leaf || !leaf->parent)
        return false;
    // Moving up or down needs a side-by-side split, whose other side has
    // rows to move into (a single window there would get the same split back).
    Node *parent = leaf->parent;
    const Node *sibling = parent->first.get() == leaf ? parent->second.get() : parent->first.get();
    if (parent->sideBySide != vertical || sibling->isLeaf())
        return false;

    remove(window);
    computeBoxes(m_root.get(), area, options);
    Node *target = leafAt(m_root.get(), x, y);
    Options atPoint = options;
    atPoint.forceSplit = 0;
    insert(window, target ? target->window : 0, area, atPoint, x, y);
    return true;
}

bool DwindleLayout::breakOut(Window window, bool vertical, bool forward)
{
    // Moving down: the window's row is a side-by-side split, the top half of
    // a stacked one.
    Node *leaf = find(m_root.get(), window);
    Node *row = leaf ? leaf->parent : nullptr;
    Node *rows = row ? row->parent : nullptr;
    if (!rows || row->sideBySide != vertical || rows->sideBySide == vertical
        || (rows->first.get() == row) != forward)
        return false;

    const bool leafFirst = row->first.get() == leaf;
    const int size = vertical ? leaf->box.width() : leaf->box.height();
    const int total = vertical ? rows->box.width() : rows->box.height();
    remove(window); // `row` goes, its other window takes its place in `rows`

    // A new split in `rows`' place: the window on one side, the rows on the other.
    auto split = std::make_unique<Node>();
    split->sideBySide = vertical;
    const double share = total > 0 ? double(leafFirst ? size : total - size) / total : 0.5;
    split->ratio = std::clamp(2 * share, 0.1, 1.9);
    auto moved = std::make_unique<Node>();
    moved->window = window;
    moved->parent = split.get();

    Node *above = rows->parent;
    std::unique_ptr<Node> &slot = !above ? m_root : above->first.get() == rows ? above->first : above->second;
    std::unique_ptr<Node> old = std::move(slot);
    old->parent = split.get();
    split->parent = above;
    if (leafFirst) {
        split->first = std::move(moved);
        split->second = std::move(old);
    } else {
        split->first = std::move(old);
        split->second = std::move(moved);
    }
    slot = std::move(split);
    return true;
}

DwindleLayout::Node *DwindleLayout::leafAt(Node *node, int x, int y) const
{
    if (!node || x < node->box.left || x >= node->box.right || y < node->box.top || y >= node->box.bottom)
        return nullptr;
    if (node->isLeaf())
        return node;
    if (Node *found = leafAt(node->first.get(), x, y))
        return found;
    return leafAt(node->second.get(), x, y);
}

void DwindleLayout::replace(Window window, Window with)
{
    if (Node *node = find(m_root.get(), window))
        node->window = with;
}

void DwindleLayout::toggleSplit(Window window)
{
    Node *leaf = find(m_root.get(), window);
    if (leaf && leaf->parent)
        leaf->parent->sideBySide = !leaf->parent->sideBySide;
}

bool DwindleLayout::resize(Window window, int dx, int dy)
{
    Node *leaf = find(m_root.get(), window);
    if (!leaf)
        return false;

    bool changed = false;
    const auto adjust = [&](bool sideBySide, int delta) {
        if (!delta)
            return;
        // The nearest split in that direction: moving its divider changes
        // this window's size.
        Node *child = leaf;
        Node *split = leaf->parent;
        while (split && split->sideBySide != sideBySide) {
            child = split;
            split = split->parent;
        }
        if (!split)
            return;
        const int total = sideBySide ? split->box.width() : split->box.height();
        if (total <= 0)
            return;
        // From where the divider is, which a minimum size may have moved
        // off the ratio.
        const Rect &firstBox = split->first->box;
        const double firstSize = sideBySide ? firstBox.width() : firstBox.height();
        const bool inFirst = split->first.get() == child;
        const double newFirst = firstSize + (inFirst ? delta : -delta);
        split->ratio = std::clamp(2 * newFirst / total, 0.1, 1.9);
        changed = true;
    };
    adjust(true, dx);
    adjust(false, dy);
    return changed;
}

void DwindleLayout::computeBoxes(Node *node, const Rect &box, const Options &options)
{
    if (!node)
        return;
    node->box = box;
    if (node->isLeaf())
        return;
    if (!options.preserveSplit)
        node->sideBySide = options.sideBySide(box);

    const double ratio = std::clamp(node->ratio, 0.1, 1.9);
    const int total = node->sideBySide ? box.width() : box.height();
    int firstSize = int(std::lround(total * ratio / 2));
    // Move the divider so neither side is below its minimum, when both fit.
    const QSize minFirst = minimumOf(node->first.get());
    const QSize minSecond = minimumOf(node->second.get());
    const int needFirst = node->sideBySide ? minFirst.width() : minFirst.height();
    const int needSecond = node->sideBySide ? minSecond.width() : minSecond.height();
    if (needFirst + needSecond <= total)
        firstSize = std::clamp(firstSize, needFirst, total - needSecond);

    Rect a = box;
    Rect b = box;
    if (node->sideBySide) {
        a.right = b.left = box.left + firstSize;
    } else {
        a.bottom = b.top = box.top + firstSize;
    }
    computeBoxes(node->first.get(), a, options);
    computeBoxes(node->second.get(), b, options);
}

// The smallest box (gaps included) `node` fits in: a window's minimum frame
// plus its gaps; a split needs both children's along its direction and the
// larger of the two across it.
QSize DwindleLayout::minimumOf(const Node *node) const
{
    if (!node)
        return {0, 0};
    if (node->isLeaf()) {
        const QSize m = m_minimums.value(node->window, QSize(0, 0));
        return {m.width() > 0 ? m.width() + 2 * m_gapsIn : 0, m.height() > 0 ? m.height() + 2 * m_gapsIn : 0};
    }
    const QSize a = minimumOf(node->first.get());
    const QSize b = minimumOf(node->second.get());
    if (node->sideBySide)
        return {a.width() + b.width(), std::max(a.height(), b.height())};
    return {std::max(a.width(), b.width()), a.height() + b.height()};
}

QList<DwindleLayout::Placement> DwindleLayout::arrange(const Rect &area, int gapsIn, int gapsOut,
                                                       const Options &options, const QHash<Window, QSize> &minimums)
{
    m_minimums = minimums;
    m_gapsIn = gapsIn;

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
