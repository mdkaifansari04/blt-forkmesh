#pragma once

// Geometry and painting for the commits list's git-graph gutter: lane pitch,
// node sizes, the per-lane palette, and the painter that draws one row's lanes.
// Kept out of MainWindowInternal.h so the graph can be rendered (and eyeballed)
// without pulling in the whole window layer — it needs nothing but QtGui.

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QSet>
#include <QVariantList>

#include <algorithm>

namespace forkmesh::ui {

// Lane geometry, shared between the column-width calc and the delegate so the
// dots line up with the section width.
constexpr int kGraphLaneWidth = 12;
constexpr int kGraphMargin = 8;
// Cap on how far a row's text can be pushed right by a very wide graph, so a
// deep merge history can't shove the messages off-screen. It is also the hard
// width of the graph gutter: lanes are compressed (and, past the compression
// floor, clipped) to stay inside it so they never stripe across the messages.
constexpr int kGraphMaxTextIndent = 160;
// Floor for the compressed lane pitch: below this, adjacent lanes stop reading
// as separate lines, so further lanes get clipped by the gutter instead.
constexpr qreal kGraphLaneMinWidth = 5.0;
// Commit node is drawn as a "bullseye": a hollow ring with a filled centre,
// matching the VS Code git-graph look. Slightly larger than before so the
// nodes read as clear anchors; lane lines stop at the ring's edge on merge
// rows so the background shows through the ring/centre-dot gap.
constexpr qreal kGraphNodeOuter = 4.5; // outer ring radius
constexpr qreal kGraphNodeInner = 2.0; // centre-dot radius

// Property carrying the widest lane count in the commits list, set by the
// loader on the table and read back by the delegate. The whole list has to
// share one lane pitch, otherwise a row with few lanes would draw them at a
// different spacing than the row above and the graph would zig-zag.
const char kGraphLaneCountProperty[] = "forkmeshGraphLaneCount";

// Geometry for one commits list, derived from how many lanes its deepest row
// needs. A repo with dozens of concurrent agent branches would otherwise draw
// lanes at the full 12px pitch far past kGraphMaxTextIndent — straight through
// the commit messages, which are indented only up to the cap (adhoc #60). The
// pitch (and with it the node and stroke sizes) shrinks so the graph fits.
struct CommitGraphMetrics
{
    qreal laneWidth = kGraphLaneWidth;
    qreal nodeOuter = kGraphNodeOuter;
    qreal nodeInner = kGraphNodeInner;
    qreal penWidth = 2.0;
};

inline CommitGraphMetrics commitGraphMetrics(int laneCount)
{
    CommitGraphMetrics m;
    const qreal avail = kGraphMaxTextIndent - kGraphMargin;
    if (laneCount > 0 && laneCount * m.laneWidth > avail)
        m.laneWidth = std::max(kGraphLaneMinWidth, avail / laneCount);
    // Dots and strokes are sized off the pitch so a compressed graph keeps the
    // same proportions instead of turning into overlapping blobs.
    m.nodeOuter = std::max(2.0, std::min(kGraphNodeOuter, m.laneWidth * 0.375));
    m.nodeInner = std::max(1.0, m.nodeOuter * 0.45);
    m.penWidth = std::max(1.0, std::min(2.0, m.laneWidth / 6.0));
    return m;
}

// Left edge of a row's text: just past the rightmost lane the row itself uses,
// never past the gutter cap.
inline int commitGraphTextIndent(int rowMaxLane, const CommitGraphMetrics &m)
{
    return std::min<int>(kGraphMargin + qRound((rowMaxLane + 1) * m.laneWidth),
                         kGraphMaxTextIndent);
}

// Stable per-lane colour so a branch keeps its hue down the whole graph.
// Blue leads so the trunk lane (main) draws blue, like the VS Code graph.
inline QColor commitGraphLaneColor(int lane)
{
    static const QColor palette[] = {
        QColor("#58a6ff"), QColor("#d29922"), QColor("#db61a2"),
        QColor("#bc8cff"), QColor("#39c5cf"), QColor("#3fb950"),
    };
    constexpr int n = int(sizeof(palette) / sizeof(palette[0]));
    return palette[((lane % n) + n) % n];
}

// Paints the git-graph gutter the way the VS Code git-graph view does: lanes
// that pass straight through a row are drawn as vertical lines, while a lane
// that merges into the commit (or branches out of it) loops through a rounded
// quarter-circle corner — a horizontal run along the node's centreline joined
// to a vertical run in its own lane. Merge commits draw as a bullseye (hollow
// ring with a filled centre), regular commits as a solid dot. Each row carries
// the lanes present at its top and bottom edges; comparing the two boundaries
// tells us which lanes pass through, merge in, or branch out. Topology is
// meaningful only while the list is in git-log order, which is why that
// ordering is pinned when the list loads. Called by CommitSummaryDelegate
// inside the summary cell (the standalone gutter column is hidden) so each
// row's text can start right beside its own rightmost lane.
inline void paintCommitGraphGutter(QPainter *painter, const QRect &r,
                                   const QVariantList &topLanes,
                                   const QVariantList &botLanes, int nodeLane,
                                   bool isMerge, bool isOutgoing,
                                   const CommitGraphMetrics &metrics)
{
    if (topLanes.isEmpty() && botLanes.isEmpty() && nodeLane < 0)
        return;
    const qreal yTop = r.top();
    const qreal yBot = r.top() + r.height(); // meets the next row's top edge
    const qreal yMid = r.center().y() + 0.5;
    auto laneX = [&](int lane) -> qreal {
        return r.left() + kGraphMargin + lane * metrics.laneWidth;
    };
    // Which lane columns are occupied at each edge of the row.
    QSet<int> topSet;
    QSet<int> botSet;
    int maxLane = nodeLane;
    for (const QVariant &v : topLanes) {
        const int l = v.toInt();
        topSet.insert(l);
        maxLane = std::max(maxLane, l);
    }
    for (const QVariant &v : botLanes) {
        const int l = v.toInt();
        botSet.insert(l);
        maxLane = std::max(maxLane, l);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Hard-clip the gutter: a history deeper than the lane pitch can compress
    // for still has to stop at the cap rather than draw over the message.
    painter->setClipRect(QRect(r.left(), r.top(), kGraphMaxTextIndent,
                               r.height()),
                         Qt::IntersectClip);

    // On merge rows the lines stop short of the node by the ring radius, so
    // the hollow ring keeps a clean background gap around its centre dot
    // instead of lane strokes cutting through it.
    const qreal trim = (isMerge && nodeLane >= 0) ? metrics.nodeOuter : 0.0;

    // Round caps/joins keep the lanes and their loops smooth where they meet
    // nodes and each other.
    auto strokePath = [&](const QPainterPath &path, const QColor &c) {
        QPen pen(c, metrics.penWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    };
    auto straight = [&](qreal x, qreal y0, qreal y1, const QColor &c) {
        QPainterPath path(QPointF(x, y0));
        path.lineTo(QPointF(x, y1));
        strokePath(path, c);
    };
    // A lane looping into the node from the row's top edge: vertical in its
    // own lane, then a rounded quarter-circle corner onto the node's
    // centreline — the smooth "loop" the VS Code graph draws for merges.
    auto loopIn = [&](int lane, const QColor &c) {
        const qreal x0 = laneX(lane);
        const qreal x1 = laneX(nodeLane);
        const qreal rad = qMax(0.0, qMin(qAbs(x1 - x0) - trim, yMid - yTop));
        const qreal sx = (x1 > x0) ? 1.0 : -1.0;
        QPainterPath path(QPointF(x0, yTop));
        path.lineTo(QPointF(x0, yMid - rad));
        path.quadTo(QPointF(x0, yMid), QPointF(x0 + sx * rad, yMid));
        path.lineTo(QPointF(x1 - sx * trim, yMid));
        strokePath(path, c);
    };
    // A lane looping out of the node towards the row's bottom edge: horizontal
    // along the centreline, then the rounded corner down into its own lane.
    auto loopOut = [&](int lane, const QColor &c) {
        const qreal x0 = laneX(nodeLane);
        const qreal x1 = laneX(lane);
        const qreal rad = qMax(0.0, qMin(qAbs(x1 - x0) - trim, yBot - yMid));
        const qreal sx = (x1 > x0) ? 1.0 : -1.0;
        QPainterPath path(QPointF(x0 + sx * trim, yMid));
        path.lineTo(QPointF(x1 - sx * rad, yMid));
        path.quadTo(QPointF(x1, yMid), QPointF(x1, yMid + rad));
        path.lineTo(QPointF(x1, yBot));
        strokePath(path, c);
    };

    // Every lane other than the node's: straight through if present at both
    // edges, a merge loop if it only enters from the top, a branch loop if it
    // only leaves at the bottom. Rows without a node (expanded file rows) only
    // carry pass-through lanes; anything else degrades to a straight stub.
    for (int lane = 0; lane <= maxLane; ++lane) {
        if (lane == nodeLane)
            continue;
        const bool inTop = topSet.contains(lane);
        const bool inBot = botSet.contains(lane);
        const QColor c = commitGraphLaneColor(lane);
        if (inTop && inBot)
            straight(laneX(lane), yTop, yBot, c);
        else if (inTop)
            nodeLane >= 0 ? loopIn(lane, c) : straight(laneX(lane), yTop, yMid, c);
        else if (inBot)
            nodeLane >= 0 ? loopOut(lane, c) : straight(laneX(lane), yMid, yBot, c);
    }

    if (nodeLane >= 0) {
        const QColor c = commitGraphLaneColor(nodeLane);
        const qreal nx = laneX(nodeLane);
        // The node's own lane: a straight stub above (it was reached from a
        // child) and below (its first parent continues here), trimmed at the
        // ring's edge on merge rows so the ring interior stays clear.
        if (topSet.contains(nodeLane))
            straight(nx, yTop, yMid - trim, c);
        if (botSet.contains(nodeLane))
            straight(nx, yMid + trim, yBot, c);
        if (isOutgoing) {
            // A local-only tip is not a commit of its own.  Draw it as a dotted
            // ring, then continue its lane into the actual top commit below.
            painter->setBrush(Qt::NoBrush);
            QPen pen(c, metrics.penWidth * 0.9, Qt::DotLine);
            pen.setCapStyle(Qt::RoundCap);
            painter->setPen(pen);
            painter->drawEllipse(QPointF(nx, yMid), metrics.nodeOuter,
                                 metrics.nodeOuter);
        } else if (isMerge) {
            // Merge node: hollow ring + filled centre.
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(c, metrics.penWidth));
            painter->drawEllipse(QPointF(nx, yMid), metrics.nodeOuter,
                                 metrics.nodeOuter);
            painter->setPen(Qt::NoPen);
            painter->setBrush(c);
            painter->drawEllipse(QPointF(nx, yMid), metrics.nodeInner,
                                 metrics.nodeInner);
        } else {
            // Regular commit: a solid dot.
            painter->setPen(Qt::NoPen);
            painter->setBrush(c);
            const qreal dot = std::max(1.5, metrics.nodeOuter - 0.7);
            painter->drawEllipse(QPointF(nx, yMid), dot, dot);
        }
    }
    painter->restore();
}

} // namespace forkmesh::ui
