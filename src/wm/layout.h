#pragma once

#include <QList>
#include <QtGlobal>

#include <memory>

namespace visor::wm {

// Physical pixels, screen coordinates; right and bottom are exclusive.
struct Rect
{
    int left = 0, top = 0, right = 0, bottom = 0;

    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool operator==(const Rect &) const = default;
};

// Hyprland's dwindle layout: a binary tree whose leaves are windows. A new
// window splits the focused one; the split is side by side when the space is
// wider than tall (scaled by split_width_multiplier), otherwise one above the
// other, so the screen fills in a spiral. Closing a window gives its space to its sibling.
class DwindleLayout
{
public:
    using Window = quintptr;

    struct Options
    {
        // dwindle:default_split_ratio. The first child gets ratio/2 of the
        // space: 1.0 is an even split, the range is 0.1..1.9.
        double splitRatio = 1.0;
        // dwindle:preserve_split. Keep each split's direction as chosen when
        // it was made, instead of re-deriving it from the shape on every
        // layout.
        bool preserveSplit = true;
        // dwindle:force_split. 0: the new window goes on the side of the
        // split the cursor is on; 1: always left/top; 2: always right/bottom.
        int forceSplit = 2;
        // dwindle:split_width_multiplier. A split is side by side when
        // width * this > height, as in Hyprland; below 1.0 a space has to
        // be wider before it splits side by side.
        double splitWidthMultiplier = 1.0;

        bool sideBySide(const Rect &box) const { return box.width() * splitWidthMultiplier > box.height(); }
    };

    struct Placement
    {
        Window window;
        Rect rect; // with gaps applied
    };

    DwindleLayout();
    ~DwindleLayout();
    DwindleLayout(const DwindleLayout &) = delete;
    DwindleLayout &operator=(const DwindleLayout &) = delete;

    bool isEmpty() const { return !m_root; }
    bool contains(Window window) const;
    QList<Window> windows() const; // left/top first

    // Adds `window` by splitting `target` (when 0 or not in the layout, the
    // most recently added window). `area` is the space the layout fills,
    // needed to choose the split direction; `cursorX/Y` matter only for
    // force_split 0.
    void insert(Window window, Window target, const Rect &area, const Options &options, int cursorX = 0,
                int cursorY = 0);
    void remove(Window window);

    // Swaps two windows' places (both in this layout).
    void swap(Window a, Window b);
    // Puts `with` where `window` is (for swapping across layouts).
    void replace(Window window, Window with);
    // Flips the direction of the split `window` belongs to.
    void toggleSplit(Window window);
    // Grows (or with negative values shrinks) `window` by dx/dy pixels by
    // moving the nearest side-by-side (dx) / stacked (dy) split it is part
    // of, as Hyprland's resizeactive does. Uses the sizes from the last
    // arrange(). Returns false when there is no such split.
    bool resize(Window window, int dx, int dy);

    // Where every window goes in `area`. gapsOut is kept from the edges of
    // the area, and 2 * gapsIn between neighbouring windows (Hyprland's
    // general:gaps_out and gaps_in).
    QList<Placement> arrange(const Rect &area, int gapsIn, int gapsOut, const Options &options);

private:
    struct Node;

    Node *find(Node *node, Window window) const;
    Node *lastLeaf() const;
    void computeBoxes(Node *node, const Rect &box, const Options &options);

    std::unique_ptr<Node> m_root;
};

} // namespace visor::wm
