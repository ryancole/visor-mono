#pragma once

#include <QHash>
#include <QList>
#include <QSize>
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
//
// Unlike Hyprland's, a split won't squeeze a window below its minimum size
// (Windows apps refuse to go smaller, and would overlap their neighbours):
// as Snap's divider stops at a snapped window's minimum, each split moves
// as far as it must for both sides to fit, and the splits above it make
// room for that. Only when the area can't hold them all does a split stay
// where its ratio puts it.
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
    // Hyprland's movewindow: takes `window` out of its place (its sibling
    // grows into it) and splits `target` with it, on the side of `target`
    // that (x, y) is on (the point just past the edge it moved across).
    // Next to its own sibling that changes nothing, so the two swap. Both
    // must be in this layout.
    void move(Window window, Window target, const Rect &area, const Options &options, int x, int y);
    // movewindow with nothing beyond `window` that way, as Snap's Win+Up
    // turns a right half into the top-right quarter: when its sibling is
    // split into rows (vertical) or columns across it, the window moves into
    // the row at that end, beside the window at (x, y) (a point just inside
    // its own edge), and the rest of that row's neighbours grow into its
    // place. Returns false, changing nothing, when there's no such split.
    bool moveToEnd(Window window, bool vertical, const Rect &area, const Options &options, int x, int y);
    // The reverse of moveToEnd, which movewindow tries first: a window in a
    // row (vertical) or column that moves across the split below (forward:
    // down/right) or above it leaves its row and becomes a full-height column
    // (or full-width row) beside the rows, on its own side and as wide as it
    // was, so a second press goes on into the row beyond. Returns false,
    // changing nothing, when the window isn't in such a row.
    bool breakOut(Window window, bool vertical, bool forward);
    // Puts `with` where `window` is (for swapping across layouts).
    void replace(Window window, Window with);
    // Flips the direction of the split `window` belongs to.
    void toggleSplit(Window window);
    // Grows (or with negative values shrinks) `window` by dx/dy pixels by
    // moving the nearest side-by-side (dx) / stacked (dy) split it is part
    // of, as Hyprland's resizeactive does. Uses the sizes from the last
    // arrange(), and minimum sizes still bound the split. Returns false when
    // there is no such split.
    bool resize(Window window, int dx, int dy);

    // Where every window goes in `area`. gapsOut is kept from the edges of
    // the area, and 2 * gapsIn between neighbouring windows (Hyprland's
    // general:gaps_out and gaps_in). `minimums` are the windows' smallest
    // frame sizes (0 for a side not known); they're kept for later
    // insert()s too.
    QList<Placement> arrange(const Rect &area, int gapsIn, int gapsOut, const Options &options,
                             const QHash<Window, QSize> &minimums = {});

private:
    struct Node;

    Node *find(Node *node, Window window) const;
    Node *lastLeaf() const;
    Node *leafAt(Node *node, int x, int y) const; // by the boxes from the last layout
    void computeBoxes(Node *node, const Rect &box, const Options &options);
    QSize minimumOf(const Node *node) const;

    std::unique_ptr<Node> m_root;
    QHash<Window, QSize> m_minimums; // from the last arrange()
    int m_gapsIn = 0;
};

} // namespace visor::wm
