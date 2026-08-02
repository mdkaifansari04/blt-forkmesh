#include "ClaudeTranscriptView.h"

#include "AgentPromptImages.h"
#include "CodexTranscriptStyle.h"
#include "ScrollJumpButtons.h"

#include <QDateTime>
#include <QEasingCurve>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QImageReader>
#include <QPixmapCache>
#include <QtConcurrentRun>
#include <functional>
#include <QButtonGroup>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString esc(const QString &s) { return s.toHtmlEscaped(); }

// The transcript renders in the system monospace face throughout, like the
// terminal the CLI actually runs in.
QFont monoFont() { return QFontDatabase::systemFont(QFontDatabase::FixedFont); }

// The permission mode a CLI init event reports, spelled the way the composer's
// mode selector does. Used only for sessions ForkMesh didn't launch (surfaced
// external ones), where no stored mode label is available.
QString permissionModeLabel(const QString &raw)
{
    if (raw == QLatin1String("bypassPermissions") || raw == QLatin1String("acceptAll"))
        return QStringLiteral("Auto");
    if (raw == QLatin1String("acceptEdits"))
        return QStringLiteral("Edit");
    if (raw == QLatin1String("plan"))
        return QStringLiteral("Plan");
    if (raw == QLatin1String("default"))
        return QStringLiteral("Ask");
    return raw; // an unknown/future mode reads better verbatim than dropped
}

// Search-match highlight colours, fixed rather than theme-derived so they read
// clearly on both light and dark canvases (dark text on a warm fill). The
// currently-selected match gets a stronger orange.
const QString kMatchBg = QStringLiteral("#ffd33d");
const QString kMatchFg = QStringLiteral("#1f2328");
const QString kCurMatchBg = QStringLiteral("#ff8c42");

// Count case-insensitive occurrences of needle in haystack.
int countOccurrences(const QString &hay, const QString &needle)
{
    int n = 0, from = 0;
    while (true) {
        const int i = hay.indexOf(needle, from, Qt::CaseInsensitive);
        if (i < 0)
            break;
        ++n;
        from = i + needle.size();
    }
    return n;
}

// How many times query appears in a label's rendered text. Plain/auto labels are
// searched directly; markdown/rich labels are searched through a QTextDocument so
// the count matches what the user actually sees (markup excluded). A cheap reject
// on the raw source first avoids building a document for the common no-match case.
int countMatchesIn(const QString &orig, Qt::TextFormat fmt, const QString &query)
{
    if (query.isEmpty() || orig.indexOf(query, 0, Qt::CaseInsensitive) < 0)
        return 0;
    if (fmt != Qt::MarkdownText && fmt != Qt::RichText)
        return countOccurrences(orig, query);
    QTextDocument d;
    if (fmt == Qt::MarkdownText)
        d.setMarkdown(orig);
    else
        d.setHtml(orig);
    int n = 0;
    for (QTextCursor c = d.find(query); !c.isNull(); c = d.find(query, c))
        ++n;
    return n;
}
} // namespace

static QString capLabelText(const QString &text, bool collapseLines);

// A foldable thinking section, CLI style: a muted italic header ("Thought for
// 6s") that folds its body open/shut on click (▸/▾), with the body indented
// under it. The body folds with a height animation, and a section can softly
// pulse while it is live (the streaming "Thinking…" card).
class Collapsible : public QFrame
{
public:
    Collapsible(const QString &header, bool expanded,
                const ClaudeTranscriptView::Palette &p,
                QWidget *parent = nullptr)
        : QFrame(parent), m_open(expanded), m_label(header)
    {
        setObjectName(QStringLiteral("xscript_section"));
        setStyleSheet(QStringLiteral(
            "QFrame#xscript_section{background:transparent;border:none;}"));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        m_btn = new QPushButton(this);
        m_btn->setCursor(Qt::PointingHandCursor);
        m_btn->setFont(monoFont());
        m_btn->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:transparent;text-align:left;"
            "color:%1;font-style:italic;padding:0;}"
            "QPushButton:hover{color:%2;}").arg(p.muted, p.accent));
        v->addWidget(m_btn);
        m_bodyWidget = new QWidget(this);
        m_bodyWidget->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
        m_bodyLayout = new QVBoxLayout(m_bodyWidget);
        m_bodyLayout->setContentsMargins(14, 2, 0, 0); // indent under the header
        m_bodyLayout->setSpacing(6);
        v->addWidget(m_bodyWidget);
        m_bodyWidget->setVisible(m_open);
        QObject::connect(m_btn, &QPushButton::clicked, m_btn,
                         [this] { setExpanded(!m_open); });
        updateHeaderText();
    }
    QVBoxLayout *body() { return m_bodyLayout; }
    void setHeaderText(const QString &t) { m_label = t; updateHeaderText(); }

    void setExpanded(bool on)
    {
        if (on == m_open)
            return;
        m_open = on;
        updateHeaderText();
        animateBody();
    }

    // Softly breathe the whole section's opacity while it is live, then settle to
    // fully opaque. Used for the streaming "Thinking…" card.
    void setPulsing(bool on)
    {
        if (!on) {
            if (m_pulse) {
                m_pulse->stop();
                m_pulse->deleteLater();
                m_pulse = nullptr;
            }
            setGraphicsEffect(nullptr);
            return;
        }
        if (m_pulse)
            return;
        auto *eff = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(eff);
        m_pulse = new QPropertyAnimation(eff, "opacity", this);
        m_pulse->setDuration(1300);
        m_pulse->setStartValue(1.0);
        m_pulse->setKeyValueAt(0.5, 0.55);
        m_pulse->setEndValue(1.0);
        m_pulse->setEasingCurve(QEasingCurve::InOutSine);
        m_pulse->setLoopCount(-1);
        m_pulse->start();
    }

private:
    void updateHeaderText()
    {
        m_btn->setText((m_open ? QStringLiteral("▾ ") : QStringLiteral("▸ "))
                       + m_label);
    }
    void animateBody()
    {
        if (!m_anim) {
            m_anim = new QPropertyAnimation(m_bodyWidget, "maximumHeight", this);
            // stop() never emits finished, so a reconfigured run can't trip the
            // previous one's settle step — we just read m_open here.
            QObject::connect(m_anim, &QPropertyAnimation::finished, this, [this] {
                if (m_open)
                    m_bodyWidget->setMaximumHeight(QWIDGETSIZE_MAX);
                else
                    m_bodyWidget->setVisible(false);
            });
        }
        m_anim->stop();
        if (m_open) {
            m_bodyWidget->setVisible(true);
            m_anim->setDuration(180);
            m_anim->setEasingCurve(QEasingCurve::OutCubic);
            m_anim->setStartValue(0);
            m_anim->setEndValue(qMax(0, m_bodyWidget->sizeHint().height()));
        } else {
            m_anim->setDuration(160);
            m_anim->setEasingCurve(QEasingCurve::InCubic);
            m_anim->setStartValue(m_bodyWidget->height());
            m_anim->setEndValue(0);
        }
        m_anim->start();
    }
    QPushButton *m_btn = nullptr;
    QWidget *m_bodyWidget = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    bool m_open = true;
    QString m_label;
    QPropertyAnimation *m_anim = nullptr;  // body fold animation
    QPropertyAnimation *m_pulse = nullptr; // live "breathing" while streaming
};

// A word-wrapped QLabel that caches heightForWidth. A long transcript stacks
// hundreds of word-wrapped Markdown/RichText labels inside a widget-resizable
// QScrollArea, and Qt's layout re-runs heightForWidth — which re-lays-out each
// label's QTextDocument — for *every* row each time a row is added or the view
// is resized, calling it repeatedly within a single pass. With a big transcript
// that O(rows) text relayout froze the GUI thread for seconds (issue #234).
//
// The height of a word-wrapped label only changes when its width, font, or text
// changes. The cache maps width → height, keyed alongside the text length: any
// content change a user can see (a streamed delta, a search-highlight span)
// shifts the text length and drops every entry, so a stale height can't survive
// a real reflow; font/style changes invalidate explicitly. Multiple widths are
// kept because a single layout pass interleaves queries at different widths
// (minimumHeightForWidth probes the minimum width, heightForWidth the real one)
// — a one-slot cache thrashed between them and re-laid-out the document on
// every call, which the stall watchdog caught as >500ms layout storms.
class CacheLabel : public QLabel
{
public:
    using QLabel::QLabel;

    int heightForWidth(int w) const override
    {
        const int len = text().size();
        if (len != m_len) {
            m_len = len;
            m_heights.clear();
        }
        const auto it = m_heights.constFind(w);
        if (it != m_heights.constEnd())
            return it.value();
        const int h = QLabel::heightForWidth(w);
        // A continuous resize streams new widths; keep the map from growing
        // without bound (a handful of live widths is the steady state).
        if (m_heights.size() >= 32)
            m_heights.clear();
        m_heights.insert(w, h);
        return h;
    }

protected:
    void changeEvent(QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::FontChange:
        case QEvent::ApplicationFontChange:
        case QEvent::StyleChange:
            m_heights.clear(); // metrics may have shifted; recompute on next query
            break;
        default:
            break;
        }
        QLabel::changeEvent(e);
    }

private:
    mutable int m_len = -1;
    mutable QHash<int, int> m_heights; // width → cached heightForWidth
};

// Defined further down; used by the peek block below to bound a pathological
// single line / sheer volume before it reaches a word-wrapped label.
static QString capLabelText(const QString &text, bool collapseLines);

// A monospace, no-background label for a slice of tool output (matches makeMono).
static CacheLabel *monoSlice(const QString &text, const QString &fg, bool capCollapse)
{
    auto *l = new CacheLabel;
    l->setTextFormat(Qt::PlainText);
    l->setText(capLabelText(text, capCollapse));
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    l->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(fg));
    return l;
}

// A long tool-output block shown "peeked": the first two and last two lines stay
// visible with a small clickable divider in between saying how many lines are
// hidden (▸ ⋯ N lines); clicking it reveals — or re-hides — the middle. Output
// short enough that nothing would be hidden renders as a plain block, no toggle.
class OutputPeek : public QWidget
{
public:
    static constexpr int kHead = 2;
    static constexpr int kTail = 2;

    // plusLabel spells the hidden middle as Codex does ("… +12 lines") rather
    // than as Claude Code's "⋯ 12 lines".
    OutputPeek(const QString &text, const QString &fg,
               const ClaudeTranscriptView::Palette &p, bool plusLabel = false,
               QWidget *parent = nullptr)
        : QWidget(parent), m_plus(plusLabel)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);

        const QStringList lines = text.split(QLatin1Char('\n'));
        m_hidden = lines.size() - kHead - kTail;
        if (m_hidden <= 0) {
            v->addWidget(monoSlice(text, fg, false));
            return;
        }

        v->addWidget(monoSlice(lines.mid(0, kHead).join(QLatin1Char('\n')), fg, false));

        m_toggle = new QPushButton(this);
        m_toggle->setCursor(Qt::PointingHandCursor);
        // Codex's elision note sits in the output's own column, so it reads as
        // one of its lines rather than as a smaller caption.
        if (m_plus)
            m_toggle->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_toggle->setStyleSheet(
            QStringLiteral("QPushButton{border:none;background:transparent;"
                           "text-align:left;color:%1;%2padding:1px 0;}"
                           "QPushButton:hover{color:%3;}")
                .arg(p.muted,
                     m_plus ? QString() : QStringLiteral("font-size:11px;"),
                     p.accent));
        v->addWidget(m_toggle, 0, Qt::AlignLeft);

        m_middle = monoSlice(lines.mid(kHead, m_hidden).join(QLatin1Char('\n')), fg, true);
        m_middle->setVisible(false);
        v->addWidget(m_middle);

        v->addWidget(monoSlice(lines.mid(lines.size() - kTail, kTail).join(QLatin1Char('\n')),
                               fg, false));

        updateToggle();
        QObject::connect(m_toggle, &QPushButton::clicked, m_toggle,
                         [this] { setOpen(!m_open); });
    }

    // Reveal the middle (so a search match hidden inside it can be scrolled to).
    void expand() { setOpen(true); }

private:
    void setOpen(bool on)
    {
        m_open = on;
        if (m_middle)
            m_middle->setVisible(on);
        updateToggle();
    }
    void updateToggle()
    {
        if (!m_toggle)
            return;
        const QString unit = m_hidden == 1 ? QStringLiteral("line")
                                           : QStringLiteral("lines");
        const QString shape = m_open ? QStringLiteral("hide %1 %2")
                                     : (m_plus ? QStringLiteral("… +%1 %2")
                                               : QStringLiteral("⋯ %1 %2"));
        m_toggle->setText(shape.arg(m_hidden).arg(unit));
    }
    QPushButton *m_toggle = nullptr;
    QWidget *m_middle = nullptr;
    int m_hidden = 0;
    bool m_open = false;
    bool m_plus = false;
};

// One transcript row: a narrow glyph gutter (the CLI's ● / > / ✻ marker) beside
// the row's content. A user turn additionally paints a full-width background
// band — gutter included — the way the CLI shades the whole prompt line.
class RailItem : public QWidget
{
public:
    static constexpr int kRailW = 22;
    static constexpr int kGap = 12;          // vertical space between items
    static constexpr int kNodeY = kGap + 9;  // glyph aligned to the first text line

    RailItem(QWidget *content, ClaudeTranscriptView::RowGlyph glyph,
             const QString &glyphColor, const QString &bandColor,
             QWidget *parent = nullptr)
        : QWidget(parent), m_glyph(glyph), m_color(glyphColor), m_band(bandColor)
    {
        setAttribute(Qt::WA_StyledBackground, false);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);
        auto *rail = new QWidget(this);
        rail->setFixedWidth(kRailW);
        rail->setAttribute(Qt::WA_TransparentForMouseEvents);
        rail->setStyleSheet(QStringLiteral("background:transparent;"));
        h->addWidget(rail);
        auto *holder = new QWidget(this);
        holder->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *hv = new QVBoxLayout(holder);
        hv->setContentsMargins(0, kGap, 0, 0); // the inter-item gap
        hv->setSpacing(0);
        content->setParent(holder);
        hv->addWidget(content);
        h->addWidget(holder, 1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        if (!m_band.isEmpty()) {
            g.setPen(Qt::NoPen);
            g.setBrush(QColor(m_band));
            g.drawRoundedRect(QRectF(0, kGap, width(), height() - kGap), 6, 6);
        }
        const double cx = kRailW / 2.0;
        switch (m_glyph) {
        case ClaudeTranscriptView::GlyphDot:
            g.setPen(Qt::NoPen);
            g.setBrush(QColor(m_color));
            g.drawEllipse(QPointF(cx, kNodeY), 4.0, 4.0);
            break;
        case ClaudeTranscriptView::GlyphChevron:
        case ClaudeTranscriptView::GlyphStar: {
            QFont f = monoFont();
            f.setBold(true);
            g.setFont(f);
            g.setPen(QColor(m_color));
            g.drawText(QRect(0, kGap, kRailW, 18), Qt::AlignCenter,
                       m_glyph == ClaudeTranscriptView::GlyphChevron
                           ? QStringLiteral(">")
                           : QStringLiteral("✻"));
            break;
        }
        case ClaudeTranscriptView::GlyphNone:
            break;
        }
    }

private:
    ClaudeTranscriptView::RowGlyph m_glyph;
    QString m_color, m_band;
};

// Decode an attachment already scaled to the width the view will show it at.
// Runs on a worker thread (see ThumbImage): a full-resolution decode plus a
// smooth downscale of a 4K screenshot costs hundreds of milliseconds, and doing
// it on the GUI thread froze the window while a transcript rendered (adhoc #90).
// setScaledSize lets the reader skip most of that work where the format allows.
QImage decodeScaledAttachment(const QString &path, int cap)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize src = reader.size(); // header only, no pixel decode
    if (src.isValid() && src.width() > cap)
        reader.setScaledSize(
            QSize(cap, qMax(1, qRound(double(cap) * src.height() / src.width()))));
    QImage img = reader.read();
    if (!img.isNull() && img.width() > cap)
        img = img.scaledToWidth(cap, Qt::SmoothTransformation);
    return img;
}

// An image attached to a user turn, shown as a small thumbnail; clicking it
// toggles between a thumbnail and a larger preview (issue #56).
//
// The decode happens off the GUI thread and the result is kept in the shared
// QPixmapCache, so re-rendering a transcript (every reloadAgents() does) costs
// a cache hit instead of a fresh decode + smooth scale of the original file.
class ThumbImage : public QLabel
{
public:
    ThumbImage(const QString &path, const QString &border, QWidget *parent = nullptr)
        : QLabel(parent), m_path(path)
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Click to expand"));
        setStyleSheet(
            QStringLiteral("border:1px solid %1;border-radius:6px;").arg(border));
        m_srcSize = QImageReader(path).size(); // header read; reserves the box
        m_stamp = QFileInfo(path).lastModified().toMSecsSinceEpoch();
        applyScale();
    }

protected:
    void mousePressEvent(QMouseEvent *) override
    {
        m_expanded = !m_expanded;
        setToolTip(m_expanded ? QStringLiteral("Click to shrink")
                              : QStringLiteral("Click to expand"));
        applyScale();
    }

private:
    void applyScale()
    {
        const int cap = m_expanded ? 560 : 160;
        m_wanted = cap;
        const QString key =
            QStringLiteral("fm-thumb:%1:%2:%3").arg(m_path).arg(m_stamp).arg(cap);
        QPixmap cached;
        if (QPixmapCache::find(key, &cached)) {
            showScaled(cached);
            return;
        }
        // Hold the row at the height the finished thumbnail will take, so the
        // transcript does not jump when the decode lands.
        if (m_srcSize.isValid() && m_srcSize.width() > 0) {
            const int w = qMin(m_srcSize.width(), cap);
            setMinimumSize(w, qMax(1, qRound(double(w) * m_srcSize.height() /
                                             m_srcSize.width())));
        }
        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this,
                [this, watcher, cap, key] {
                    watcher->deleteLater();
                    const QImage img = watcher->result();
                    if (img.isNull())
                        return;
                    const QPixmap pm = QPixmap::fromImage(img);
                    QPixmapCache::insert(key, pm);
                    if (m_wanted == cap) // a later toggle already superseded this
                        showScaled(pm);
                });
        watcher->setFuture(QtConcurrent::run(decodeScaledAttachment, m_path, cap));
    }

    void showScaled(const QPixmap &pm)
    {
        setMinimumSize(0, 0); // the pixmap now drives the size hint
        setPixmap(pm);
    }

    QString m_path;
    QSize m_srcSize;
    qint64 m_stamp = 0;
    int m_wanted = 0;
    bool m_expanded = false;
};

// One clickable row of an inline-choice card (see addInlineChoices): unlike a
// QPushButton this wraps long text, since the CLI's heuristically-detected
// options are often full sentences rather than short labels.
class ChoiceOption : public QFrame
{
public:
    explicit ChoiceOption(const QString &text, QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(10, 8, 10, 8);
        auto *l = new QLabel(text);
        l->setWordWrap(true);
        l->setAttribute(Qt::WA_TransparentForMouseEvents); // clicks reach the frame
        v->addWidget(l);
    }

    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent *) override
    {
        if (onClick)
            onClick();
    }
};

ClaudeTranscriptView::ClaudeTranscriptView(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    m_container = new QWidget;
    m_col = new QVBoxLayout(m_container);
    m_col->setContentsMargins(8, 14, 14, 14);
    m_col->setSpacing(0); // RailItems supply their own inter-item gap
    m_bottomSpacer = new QWidget;
    // Expanding (not fixed) so that when the transcript is shorter than the
    // viewport the leftover height collects here instead of stretching the rows
    // — otherwise a just-started session's lone "you" bubble blows up into a tall
    // box with a big empty gap in it (issue #56).
    m_bottomSpacer->setMinimumHeight(8);
    m_bottomSpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_col->addWidget(m_bottomSpacer);
    setWidget(m_container);

    // Floating jump-to-top / jump-to-bottom buttons over the viewport corner.
    // The shared ScrollJumpButtons helper owns their placement and show/hide.
    m_jumpButtons = new ScrollJumpButtons(this);
    connect(m_jumpButtons, &ScrollJumpButtons::topClicked, this,
            &ClaudeTranscriptView::scrollToTop);
    connect(m_jumpButtons, &ScrollJumpButtons::bottomClicked, this,
            &ClaudeTranscriptView::scrollToBottom);

    QScrollBar *sb = verticalScrollBar();
    connect(sb, &QScrollBar::valueChanged, this, [this](int v) {
        QScrollBar *b = verticalScrollBar();
        m_stickBottom = v >= b->maximum() - 4;
        // Infinite-scroll-upward: nearing the top with more history available
        // asks the host for the next batch (see loadEarlierRequested()). Guarded
        // by m_loadEarlierPending so a held-at-top scroll position doesn't spam
        // requests before the previous batch has landed.
        if (m_skippedNotice && !m_loadEarlierPending && v <= b->minimum() + 200) {
            m_loadEarlierPending = true;
            emit loadEarlierRequested();
        }
    });
    connect(sb, &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_prependCompensationPending) {
            // The range just grew by whatever content prependEarlierEvents()
            // inserted above the viewport; shift by the same amount so the rows
            // the user was looking at stay put instead of sliding down.
            verticalScrollBar()->setValue(m_prependOldValue + (max - m_prependOldMax));
            m_prependCompensationPending = false;
            return;
        }
        if (m_stickBottom)
            verticalScrollBar()->setValue(max); // keep pinned as content grows
    });

    applyScheme();
    if (QStyleHints *h = QGuiApplication::styleHints())
        connect(h, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { applyScheme(); });
}

void ClaudeTranscriptView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    if (m_stickBottom)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void ClaudeTranscriptView::scrollToTop()
{
    m_stickBottom = false;
    smoothScrollTo(verticalScrollBar()->minimum());
}

void ClaudeTranscriptView::scrollToBottom()
{
    m_stickBottom = true;
    smoothScrollTo(verticalScrollBar()->maximum());
}

void ClaudeTranscriptView::jumpToBottom()
{
    m_stickBottom = true;
    if (m_scrollAnim)
        m_scrollAnim->stop(); // don't let an in-flight smooth scroll pull us back
    QScrollBar *sb = verticalScrollBar();
    sb->setValue(sb->maximum());
    // After a rebuild the rows haven't laid out yet, so maximum() is still stale;
    // m_stickBottom keeps us pinned when the deferred rangeChanged lands the real
    // range (see the rangeChanged handler in the constructor).
}

void ClaudeTranscriptView::setSplitDiffs(bool on) { m_splitDiffs = on; }

void ClaudeTranscriptView::setCodexStyle(bool on) { m_codexStyle = on; }

void ClaudeTranscriptView::setSessionContext(const QString &branch,
                                             const QString &mode,
                                             const QString &strength)
{
    m_ctxBranch = branch.trimmed();
    m_ctxMode = mode.trimmed();
    m_ctxStrength = strength.trimmed();
}

// A playful, ForkMesh-flavoured gerund for the live "what it's doing" ticker —
// except under Codex's dialect, where the CLI just counts the turn's seconds.
void ClaudeTranscriptView::cycleActivityWord()
{
    if (m_codexStyle) {
        if (!m_activityLabel)
            return;
        const qint64 secs =
            qMax(qint64(0), (QDateTime::currentMSecsSinceEpoch() - m_activityStartMs)
                                / 1000);
        const QString elapsed =
            secs < 60 ? QStringLiteral("%1s").arg(secs)
                      : QStringLiteral("%1m %2s")
                            .arg(secs / 60)
                            .arg(secs % 60, 2, 10, QLatin1Char('0'));
        m_activityLabel->setText(QStringLiteral("Working (%1)").arg(elapsed));
        return;
    }
    static const char *kWords[] = {
        "Thinking", "Pondering", "Cogitating", "Percolating", "Noodling",
        "Conjuring", "Ruminating", "Tinkering", "Synthesizing", "Scheming",
        // …and the made-up fork/mesh ones:
        "Forking", "Meshing", "Enmeshing", "Forkmeshing", "Remeshing",
        "Reticulating meshes", "Untangling forks", "Weaving the mesh",
        "Spinning up forks", "Meshulating", "Defragging the mesh", "Reforking",
        "Coalescing nodes", "Threading the mesh", "Herding forks",
        "Greasing the mesh", "Forkstrapping", "Demeshing", "Transmeshing"};
    constexpr int n = int(sizeof(kWords) / sizeof(kWords[0]));
    if (m_activityLabel)
        m_activityLabel->setText(
            QString::fromUtf8(kWords[QRandomGenerator::global()->bounded(n)])
            + QString::fromUtf8("\xE2\x80\xA6")); // …
}

void ClaudeTranscriptView::ensureActivity()
{
    if (m_activity || m_liveThinking)
        return; // the thinking card is its own live indicator
    m_activityLabel = new QLabel;
    m_activityLabel->setFont(monoFont());
    m_activityLabel->setStyleSheet(QStringLiteral(
        "color:%1;background:transparent;border:none;font-style:italic;")
        .arg(m_p.muted));
    m_activityStartMs = QDateTime::currentMSecsSinceEpoch();
    cycleActivityWord();
    // The ticker is transient scaffolding, not transcript content: it must not
    // close an "Explored" block that the next tool call still wants to join.
    m_keepExplore = true;
    m_activity = addRow(m_activityLabel, m_p.accent,
                        m_codexStyle ? GlyphDot : GlyphStar);
    m_keepExplore = false;
    if (!m_activityTimer) {
        m_activityTimer = new QTimer(this);
        connect(m_activityTimer, &QTimer::timeout, this,
                &ClaudeTranscriptView::cycleActivityWord);
    }
    // Codex counts seconds, so it needs a per-second tick; the gerunds read
    // better at a slower cadence.
    m_activityTimer->start(m_codexStyle ? 1000 : 2200);
}

void ClaudeTranscriptView::clearActivity()
{
    if (m_activityTimer)
        m_activityTimer->stop();
    if (m_activity) {
        m_col->removeWidget(m_activity);
        m_activity->deleteLater();
        m_activity = nullptr;
        m_activityLabel = nullptr;
    }
}

void ClaudeTranscriptView::applyScheme()
{
    bool dark = true;
    if (QStyleHints *h = QGuiApplication::styleHints())
        dark = h->colorScheme() != Qt::ColorScheme::Light;
    if (dark)
        m_p = {"#0d1117", "#161b22", "#30363d", "#e6edf3", "#8b949e", "#58a6ff",
               "#3fb950", "#f85149", "#12261a", "#2d1416", "#1f2630"};
    else
        m_p = {"#ffffff", "#f6f8fa", "#d0d7de", "#1f2328", "#656d76", "#0969da",
               "#1a7f37", "#cf222e", "#e6ffec", "#ffebe9", "#eef1f5"};
    setStyleSheet(QStringLiteral("QScrollArea{background:%1;border:none;}").arg(m_p.canvas));
    if (m_container)
        m_container->setStyleSheet(QStringLiteral("background:%1;").arg(m_p.canvas));
    const QString btnCss = QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:15px;"
        "font-size:12px;font-weight:700;}"
        "QPushButton:hover{background:%4;}")
        .arg(m_p.surface, m_p.text, m_p.border, m_p.userBg);
    if (m_jumpButtons)
        m_jumpButtons->setButtonStyle(btnCss);
}

void ClaudeTranscriptView::clear()
{
    clearActivity();
    // Drop search state without touching the labels (they are about to be
    // deleted below); the host re-applies the query against the rebuilt tree.
    m_searchLabels.clear();
    m_searchQuery.clear();
    m_searchTotal = 0;
    m_searchCurrent = -1;
    emit searchResultsChanged(0, 0);
    m_toolCards.clear();
    m_liveAgentText.clear();
    m_liveAgentTextValue.clear();
    m_askCards.clear();
    m_skippedNotice = nullptr;
    m_skippedCount = 0;
    m_loadEarlierPending = false;
    m_prependCompensationPending = false;
    m_prependAt = -1;
    m_exploreBlock = nullptr;
    m_exploreLines = nullptr;
    m_liveThinking = nullptr;
    m_thinkingBody = nullptr;
    m_thinkingText.clear();
    m_lastFinalizedThinkingText.clear();
    m_thinkingTokens = 0;
    m_totalTokens = 0;
    m_totalCost = 0.0;
    emit statsChanged(0, 0.0);
    while (m_col->count() > 1) { // keep the trailing spacer
        QLayoutItem *it = m_col->takeAt(0);
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }
    // The colour scheme is applied at construction and on the OS scheme-change
    // signal; it never changes during a clear. Re-running applyScheme() here just
    // forced a full Qt stylesheet repolish of the (still-undeleted) widget tree,
    // which is what showed up as multi-second event-loop stalls.
}

QString ClaudeTranscriptView::accentFor(const QString &name) const
{
    if (name == QLatin1String("Bash"))
        return m_p.add;
    if (name == QLatin1String("Edit") || name == QLatin1String("Write")
        || name == QLatin1String("MultiEdit") || name == QLatin1String("NotebookEdit"))
        return m_p.accent;
    if (name == QLatin1String("Read") || name == QLatin1String("Grep")
        || name == QLatin1String("Glob"))
        return m_p.muted;
    if (name == QLatin1String("Task"))
        return QStringLiteral("#a371f7");
    if (name == QLatin1String("TodoWrite"))
        return QStringLiteral("#d29922");
    return m_p.accent;
}

QWidget *ClaudeTranscriptView::addRow(QWidget *card, const QString &nodeColor,
                                      RowGlyph glyph, const QString &bandColor)
{
    // Anything that isn't the block itself (or the live ticker) ends the run of
    // exploration Codex was folding together — the next read starts a new one.
    if (!m_keepExplore) {
        m_exploreBlock = nullptr;
        m_exploreLines = nullptr;
    }
    int pos;
    if (m_prependAt >= 0) {
        // Loading earlier history: rows land at a fixed, advancing column index
        // instead of the usual tail position, so a batch renders in order above
        // whatever used to be first.
        pos = m_prependAt++;
    } else {
        pos = m_col->count() - 1; // before the trailing spacer
        // Keep the live activity ticker pinned as the last content row: new
        // rows slot in just above it.
        if (m_activity) {
            const int ai = m_col->indexOf(m_activity);
            if (ai >= 0)
                pos = ai;
        }
    }
    auto *item = new RailItem(card, glyph,
                              nodeColor.isEmpty() ? m_p.muted : nodeColor,
                              bandColor);
    m_col->insertWidget(pos, item);
    fadeIn(item);
    // Follow mode does the scrolling: the scrollbar's rangeChanged handler pins
    // the view to the bottom as the new row expands the content; ScrollJumpButtons
    // tracks the scrollbar itself to show/hide its arrows.
    return item;
}

// Token/cost bookkeeping for an event that is deliberately not rendered (the
// tail-capped replay skips old rows): mirrors handleEvent's stats reads only.
void ClaudeTranscriptView::accumulateStatsOnly(const QJsonObject &ev)
{
    const QString type = ev.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("assistant")) {
        m_totalTokens += (qint64)ev.value(QStringLiteral("message")).toObject()
                             .value(QStringLiteral("usage")).toObject()
                             .value(QStringLiteral("output_tokens")).toDouble();
    } else if (type == QLatin1String("result")) {
        const double cost = ev.value(QStringLiteral("total_cost_usd")).toDouble();
        if (cost > 0)
            m_totalCost = cost; // result carries the run's cumulative cost
    } else if (type == QLatin1String("_codex_usage")) {
        m_totalTokens = static_cast<qint64>(
            ev.value(QStringLiteral("total_tokens")).toDouble());
    }
}

void ClaudeTranscriptView::addSkippedNotice(int count)
{
    if (m_skippedNotice) {
        // Replacing an existing notice (a previous batch load updated the
        // count) — drop it first so we don't end up with two.
        m_col->removeWidget(m_skippedNotice);
        m_skippedNotice->deleteLater();
        m_skippedNotice = nullptr;
    }
    m_skippedCount = count;
    auto *btn = new QPushButton(
        QStringLiteral("Load %1 earlier event%2")
            .arg(count)
            .arg(count == 1 ? QString() : QStringLiteral("s")));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFont(monoFont());
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:transparent;text-align:left;"
        "color:%1;padding:0;}QPushButton:hover{color:%2;}")
        .arg(m_p.muted, m_p.accent));
    connect(btn, &QPushButton::clicked, this, [this] {
        if (m_loadEarlierPending)
            return;
        m_loadEarlierPending = true;
        emit loadEarlierRequested();
    });
    m_skippedNotice = addRow(btn, QString(), GlyphNone);
    emit statsChanged(m_totalTokens, m_totalCost);
}

void ClaudeTranscriptView::prependEarlierEvents(const QList<QJsonObject> &events,
                                                int stillSkipped)
{
    m_loadEarlierPending = false;
    if (events.isEmpty()) {
        if (stillSkipped <= 0 && m_skippedNotice) {
            m_col->removeWidget(m_skippedNotice);
            m_skippedNotice->deleteLater();
            m_skippedNotice = nullptr;
            m_skippedCount = 0;
        }
        return;
    }

    QScrollBar *sb = verticalScrollBar();
    m_prependOldMax = sb->maximum();
    m_prependOldValue = sb->value();
    m_prependCompensationPending = true;

    // If this session is still streaming, m_activity/m_liveThinking currently
    // hold the *real* live ticker/thinking card established by the tail's own
    // replay. The batch we're about to replay is strictly older history, but it
    // runs through the same handleEvent() state machine (ensureActivity() /
    // clearActivity() / finalizeThinking()) — without shadowing, a historical
    // turn boundary partway through the batch would delete the live ticker row
    // out from under the user (and stop the shared m_activityTimer for good,
    // since a non-null m_activity short-circuits the next real ensureActivity()
    // call). Swap the live state out, let history replay against a fresh
    // "nothing live" slate, then restore it untouched.
    QWidget *liveActivity = m_activity;
    QLabel *liveActivityLabel = m_activityLabel;
    QTimer *liveActivityTimer = m_activityTimer;
    Collapsible *liveThinking = m_liveThinking;
    QLabel *liveThinkingBody = m_thinkingBody;
    QString liveThinkingText = m_thinkingText;
    int liveThinkingTokens = m_thinkingTokens;
    qint64 liveThinkingStart = m_thinkingStartMs;
    m_activity = nullptr;
    m_activityLabel = nullptr;
    m_activityTimer = nullptr; // ensureActivity() allocates its own scratch timer
    m_liveThinking = nullptr;
    m_thinkingBody = nullptr;
    m_thinkingText.clear();
    m_thinkingTokens = 0;
    m_thinkingStartMs = 0;

    setBulkPopulate(true); // no per-row fade-in for a whole batch landing at once
    m_prependAt = 0;
    // The batch builds its own "Explored" blocks from scratch; an open one from
    // the tail is far below and must not swallow these older lines.
    m_exploreBlock = nullptr;
    m_exploreLines = nullptr;
    if (stillSkipped > 0) {
        addSkippedNotice(stillSkipped); // replaces the old notice, at the prepend head
    } else if (m_skippedNotice) {
        m_col->removeWidget(m_skippedNotice);
        m_skippedNotice->deleteLater();
        m_skippedNotice = nullptr;
        m_skippedCount = 0;
    }
    for (const QJsonObject &ev : events) {
        if (ev.value(QStringLiteral("type")).toString() == QLatin1String("_local_user"))
            addUserTurn(ev.value(QStringLiteral("text")).toString());
        else
            handleEvent(ev, /*countStats=*/false); // already folded into totals
    }
    m_prependAt = -1;
    setBulkPopulate(false);
    m_exploreBlock = nullptr; // the batch's last block ends with the batch
    m_exploreLines = nullptr;

    // Settle anything the batch left dangling (it was cut off mid-turn): this
    // ticker/thinking card belongs to now-static history, not live state, so
    // fold it away rather than leaving it visibly stuck.
    if (m_activity) {
        m_col->removeWidget(m_activity);
        m_activity->deleteLater();
    }
    if (m_activityTimer) {
        m_activityTimer->stop();
        m_activityTimer->deleteLater();
    }
    if (m_liveThinking) {
        m_liveThinking->setPulsing(false);
        m_liveThinking->setExpanded(false);
    }

    m_activity = liveActivity;
    m_activityLabel = liveActivityLabel;
    m_activityTimer = liveActivityTimer;
    m_liveThinking = liveThinking;
    m_thinkingBody = liveThinkingBody;
    m_thinkingText = liveThinkingText;
    m_thinkingTokens = liveThinkingTokens;
    m_thinkingStartMs = liveThinkingStart;
}

void ClaudeTranscriptView::fadeIn(QWidget *card)
{
    if (m_bulkPopulate)
        return; // replaying a stored session: rows appear at once, no per-row anim
    auto *eff = new QGraphicsOpacityEffect(card);
    card->setGraphicsEffect(eff);
    auto *a = new QPropertyAnimation(eff, "opacity", card);
    a->setDuration(240);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    QPointer<QWidget> pc = card;
    connect(a, &QPropertyAnimation::finished, card, [pc] {
        if (pc)
            pc->setGraphicsEffect(nullptr); // drop the effect once shown
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void ClaudeTranscriptView::smoothScrollTo(int value)
{
    QScrollBar *sb = verticalScrollBar();
    value = qBound(sb->minimum(), value, sb->maximum());
    if (!m_scrollAnim) {
        m_scrollAnim = new QPropertyAnimation(sb, "value", this);
        m_scrollAnim->setDuration(220);
        m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_scrollAnim->stop();
    m_scrollAnim->setStartValue(sb->value());
    m_scrollAnim->setEndValue(value);
    m_scrollAnim->start();
}

// ---- event dispatch --------------------------------------------------------

void ClaudeTranscriptView::handleEvent(const QJsonObject &ev, bool countStats)
{
    const QString type = ev.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("system")) {
        const QString sub = ev.value(QStringLiteral("subtype")).toString();
        if (sub == QLatin1String("init")) {
            // What the CLI announces (model, cwd) plus the run's own context —
            // branch, permission mode, reasoning strength — which only the host
            // knows (adhoc #9). Empty parts drop out rather than leaving gaps.
            QStringList parts{ev.value(QStringLiteral("model")).toString().trimmed(),
                              ev.value(QStringLiteral("cwd")).toString().trimmed()};
            QStringList context;
            if (!m_ctxBranch.isEmpty())
                context << QStringLiteral("branch %1").arg(m_ctxBranch);
            const QString mode =
                m_ctxMode.isEmpty()
                    ? permissionModeLabel(
                          ev.value(QStringLiteral("permissionMode")).toString().trimmed())
                    : m_ctxMode;
            if (!mode.isEmpty())
                context << mode;
            if (!m_ctxStrength.isEmpty())
                context << QStringLiteral("%1 thinking").arg(m_ctxStrength);
            parts.removeAll(QString());
            QString line = QStringLiteral("session started ") + parts.join(QLatin1Char(' '));
            if (!context.isEmpty())
                line += QStringLiteral(" · ") + context.join(QStringLiteral(" · "));
            auto *l = new QLabel(esc(line));
            l->setWordWrap(true); // long branch names shouldn't widen the view
            l->setFont(monoFont());
            l->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(m_p.muted));
            addRow(l, QString(), GlyphNone);
        } else if (sub == QLatin1String("thinking_tokens")) {
            ensureLiveThinking();
            setThinkingTokens(ev.value(QStringLiteral("estimated_tokens")).toInt());
        }
    } else if (type == QLatin1String("stream_event")) {
        const QJsonObject e = ev.value(QStringLiteral("event")).toObject();
        if (e.value(QStringLiteral("type")).toString() == QLatin1String("content_block_delta")) {
            const QJsonObject d = e.value(QStringLiteral("delta")).toObject();
            if (d.value(QStringLiteral("type")).toString() == QLatin1String("thinking_delta")) {
                ensureLiveThinking();
                appendThinkingDelta(d.value(QStringLiteral("thinking")).toString());
                if (d.contains(QStringLiteral("estimated_tokens")))
                    setThinkingTokens(d.value(QStringLiteral("estimated_tokens")).toInt());
            }
        }
    } else if (type == QLatin1String("assistant")) {
        const QJsonObject message = ev.value(QStringLiteral("message")).toObject();
        const qint64 out = (qint64)message.value(QStringLiteral("usage")).toObject()
                               .value(QStringLiteral("output_tokens")).toDouble();
        if (out > 0 && countStats) {
            m_totalTokens += out;
            emit statsChanged(m_totalTokens, m_totalCost);
        }
        addAssistantBlocks(message);
    } else if (type == QLatin1String("user")) {
        finalizeThinking(QString());
        const QJsonArray content = ev.value(QStringLiteral("message")).toObject()
                                       .value(QStringLiteral("content")).toArray();
        for (const QJsonValue &bv : content) {
            const QJsonObject b = bv.toObject();
            if (b.value(QStringLiteral("type")).toString() == QLatin1String("tool_result")) {
                QString text;
                const QJsonValue c = b.value(QStringLiteral("content"));
                if (c.isString())
                    text = c.toString();
                else
                    for (const QJsonValue &cv : c.toArray())
                        text += cv.toObject().value(QStringLiteral("text")).toString();
                addToolResult(b.value(QStringLiteral("tool_use_id")).toString(), text,
                              b.value(QStringLiteral("is_error")).toBool());
            }
        }
        ensureActivity(); // a tool came back; the agent keeps going
    } else if (type == QLatin1String("rate_limit_event")) {
        const QJsonObject info = ev.value(QStringLiteral("rate_limit_info")).toObject();
        // utilization arrives either as a 0..1 fraction or an already-scaled
        // 0..100 percentage depending on the CLI build; scale a fraction but pass
        // a percentage through so the gauge isn't 100x too high (adhoc #47).
        const double u = info.value(QStringLiteral("utilization")).toDouble();
        const int pct = qRound(u <= 1.0 ? u * 100.0 : u);
        const QString rlt = info.value(QStringLiteral("rateLimitType")).toString();
        // The premium per-model weekly window (adhoc #96) is its own bar, so it
        // must be recognised before the plain weekly test below — its type name
        // carries "seven_day" too.
        const bool fable = rlt.contains(QStringLiteral("fable"))
                           || rlt.contains(QStringLiteral("opus"))
                           || rlt.contains(QStringLiteral("premium"));
        const bool weekly = rlt.contains(QStringLiteral("seven"))
                            || rlt.contains(QStringLiteral("week"));
        const QString kind = fable ? QStringLiteral("fable")
                                   : (weekly ? QStringLiteral("weekly")
                                             : QStringLiteral("5h"));
        emit usageChanged(kind, QStringLiteral("%1%").arg(pct), pct);
    } else if (type == QLatin1String("result")) {
        finalizeThinking(QString());
        clearActivity(); // the turn is done
        const double cost = ev.value(QStringLiteral("total_cost_usd")).toDouble();
        if (cost > 0 && countStats) {
            m_totalCost = cost; // result carries the run's cumulative cost
            emit statsChanged(m_totalTokens, m_totalCost);
        }
        addResult(ev);
    } else if (type == QLatin1String("_codex_agent_delta")) {
        finalizeThinking(QString());
        const QString delta = ev.value(QStringLiteral("delta")).toString(
            ev.value(QStringLiteral("text")).toString());
        appendAgentText(ev.value(QStringLiteral("item_id")).toString(),
                        delta);
    } else if (type == QLatin1String("_codex_agent_complete")) {
        finalizeThinking(QString());
        completeAgentText(ev.value(QStringLiteral("item_id")).toString(),
                          ev.value(QStringLiteral("text")).toString());
        clearActivity();
    } else if (type == QLatin1String("_codex_reasoning_complete")) {
        finalizeThinking(ev.value(QStringLiteral("text")).toString());
    } else if (type == QLatin1String("_codex_tool_delta")) {
        appendToolOutput(ev.value(QStringLiteral("item_id")).toString(),
                         ev.value(QStringLiteral("delta")).toString());
    } else if (type == QLatin1String("_codex_usage")) {
        if (countStats) {
            m_totalTokens = static_cast<qint64>(
                ev.value(QStringLiteral("total_tokens")).toDouble());
            emit statsChanged(m_totalTokens, m_totalCost);
        }
    } else if (type == QLatin1String("_local_ask_answer")) {
        // Synthetic, host-injected event that records the user's answer to an
        // AskUserQuestion card so the answered state is rebuilt on replay.
        markAskAnswered(ev.value(QStringLiteral("tool_use_id")).toString(),
                        ev.value(QStringLiteral("text")).toString());
    } else if (type == QLatin1String("_local_notice")) {
        // Synthetic, host-injected status row — e.g. the auto model router
        // explaining which model it picked and why (adhoc #91). Muted, like
        // the "session started" divider; persists and replays with the stream.
        auto *l = new QLabel(ev.value(QStringLiteral("text")).toString());
        l->setTextFormat(Qt::PlainText);
        l->setWordWrap(true);
        l->setFont(monoFont());
        l->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;").arg(m_p.muted));
        addRow(l, QString(), GlyphNone);
    }
}

void ClaudeTranscriptView::appendAgentText(const QString &id, const QString &text)
{
    if (id.isEmpty() || text.isEmpty())
        return;
    QPointer<QLabel> label = m_liveAgentText.value(id);
    if (!label) {
        auto *created = new CacheLabel;
        created->setTextFormat(Qt::MarkdownText);
        created->setWordWrap(true);
        created->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                         Qt::LinksAccessibleByMouse);
        created->setOpenExternalLinks(true);
        created->setFont(monoFont());
        created->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;border:none;")
                .arg(m_p.text));
        addRow(created, m_p.text);
        label = created;
        m_liveAgentText.insert(id, label);
    }
    QString &value = m_liveAgentTextValue[id];
    value += text;
    label->setText(value);
}

void ClaudeTranscriptView::completeAgentText(const QString &id,
                                             const QString &text)
{
    QPointer<QLabel> label = m_liveAgentText.value(id);
    if (label) {
        const QString complete = text.isEmpty() ? m_liveAgentTextValue.value(id) : text;
        label->setText(complete);
        m_liveAgentTextValue[id] = complete;
        return;
    }
    if (!text.trimmed().isEmpty())
        addAssistantText(text);
}

void ClaudeTranscriptView::addAssistantBlocks(const QJsonObject &message)
{
    const QJsonArray content = message.value(QStringLiteral("content")).toArray();
    QString thinkingText;
    bool hasThinking = false;
    for (const QJsonValue &bv : content)
        if (bv.toObject().value(QStringLiteral("type")).toString() == QLatin1String("thinking")) {
            hasThinking = true;
            thinkingText += bv.toObject().value(QStringLiteral("thinking")).toString();
        }
    if (hasThinking)
        finalizeThinking(thinkingText);
    else if (m_liveThinking)
        finalizeThinking(QString());

    bool hadTool = false;
    bool askedQuestion = false;
    for (const QJsonValue &bv : content) {
        const QJsonObject b = bv.toObject();
        const QString t = b.value(QStringLiteral("type")).toString();
        if (t == QLatin1String("text")) {
            const QString text = b.value(QStringLiteral("text")).toString();
            if (!text.trimmed().isEmpty() && addAssistantText(text))
                askedQuestion = true; // inline multiple-choice prose (issue #212)
        } else if (t == QLatin1String("tool_use")) {
            hadTool = true;
            const QString name = b.value(QStringLiteral("name")).toString();
            if (name == QLatin1String("AskUserQuestion")) {
                // A clarifying question: render an interactive multiple-choice
                // card the user answers in place, not a passive tool card.
                askedQuestion = true;
                addAskUserQuestion(b.value(QStringLiteral("id")).toString(),
                                   b.value(QStringLiteral("input")).toObject());
            } else {
                addToolUse(b.value(QStringLiteral("id")).toString(), name,
                           b.value(QStringLiteral("input")).toObject());
            }
        }
    }
    // Tools running => keep the "what it's doing" ticker; a plain reply ends it.
    // An AskUserQuestion turn stops on the tool call and waits for the user, so
    // it's not "working" — drop the ticker even though a tool was used.
    if (askedQuestion)
        clearActivity();
    else if (hadTool)
        ensureActivity();
    else
        clearActivity();
}

// ---- thinking lifecycle ----------------------------------------------------

void ClaudeTranscriptView::ensureLiveThinking()
{
    if (m_liveThinking)
        return;
    clearActivity(); // the thinking card becomes the live indicator
    m_thinkingText.clear();
    m_lastFinalizedThinkingText.clear();
    m_thinkingTokens = 0;
    m_thinkingStartMs = QDateTime::currentMSecsSinceEpoch();
    m_thinkingBody = new CacheLabel(QStringLiteral("…"));
    m_thinkingBody->setWordWrap(true);
    m_thinkingBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_thinkingBody->setFont(monoFont());
    m_thinkingBody->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.muted));
    m_liveThinking = makeCollapsible(QStringLiteral("Thinking…"), m_thinkingBody, false);
    m_liveThinking->setPulsing(true);
    addRow(m_liveThinking, m_p.accent, GlyphStar);
}

void ClaudeTranscriptView::setThinkingTokens(int tokens)
{
    m_thinkingTokens = tokens;
    if (m_liveThinking)
        m_liveThinking->setHeaderText(
            QStringLiteral("Thinking… ~%1 tokens").arg(QLocale().toString(tokens)));
}

void ClaudeTranscriptView::appendThinkingDelta(const QString &text)
{
    if (text.isEmpty())
        return;
    m_thinkingText += text;
    if (m_thinkingBody)
        m_thinkingBody->setText(m_thinkingText);
    // live estimate from accumulated text; server setThinkingTokens overrides when available
    const int est = qMax(1, m_thinkingText.length() / 4);
    if (m_liveThinking)
        m_liveThinking->setHeaderText(
            QStringLiteral("Thinking… ~%1 tokens").arg(QLocale().toString(est)));
}

void ClaudeTranscriptView::finalizeThinking(const QString &fullText)
{
    const QString completed = fullText.trimmed();
    if (!m_liveThinking) {
        if (completed.isEmpty() || completed == m_lastFinalizedThinkingText)
            return;
        auto *body = new CacheLabel(completed);
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        body->setFont(monoFont());
        body->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;border:none;")
                .arg(m_p.muted));
        addRow(makeCollapsible(QStringLiteral("Thought"), body, false), m_p.accent,
               GlyphStar);
        m_lastFinalizedThinkingText = completed;
        return;
    }
    m_liveThinking->setPulsing(false);
    const qint64 secs = m_thinkingStartMs > 0
        ? (QDateTime::currentMSecsSinceEpoch() - m_thinkingStartMs) / 1000
        : 0;
    const QString label = secs > 0
        ? QStringLiteral("Thought for %1s").arg(secs)
        : QStringLiteral("Thought");

    const QString finalText = completed.isEmpty() ? m_thinkingText.trimmed() : completed;
    m_liveThinking->setHeaderText(label);
    m_liveThinking->setExpanded(false);
    if (m_thinkingBody)
        m_thinkingBody->setText(finalText);
    m_lastFinalizedThinkingText = finalText;
    m_liveThinking = nullptr;
    m_thinkingBody = nullptr;
    m_thinkingText.clear();
    m_thinkingTokens = 0;
    m_thinkingStartMs = 0;
}

// ---- bubbles & cards -------------------------------------------------------

Collapsible *ClaudeTranscriptView::makeCollapsible(const QString &header,
                                                  QWidget *body, bool expanded)
{
    auto *c = new Collapsible(header, expanded, m_p);
    if (body)
        c->body()->addWidget(body);
    return c;
}

// Assistant prose renders as plain, full-width text (no card), matching the
// Claude Code conversation view where only the user's turns are boxed.
bool ClaudeTranscriptView::parseInlineChoices(const QString &markdown, QStringList &options)
{
    // A markdown ordered-list item: "1. ..." or "1) ...", one per line. Capture
    // the number too so we can require a real 1, 2, 3, … sequence.
    static const QRegularExpression item(
        QStringLiteral("(?m)^[ \\t]{0,3}(\\d{1,2})[.)][ \\t]+(.+)$"));
    QStringList found;
    QList<int> numbers;
    int prevEnd = -1;
    int lastEnd = -1;
    bool contiguous = true;
    auto it = item.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        // The items must sit back-to-back (only blank lines between them). A gap
        // filled with prose means this is an explanation/plan whose paragraphs
        // happen to start with numbers, not a block of choices.
        if (prevEnd >= 0
            && !QStringView(markdown).mid(prevEnd, m.capturedStart(0) - prevEnd)
                    .trimmed().isEmpty())
            contiguous = false;
        numbers << m.captured(1).toInt();
        found << m.captured(2).trimmed();
        prevEnd = lastEnd = m.capturedEnd(0);
    }
    // Require at least two options so a lone numbered line isn't a "choice".
    if (found.size() < 2 || !contiguous)
        return false;
    // The options must be numbered sequentially from 1 (1, 2, 3, …). Scattered
    // or restarting numbers are ordinary prose that merely begins with a digit.
    for (int i = 0; i < numbers.size(); ++i) {
        if (numbers.at(i) != i + 1)
            return false;
    }
    // The question mark that makes this read like a clarifying question must
    // appear in the lead-in prose before the options or on an option line —
    // i.e. at or before the end of the list. A "?" only in text that *follows*
    // the list (e.g. "I changed:\n1. A\n2. B\nWant me to run tests?") is a
    // trailing yes/no follow-up, not a selection over these items, so it must
    // not turn a completed-work summary into a multiple-choice card (adhoc #15).
    if (!QStringView(markdown).left(lastEnd).contains(QLatin1Char('?')))
        return false;
    options = found;
    return true;
}

bool ClaudeTranscriptView::addAssistantText(const QString &markdown)
{
    auto *l = new CacheLabel;
    l->setTextFormat(Qt::MarkdownText);
    l->setText(markdown);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    l->setOpenExternalLinks(true);
    l->setFont(monoFont());
    l->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.text));

    QStringList options;
    if (parseInlineChoices(markdown, options)) {
        if (m_openInlineChoices)
            lockInlineChoices(m_openInlineChoices); // the conversation moved on
        auto *wrap = new QWidget;
        wrap->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *v = new QVBoxLayout(wrap);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(8);
        v->addWidget(l);
        v->addWidget(addInlineChoices(options));
        addRow(wrap, m_p.accent);
        return true;
    }
    // The CLI's assistant bullet is the plain foreground colour; tools get the
    // tinted dots.
    addRow(l, m_p.text);
    return false;
}

void ClaudeTranscriptView::addUserTurn(const QString &text)
{
    if (m_openInlineChoices)
        lockInlineChoices(m_openInlineChoices); // a reply arrived; stop offering it

    // The CLI's prompt line: a full-width grey band with a ">" in the gutter
    // (the band itself is painted by the row — see RailItem).
    auto *frame = new QWidget;
    frame->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(0, 7, 10, 7);
    v->setSpacing(6);

    // Lift any "Attached image: <path>" lines out of the prose and show each as a
    // small clickable thumbnail (issue #56); the rest renders as plain text.
    // The path is resolved through AgentPromptImages so an attachment written
    // to the old temp directory still renders after a restart (adhoc #66).
    static const QRegularExpression imgLine(
        QStringLiteral("^Attached image:\\s*(.+?)\\s*$"));
    QStringList prose;
    QStringList images;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch m = imgLine.match(line);
        const QString path =
            m.hasMatch() ? AgentPromptImages::resolve(m.captured(1)) : QString();
        // canRead() sniffs the header only: deciding "this line is an image"
        // must not decode the file on the GUI thread (ThumbImage does that on a
        // worker), or a transcript with screenshots freezes the window.
        if (!path.isEmpty() && QImageReader(path).canRead())
            images << path;
        else
            prose << line;
    }

    const QString bodyText = prose.join(QLatin1Char('\n')).trimmed();
    if (!bodyText.isEmpty()) {
        auto *body = new CacheLabel(bodyText);
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        body->setFont(monoFont());
        body->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.text));
        v->addWidget(body);
    }
    for (const QString &path : images)
        v->addWidget(new ThumbImage(path, m_p.border), 0, Qt::AlignLeft);
    addRow(frame, m_p.muted, GlyphChevron, m_p.userBg);
}

// A tool call, CLI style: a "Name(args)" header with the input and any output
// hanging below it off ⎿ connectors — a flat indented column, no boxes.
void ClaudeTranscriptView::addToolUse(const QString &id, const QString &name,
                                      const QJsonObject &input)
{
    if (m_codexStyle) {
        // Codex narrates by action: a burst of reads is one "Explored" block and
        // a real command is "Ran <command>", not a Bash card.
        if (addCodexExplore(id, name, input))
            return;
        if (name == QLatin1String("Bash")) {
            const QString cmd = CodexTranscriptStyle::commandText(input);
            if (!cmd.isEmpty()) {
                addCodexCommand(id, cmd);
                return;
            }
        }
    }

    // The CLI titles a subagent by its agent type — "Explore(Find the…)" — and
    // a todo update as "Update Todos".
    QString shown = name;
    bool bare = false;
    if (name == QLatin1String("Task")) {
        const QString sub =
            input.value(QStringLiteral("subagent_type")).toString().trimmed();
        if (!sub.isEmpty())
            shown = sub;
    } else if (name == QLatin1String("TodoWrite")) {
        shown = QStringLiteral("Update Todos");
    }
    if (m_codexStyle) {
        // "Edited src/foo.cpp", not "FileChange(src/foo.cpp)".
        const QString verb = CodexTranscriptStyle::toolVerb(name);
        if (!verb.isEmpty()) {
            shown = verb;
            bare = true;
        }
    }

    auto *card = new QFrame;
    card->setStyleSheet(QStringLiteral("QFrame{background:transparent;border:none;}"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    v->addWidget(dotHeader(shown, toolSubtitle(name, input), bare));

    if (QWidget *inBody = toolBody(name, input))
        v->addWidget(connectorRow(inBody));

    ToolCard tc;
    tc.io = v;
    tc.summarizeResult = name == QLatin1String("Read")
        || name == QLatin1String("Grep") || name == QLatin1String("Glob");
    tc.suppressResult = name == QLatin1String("TodoWrite");
    // Codex's own display of a write is the diff; the transport's echo of the
    // changed paths back as a "result" adds nothing under it.
    if (m_codexStyle && name == QLatin1String("FileChange"))
        tc.suppressResult = true;
    m_toolCards.insert(id, tc);
    addRow(card, accentFor(name));
}

void ClaudeTranscriptView::addToolResult(const QString &id, const QString &text,
                                         bool isError)
{
    auto it = m_toolCards.find(id);
    if (it == m_toolCards.end() || text.trimmed().isEmpty())
        return;
    ToolCard &tc = it.value();
    if (tc.exploreLine) {
        // A line in an "Explored" block: Codex doesn't echo what a read
        // returned, but a read that *failed* can't just look like it worked.
        tc.hasResult = true;
        if (isError)
            tc.exploreLine->setText(
                tc.exploreLine->text()
                + QStringLiteral("<span style='color:%1'> (failed)</span>")
                      .arg(m_p.del));
        return;
    }
    if (!tc.io || tc.hasResult)
        return; // a result already attached
    if (tc.codexCommand) {
        addCodexCommandResult(id, text, isError);
        return;
    }
    if (tc.suppressResult) {
        tc.hasResult = true;
        return;
    }
    if (tc.liveOutput) {
        tc.liveOutput->setText(capLabelText(text, true));
        tc.liveOutputText = text;
        tc.hasResult = true;
        return;
    }
    tc.hasResult = true;
    if (tc.summarizeResult && !isError) {
        // The header already names the file/pattern; the raw dump folds down to
        // the CLI's "⎿ N lines" note (a single short line shows as itself).
        const QStringList lines = text.trimmed().split(QLatin1Char('\n'));
        QString summary;
        if (lines.size() > 1)
            summary = QStringLiteral("%1 lines")
                          .arg(QLocale().toString(qint64(lines.size())));
        else {
            summary = lines.value(0).trimmed();
            if (summary.size() > 160)
                summary = summary.left(160) + QStringLiteral("…");
        }
        auto *l = new QLabel(summary);
        l->setFont(monoFont());
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setStyleSheet(QStringLiteral(
            "color:%1;background:transparent;border:none;").arg(m_p.muted));
        tc.io->addWidget(connectorRow(l));
        return;
    }
    // Peek the output: first/last two lines visible, the middle behind a toggle
    // that says how many lines it hides (dimmed like the CLI; errors tinted red).
    QWidget *out =
        new OutputPeek(text, isError ? m_p.del : m_p.muted, m_p, m_codexStyle);
    tc.io->addWidget(connectorRow(out));
}

void ClaudeTranscriptView::appendToolOutput(const QString &id,
                                            const QString &text)
{
    auto it = m_toolCards.find(id);
    if (it == m_toolCards.end() || text.isEmpty())
        return;
    ToolCard &tc = it.value();
    if (!tc.io || tc.hasResult)
        return;
    if (!tc.liveOutput) {
        auto *label = new CacheLabel;
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setFont(monoFont());
        label->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;border:none;")
                .arg(m_p.muted));
        // A Codex command's output shares one block with the exit= line that
        // follows it; everything else hangs off its own connector.
        if (tc.codexCommand)
            codexResultColumn(tc)->addWidget(label);
        else
            tc.io->addWidget(connectorRow(label));
        tc.liveOutput = label;
    }
    tc.liveOutputText += text;
    tc.liveOutput->setText(capLabelText(tc.liveOutputText, true));
}

// Claude Code's AskUserQuestion tool: render an interactive multiple-choice card
// (Anthropic Agent SDK "Handle approvals and user input" shape — a `questions`
// array, each with a question/header/options[label,description]/multiSelect) so
// the user can answer the clarifying question right in the transcript. Each
// option is a checkable button; a free-text "Other" field covers answers the
// options don't. On submit it emits questionAnswered(id, answer, sensitive); the
// host responds through the active CLI protocol and records a redacted
// "_local_ask_answer" event so the answered state replays on rebuild.
void ClaudeTranscriptView::addAskUserQuestion(const QString &id,
                                              const QJsonObject &input)
{
    clearActivity(); // the agent is now waiting on the user, not working

    auto *card = new QFrame;
    card->setStyleSheet(QStringLiteral(
        "QFrame{background:%1;border:1px solid %2;border-left:3px solid %3;"
        "border-radius:8px;}").arg(m_p.userBg, m_p.border, m_p.accent));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(10);

    auto *heading = new QLabel(QStringLiteral("Agent needs your input"));
    heading->setStyleSheet(QStringLiteral(
        "color:%1;font-weight:700;background:transparent;border:none;").arg(m_p.accent));
    v->addWidget(heading);

    // Everything interactive lives in one container so answering can lock it all
    // at once (see markAskAnswered).
    auto *controls = new QWidget;
    controls->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *cv = new QVBoxLayout(controls);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(16);
    v->addWidget(controls);

    // One question's live widgets, captured for the submit handler.
    struct QState {
        QString question;
        bool multi = false;
        bool secret = false;
        QList<QPushButton *> optionButtons;
        QStringList optionLabels;
        QLineEdit *other = nullptr;
    };
    QVector<QState> states;
    bool cardSensitive = false;

    const QString optCss = QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;"
        "padding:7px 12px;text-align:left;font-weight:600;}"
        "QPushButton:hover{border-color:%4;}"
        "QPushButton:checked{background:%4;color:%1;border-color:%4;}")
        .arg(m_p.canvas, m_p.text, m_p.border, m_p.accent);
    const QString editCss = QStringLiteral(
        "QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:6px;"
        "padding:6px 10px;}QLineEdit:focus{border-color:%4;}")
        .arg(m_p.canvas, m_p.text, m_p.border, m_p.accent);

    for (const QJsonValue &qv : input.value(QStringLiteral("questions")).toArray()) {
        const QJsonObject q = qv.toObject();
        QState st;
        st.question = q.value(QStringLiteral("question")).toString();
        st.multi = q.value(QStringLiteral("multiSelect")).toBool();
        st.secret = q.value(QStringLiteral("isSecret")).toBool();
        cardSensitive = cardSensitive || st.secret;

        auto *qbox = new QWidget;
        qbox->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *qv2 = new QVBoxLayout(qbox);
        qv2->setContentsMargins(0, 0, 0, 0);
        qv2->setSpacing(6);

        auto *qlabel = new QLabel(st.question);
        qlabel->setWordWrap(true);
        qlabel->setStyleSheet(QStringLiteral(
            "color:%1;font-weight:600;background:transparent;border:none;").arg(m_p.text));
        qv2->addWidget(qlabel);

        // Exclusive for single-select (radio behaviour), free for multi-select.
        auto *group = new QButtonGroup(controls);
        group->setExclusive(!st.multi);

        for (const QJsonValue &ov : q.value(QStringLiteral("options")).toArray()) {
            const QJsonObject o = ov.toObject();
            const QString label = o.value(QStringLiteral("label")).toString();
            const QString desc = o.value(QStringLiteral("description")).toString();
            auto *btn = new QPushButton(label);
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(optCss);
            group->addButton(btn);
            qv2->addWidget(btn);
            st.optionButtons.append(btn);
            st.optionLabels.append(label);
            if (!desc.trimmed().isEmpty()) {
                auto *d = new QLabel(desc);
                d->setWordWrap(true);
                d->setStyleSheet(QStringLiteral(
                    "color:%1;background:transparent;border:none;"
                    "margin:0 0 2px 4px;font-size:12px;").arg(m_p.muted));
                qv2->addWidget(d);
            }
        }

        auto *other = new QLineEdit;
        if (st.secret)
            other->setEchoMode(QLineEdit::Password);
        other->setPlaceholderText(
            st.multi ? QStringLiteral("Other… (adds your own answer)")
                     : QStringLiteral("Other… (type your own answer)"));
        other->setStyleSheet(editCss);
        qv2->addWidget(other);
        st.other = other;

        // Keep the free-text field and the option buttons from fighting for a
        // single-select answer: typing clears the picked option, and picking an
        // option clears the free text. Multi-select stacks them, so leave it.
        if (!st.multi) {
            connect(other, &QLineEdit::textEdited, group, [group](const QString &t) {
                if (t.isEmpty())
                    return;
                if (QAbstractButton *b = group->checkedButton()) {
                    group->setExclusive(false);
                    b->setChecked(false);
                    group->setExclusive(true);
                }
            });
            connect(group, &QButtonGroup::buttonClicked, other,
                    [other](QAbstractButton *) { other->clear(); });
        }

        states.append(st);
        cv->addWidget(qbox);
    }

    auto *submit = new QPushButton(QStringLiteral("Send answer"));
    submit->setCursor(Qt::PointingHandCursor);
    submit->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:none;border-radius:6px;"
        "padding:8px 16px;font-weight:600;}"
        "QPushButton:hover{background:%3;}")
        .arg(m_p.accent, m_p.canvas, m_p.add));
    cv->addWidget(submit, 0, Qt::AlignLeft);

    auto *status = new QLabel;
    status->setWordWrap(true);
    status->setVisible(false);
    status->setStyleSheet(QStringLiteral(
        "color:%1;font-weight:600;background:transparent;border:none;").arg(m_p.add));
    v->addWidget(status);

    m_askCards.insert(id, AskCard{controls, status, cardSensitive});

    connect(submit, &QPushButton::clicked, this,
            [this, id, states, cardSensitive] {
        QStringList lines;
        for (const QState &st : states) {
            QStringList picks;
            for (int i = 0; i < st.optionButtons.size(); ++i)
                if (st.optionButtons[i]->isChecked())
                    picks << st.optionLabels[i];
            const QString otherText = st.other ? st.other->text().trimmed() : QString();
            QString value;
            if (st.multi) {
                if (!otherText.isEmpty())
                    picks << otherText;
                value = picks.join(QStringLiteral(", "));
            } else {
                value = !otherText.isEmpty()
                            ? otherText
                            : (picks.isEmpty() ? QString() : picks.first());
            }
            if (value.isEmpty())
                continue;
            lines << (states.size() > 1
                          ? QStringLiteral("%1 → %2").arg(st.question, value)
                          : value);
        }
        const QString answer = lines.join(QStringLiteral("\n"));
        if (answer.isEmpty())
            return; // nothing chosen yet — keep the card open
        markAskAnswered(id, answer); // instant local feedback
        emit questionAnswered(id, answer, cardSensitive);
    });

    addRow(card, m_p.accent);
}

// Lock a (possibly rebuilt) AskUserQuestion card and show the chosen reply.
// Idempotent: called both from the submit handler and from the replayed
// "_local_ask_answer" event.
void ClaudeTranscriptView::markAskAnswered(const QString &id, const QString &answer)
{
    auto it = m_askCards.find(id);
    if (it == m_askCards.end())
        return;
    AskCard &c = it.value();
    if (c.buttons) {
        c.buttons->setEnabled(false);
        auto *fx = new QGraphicsOpacityEffect(c.buttons);
        fx->setOpacity(0.5);
        c.buttons->setGraphicsEffect(fx);
    }
    if (c.status) {
        QString shown = c.sensitive ? QStringLiteral("Secret answer submitted")
                                    : answer;
        if (!c.sensitive)
            shown.replace(QLatin1Char('\n'), QStringLiteral(" · "));
        c.status->setText(QStringLiteral("✓ You answered: %1").arg(shown));
        c.status->setVisible(true);
    }
}

// A lighter card for a heuristically-detected inline clarifying question (see
// parseInlineChoices): one clickable row per option. Clicking sends that
// option's full text as the reply — there's no tool_use_id to satisfy, so the
// host just forwards it as a normal follow-up prompt (issue #212).
QWidget *ClaudeTranscriptView::addInlineChoices(const QStringList &options)
{
    auto *box = new QWidget;
    box->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    const QString css = QStringLiteral(
        "color:%1;background:%2;border:1px solid %3;border-radius:6px;")
        .arg(m_p.text, m_p.canvas, m_p.border);

    for (const QString &opt : options) {
        auto *row = new ChoiceOption(opt);
        row->setStyleSheet(css);
        v->addWidget(row);
        row->onClick = [this, box, opt] {
            lockInlineChoices(box);
            emit inlineChoiceAnswered(opt);
        };
    }

    m_openInlineChoices = box;
    return box;
}

void ClaudeTranscriptView::lockInlineChoices(QWidget *box)
{
    if (!box || !box->isEnabled())
        return; // already locked
    box->setEnabled(false);
    auto *fx = new QGraphicsOpacityEffect(box);
    fx->setOpacity(0.5);
    box->setGraphicsEffect(fx);
    if (m_openInlineChoices == box)
        m_openInlineChoices = nullptr;
}

void ClaudeTranscriptView::addResult(const QJsonObject &ev)
{
    const bool err = ev.value(QStringLiteral("is_error")).toBool();
    const double cost = ev.value(QStringLiteral("total_cost_usd")).toDouble();
    const double ms = ev.value(QStringLiteral("duration_ms")).toDouble();
    const int turns = ev.value(QStringLiteral("num_turns")).toInt();
    QStringList details;
    if (turns > 0)
        details << QStringLiteral("%1 turn%2")
                       .arg(turns)
                       .arg(turns == 1 ? QString() : QStringLiteral("s"));
    if (ms > 0)
        details << QStringLiteral("%1s").arg(ms / 1000.0, 0, 'f', 1);
    const qint64 tokens = static_cast<qint64>(
        ev.value(QStringLiteral("total_tokens")).toDouble());
    if (tokens > 0)
        details << QStringLiteral("%1 tokens").arg(QLocale().toString(tokens));
    if (cost > 0)
        details << QStringLiteral("$%1").arg(cost, 0, 'f', 4);
    QString label = QStringLiteral("%1 %2")
                        .arg(err ? QStringLiteral("✗") : QStringLiteral("✓"),
                             err ? QStringLiteral("Failed") : QStringLiteral("Done"));
    if (!details.isEmpty())
        label += QStringLiteral(" · ") + details.join(QStringLiteral(" · "));
    auto *l = new QLabel(label);
    l->setFont(monoFont());
    l->setStyleSheet(QStringLiteral("color:%1;font-weight:600;background:transparent;")
                         .arg(err ? m_p.del : m_p.add));
    addRow(l, err ? m_p.del : m_p.add);
}

// ---- Codex CLI dialect (see setCodexStyle) ---------------------------------

bool ClaudeTranscriptView::addCodexExplore(const QString &id, const QString &name,
                                           const QJsonObject &input)
{
    QString verb, target;
    if (name == QLatin1String("Bash")) {
        if (!CodexTranscriptStyle::exploreLine(
                CodexTranscriptStyle::commandText(input), verb, target))
            return false;
    } else if (name == QLatin1String("Read")) {
        verb = QStringLiteral("Read");
        target = input.value(QStringLiteral("file_path"))
                     .toString()
                     .section(QLatin1Char('/'), -1);
    } else if (name == QLatin1String("Grep")) {
        verb = QStringLiteral("Search");
        target = input.value(QStringLiteral("pattern")).toString();
        const QString path = input.value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            target += QStringLiteral(" in ") + path.section(QLatin1Char('/'), -1);
    } else if (name == QLatin1String("Glob")) {
        verb = QStringLiteral("List");
        target = input.value(QStringLiteral("pattern")).toString();
    } else {
        return false;
    }
    if (target.trimmed().isEmpty())
        return false;

    if (!m_exploreBlock)
        m_exploreLines = nullptr; // the row itself went away; start a new block
    if (!m_exploreLines) {
        auto *card = new QFrame;
        card->setStyleSheet(
            QStringLiteral("QFrame{background:transparent;border:none;}"));
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        v->addWidget(dotHeader(QStringLiteral("Explored"), QString()));
        auto *lines = new QWidget;
        lines->setStyleSheet(QStringLiteral("background:transparent;"));
        m_exploreLines = new QVBoxLayout(lines);
        m_exploreLines->setContentsMargins(0, 0, 0, 0);
        m_exploreLines->setSpacing(2);
        v->addWidget(connectorRow(lines));
        // This row *is* the block, so it mustn't close the one it opens.
        m_keepExplore = true;
        m_exploreBlock = addRow(card, m_p.muted);
        m_keepExplore = false;
    }

    auto *l = new CacheLabel;
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFont(monoFont());
    l->setText(QStringLiteral("<span style='color:%1'>%2</span> "
                              "<span style='color:%3'>%4</span>")
                   .arg(m_p.accent, esc(verb), m_p.text, esc(target.trimmed())));
    l->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    m_exploreLines->addWidget(l);

    // No card of its own: the line is the whole rendering, so a result has
    // nowhere to attach (addToolResult only tints it if the read failed).
    ToolCard tc;
    tc.exploreLine = l;
    m_toolCards.insert(id, tc);
    return true;
}

QString ClaudeTranscriptView::codexCommandHtml(const QString &line) const
{
    QString out;
    bool commandWord = true; // the next word starts a command
    int i = 0;
    const int n = line.size();
    while (i < n) {
        const int space = i;
        while (i < n && line.at(i).isSpace())
            ++i;
        if (i > space)
            out += esc(line.mid(space, i - space));
        if (i >= n)
            break;
        // One word, quotes included — they're part of what the user typed.
        const int start = i;
        QChar quote;
        while (i < n) {
            const QChar c = line.at(i);
            if (!quote.isNull()) {
                if (c == quote)
                    quote = QChar();
            } else if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
                quote = c;
            } else if (c.isSpace()) {
                break;
            }
            ++i;
        }
        const QString token = line.mid(start, i - start);
        static const QRegularExpression assignment(
            QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*="));
        QString colour = m_p.text;
        if (CodexTranscriptStyle::isShellOperator(token)) {
            colour = m_p.accent;
            commandWord = true; // what follows is a command again
        } else if (assignment.match(token).hasMatch()) {
            // VAR=value in front of a command; the command is still to come.
        } else if (commandWord) {
            colour = m_p.accent;
            commandWord = false;
        } else if (token.size() > 1 && token.startsWith(QLatin1Char('-'))) {
            colour = m_p.del; // a flag
        }
        out += QStringLiteral("<span style='color:%1'>%2</span>")
                   .arg(colour, esc(token));
    }
    return out;
}

QWidget *ClaudeTranscriptView::codexMoreLines(int n)
{
    auto *l = new QLabel(QStringLiteral("… +%1 lines").arg(n));
    l->setFont(monoFont());
    l->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;")
            .arg(m_p.muted));
    return l;
}

void ClaudeTranscriptView::addCodexCommand(const QString &id,
                                           const QString &command)
{
    // A script's first line rides in the header and the next couple hang under
    // it; the rest is counted, the way the CLI keeps a heredoc from taking over
    // the transcript.
    constexpr int kScriptPreview = 2;
    QStringList lines = command.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return;

    auto *card = new QFrame;
    card->setStyleSheet(QStringLiteral("QFrame{background:transparent;border:none;}"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    v->addWidget(dotHeader(QStringLiteral("Ran"), codexCommandHtml(lines.first()),
                           /*bareSubtitle=*/true, /*html=*/true));

    const QStringList rest = lines.mid(1);
    if (!rest.isEmpty()) {
        auto *body = new QWidget;
        body->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *bv = new QVBoxLayout(body);
        bv->setContentsMargins(0, 0, 0, 0);
        bv->setSpacing(2);
        for (const QString &script : rest.mid(0, kScriptPreview)) {
            auto *l = new CacheLabel;
            l->setTextFormat(Qt::RichText);
            l->setWordWrap(true);
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            l->setFont(monoFont());
            l->setText(codexCommandHtml(script));
            l->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
            bv->addWidget(l);
        }
        if (rest.size() > kScriptPreview)
            bv->addWidget(codexMoreLines(rest.size() - kScriptPreview));
        v->addWidget(connectorRow(body, QStringLiteral("│")));
    }

    ToolCard tc;
    tc.io = v;
    tc.codexCommand = true;
    m_toolCards.insert(id, tc);
    addRow(card, accentFor(QStringLiteral("Bash")));
}

QVBoxLayout *ClaudeTranscriptView::codexResultColumn(ToolCard &tc)
{
    if (!tc.codexResult) {
        auto *box = new QWidget;
        box->setStyleSheet(QStringLiteral("background:transparent;"));
        tc.codexResult = new QVBoxLayout(box);
        tc.codexResult->setContentsMargins(0, 0, 0, 0);
        tc.codexResult->setSpacing(2);
        tc.io->addWidget(connectorRow(box));
    }
    return tc.codexResult;
}

void ClaudeTranscriptView::addCodexCommandResult(const QString &id,
                                                 const QString &text,
                                                 bool isError)
{
    auto it = m_toolCards.find(id);
    if (it == m_toolCards.end() || !it.value().io)
        return;
    ToolCard &tc = it.value();
    tc.hasResult = true;

    // The transport appends "Exit code: N" to a finished command's output (see
    // CodexAppServerSession::toolOutput); Codex leads with that instead, so peel
    // it back off the tail.
    QString body = text;
    QLabel *exitLabel = nullptr;
    static const QRegularExpression exitTail(
        QStringLiteral("\\n?Exit code: (-?\\d+)\\s*$"));
    const QRegularExpressionMatch m = exitTail.match(body);
    if (m.hasMatch()) {
        const int code = m.captured(1).toInt();
        body = body.left(m.capturedStart());
        exitLabel = new QLabel(QStringLiteral("exit=%1").arg(code));
        exitLabel->setFont(monoFont());
        exitLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        exitLabel->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;border:none;")
                .arg(code == 0 ? m_p.add : m_p.del));
    }

    if (tc.liveOutput) {
        // The output already streamed into the block; the exit line slots in
        // above it rather than trailing what it describes.
        tc.liveOutput->setText(capLabelText(body, true));
        tc.liveOutputText = body;
        if (exitLabel)
            codexResultColumn(tc)->insertWidget(0, exitLabel);
        return;
    }
    if (exitLabel)
        codexResultColumn(tc)->addWidget(exitLabel);
    if (!body.trimmed().isEmpty())
        codexResultColumn(tc)->addWidget(
            new OutputPeek(body, isError ? m_p.del : m_p.muted, m_p, true));
}

// ---- per-tool rendering ----------------------------------------------------

QString ClaudeTranscriptView::toolSubtitle(const QString &name,
                                           const QJsonObject &input) const
{
    if (name == QLatin1String("Bash"))
        return input.value(QStringLiteral("description")).toString();
    if (name == QLatin1String("Read")) {
        const QString fp = input.value(QStringLiteral("file_path")).toString();
        const QString base = fp.section(QLatin1Char('/'), -1);
        const int off = input.value(QStringLiteral("offset")).toInt();
        const int lim = input.value(QStringLiteral("limit")).toInt();
        if (off > 0 && lim > 0)
            return QStringLiteral("%1 (lines %2-%3)").arg(base).arg(off).arg(off + lim - 1);
        return base.isEmpty() ? fp : base;
    }
    if (name == QLatin1String("Edit") || name == QLatin1String("Write")
        || name == QLatin1String("MultiEdit") || name == QLatin1String("NotebookEdit"))
        return input.value(QStringLiteral("file_path")).toString();
    if (name == QLatin1String("Task"))
        return input.value(QStringLiteral("description")).toString();
    if (name == QLatin1String("Grep"))
        return input.value(QStringLiteral("pattern")).toString();
    if (name == QLatin1String("WebSearch"))
        return input.value(QStringLiteral("query")).toString();
    if (name == QLatin1String("FileChange")) {
        QStringList paths;
        for (const QJsonValue &value : input.value(QStringLiteral("changes")).toArray()) {
            const QString path = value.toObject().value(QStringLiteral("path")).toString();
            if (!path.isEmpty())
                paths << path;
        }
        return paths.join(QStringLiteral(", "));
    }
    return QString();
}

QWidget *ClaudeTranscriptView::toolBody(const QString &name, const QJsonObject &input)
{
    if (name == QLatin1String("Bash")) {
        // "$ command", the way the CLI previews shell commands.
        const QString cmd =
            input.value(QStringLiteral("command")).toString().trimmed();
        return cmd.isEmpty() ? nullptr
                             : makeMono(QStringLiteral("$ ") + cmd, false);
    }
    if (name == QLatin1String("Edit"))
        return makeDiff(input.value(QStringLiteral("old_string")).toString(),
                        input.value(QStringLiteral("new_string")).toString());
    if (name == QLatin1String("Write"))
        return makeMono(input.value(QStringLiteral("content")).toString(), true);
    if (name == QLatin1String("MultiEdit")) {
        auto *holder = new QWidget;
        holder->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *v = new QVBoxLayout(holder);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);
        for (const QJsonValue &ev : input.value(QStringLiteral("edits")).toArray()) {
            const QJsonObject e = ev.toObject();
            v->addWidget(makeDiff(e.value(QStringLiteral("old_string")).toString(),
                                  e.value(QStringLiteral("new_string")).toString()));
        }
        return holder;
    }
    if (name == QLatin1String("TodoWrite")) {
        // The CLI's checklist: done items struck through and dimmed, the item in
        // progress bold behind a filled marker, pending items plain.
        QStringList rows;
        for (const QJsonValue &tv : input.value(QStringLiteral("todos")).toArray()) {
            const QJsonObject t = tv.toObject();
            const QString st = t.value(QStringLiteral("status")).toString();
            const QString item = esc(t.value(QStringLiteral("content")).toString());
            if (st == QLatin1String("completed"))
                rows << QStringLiteral("<span style='color:%1'>☒ <s>%2</s></span>")
                            .arg(m_p.muted, item);
            else if (st == QLatin1String("in_progress"))
                rows << QStringLiteral(
                            "<span style='color:%1'>■</span> <b>%2</b>")
                            .arg(accentFor(name), item);
            else
                rows << QStringLiteral("☐ %1").arg(item);
        }
        if (rows.isEmpty())
            return nullptr;
        auto *l = new CacheLabel;
        l->setTextFormat(Qt::RichText);
        l->setText(QStringLiteral("<div style='color:%1'>%2</div>")
                       .arg(m_p.text, rows.join(QStringLiteral("<br>"))));
        l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setFont(monoFont());
        l->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
        return l;
    }
    if (name == QLatin1String("Task")) {
        const QString prompt =
            input.value(QStringLiteral("prompt")).toString().trimmed();
        return prompt.isEmpty() ? nullptr : makeMono(prompt, true);
    }
    if (name == QLatin1String("FileChange")) {
        auto *holder = new QWidget;
        holder->setStyleSheet(QStringLiteral("background:transparent;"));
        auto *layout = new QVBoxLayout(holder);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        for (const QJsonValue &value : input.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject change = value.toObject();
            auto *path = new QLabel(change.value(QStringLiteral("path")).toString());
            path->setStyleSheet(
                QStringLiteral("color:%1;font-weight:600;background:transparent;")
                    .arg(m_p.muted));
            layout->addWidget(path);
            layout->addWidget(makeCode(
                change.value(QStringLiteral("diff")).toString(), true));
        }
        return holder;
    }
    if (name == QLatin1String("Read") || name == QLatin1String("Grep")
        || name == QLatin1String("Glob") || name == QLatin1String("WebSearch"))
        return nullptr; // the subtitle already says the file/pattern/query
    if (!input.isEmpty())
        return makeMono(QString::fromUtf8(
                            QJsonDocument(input).toJson(QJsonDocument::Indented))
                            .trimmed(),
                        true);
    return nullptr;
}

// "Name(args)" — bold tool name with the muted argument in parens, the way the
// CLI titles a tool call; the row's gutter supplies the ● glyph. Codex phrases
// the same line as a sentence instead — "Ran git status", "Edited src/foo.cpp"
// — so bareSubtitle drops the parens and html keeps pre-built markup (a
// shell-highlighted command) intact.
QWidget *ClaudeTranscriptView::dotHeader(const QString &name,
                                         const QString &subtitle,
                                         bool bareSubtitle, bool html)
{
    auto *l = new CacheLabel;
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFont(monoFont());
    QString markup = QStringLiteral("<span style='color:%1;font-weight:700'>%2</span>")
                         .arg(m_p.text, esc(name));
    if (!subtitle.trimmed().isEmpty()) {
        const QString arg = html ? subtitle.trimmed() : esc(subtitle.trimmed());
        if (bareSubtitle)
            markup += QStringLiteral(" <span style='color:%1'>%2</span>")
                          .arg(m_p.text, arg);
        else
            markup += QStringLiteral("<span style='color:%1'>(%2)</span>")
                          .arg(m_p.muted, arg);
    }
    l->setText(markup);
    l->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    return l;
}

// The CLI's "  ⎿  result" shape: a muted L-connector in a small gutter with the
// content hanging beside it. Codex draws the same gutter with a plain "└".
QWidget *ClaudeTranscriptView::connectorRow(QWidget *content, const QString &glyph)
{
    auto *row = new QWidget;
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(2, 0, 0, 0);
    h->setSpacing(8);
    const QString mark = !glyph.isEmpty()
                             ? glyph
                             : (m_codexStyle ? QStringLiteral("└")
                                             : QStringLiteral("⎿"));
    auto *lab = new QLabel(mark);
    lab->setFont(monoFont());
    lab->setStyleSheet(QStringLiteral(
        "color:%1;background:transparent;border:none;").arg(m_p.muted));
    h->addWidget(lab, 0, Qt::AlignTop);
    content->setParent(row);
    h->addWidget(content, 1);
    return row;
}

// Bound text destined for a word-wrapped QLabel so a pathological tool input/result
// (a minified bundle, a base64 blob, a megabyte of command output on one line)
// can't freeze the UI: QLabel lays its document out synchronously on the GUI
// thread, and one very long logical line makes QTextLine line-breaking
// pathologically slow even when the line *count* is tiny (adhoc #169). Caps the
// line count (when collapsing), elides runaway single lines, and caps the grand
// total. The raw-log surface and on-disk transcript still keep the full text.
static QString capLabelText(const QString &text, bool collapseLines)
{
    constexpr int kMaxLines = 16;        // matches the long-output collapse below
    constexpr int kMaxLineChars = 2000;  // one wrapped line stays cheap to lay out
    constexpr int kMaxTotalChars = 20000;
    const QStringList lines = text.split(QLatin1Char('\n'));
    const bool longText = collapseLines && lines.size() > kMaxLines;
    QStringList shownLines = longText ? lines.mid(0, kMaxLines) : lines;
    for (QString &ln : shownLines)
        if (ln.size() > kMaxLineChars)
            ln = ln.left(kMaxLineChars) + QStringLiteral(" …");
    QString shown = shownLines.join(QLatin1Char('\n'));
    if (longText)
        shown += QStringLiteral("\n… (%1 more lines)").arg(lines.size() - kMaxLines);
    if (shown.size() > kMaxTotalChars)
        shown = shown.left(kMaxTotalChars) + QStringLiteral("\n… (truncated)");
    return shown;
}

// Monospace content with no panel background — it sits inside a tool card box.
QWidget *ClaudeTranscriptView::makeMono(const QString &text, bool collapsedIfLong)
{
    // capLabelText caps the *display* string: it elides a single pathologically
    // long logical line (a minified bundle / base64 blob) that QTextLine lays out
    // synchronously, on top of the line-count collapse — and CacheLabel caches the
    // resulting size so repaints stay cheap.
    const QString shown = capLabelText(text, collapsedIfLong);
    auto *l = new CacheLabel;
    l->setTextFormat(Qt::PlainText);
    l->setText(shown);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    l->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.text));
    return l;
}

// A monospace code/output block. Renders as PLAIN text (no HTML escaping) so
// shell metacharacters like " and > show literally instead of as entities.
QWidget *ClaudeTranscriptView::makeCode(const QString &text, bool collapsedIfLong)
{
    // capLabelText caps the *display* string: it elides a single pathologically
    // long logical line (a minified bundle / base64 blob) that QTextLine lays out
    // synchronously, on top of the line-count collapse — and CacheLabel caches the
    // resulting size so repaints stay cheap.
    const QString shown = capLabelText(text, collapsedIfLong);
    auto *l = new CacheLabel;
    l->setTextFormat(Qt::PlainText);
    l->setText(shown);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    l->setStyleSheet(QStringLiteral("color:%1;background:%2;border:none;border-radius:6px;padding:8px 10px;")
                         .arg(m_p.text, m_p.canvas));
    return l;
}

QWidget *ClaudeTranscriptView::makeDiff(const QString &oldText, const QString &newText)
{
    QStringList oldL = oldText.split(QLatin1Char('\n'));
    QStringList newL = newText.split(QLatin1Char('\n'));
    int pre = 0;
    while (pre < oldL.size() && pre < newL.size() && oldL[pre] == newL[pre])
        ++pre;
    int suf = 0;
    while (suf < oldL.size() - pre && suf < newL.size() - pre
           && oldL[oldL.size() - 1 - suf] == newL[newL.size() - 1 - suf])
        ++suf;

    QString html;
    if (m_splitDiffs) {
        // Side-by-side: old on the left, new on the right, aligned row-for-row.
        html = QStringLiteral("<table style='border-collapse:collapse;"
                              "font-family:monospace;white-space:pre;width:100%'>");
        auto cell = [&](const QString &s, const QString &color, const QString &bg) {
            return QStringLiteral("<td style='width:50%;color:%1;background:%2;"
                                  "padding:0 6px;vertical-align:top'>%3</td>")
                .arg(color, bg, s.isEmpty() ? QStringLiteral("&nbsp;") : esc(s));
        };
        auto pair = [&](const QString &l, const QString &lc, const QString &lbg,
                        const QString &r, const QString &rc, const QString &rbg) {
            html += QStringLiteral("<tr>") + cell(l, lc, lbg) + cell(r, rc, rbg)
                    + QStringLiteral("</tr>");
        };
        for (int i = 0; i < pre; ++i)
            pair(QStringLiteral("  ") + oldL[i], m_p.muted, m_p.canvas,
                 QStringLiteral("  ") + oldL[i], m_p.muted, m_p.canvas);
        const int delN = oldL.size() - suf - pre, addN = newL.size() - suf - pre;
        for (int i = 0; i < qMax(delN, addN); ++i) {
            const bool hasDel = i < delN, hasAdd = i < addN;
            pair(hasDel ? QStringLiteral("- ") + oldL[pre + i] : QString(),
                 m_p.del, hasDel ? m_p.delBg : m_p.canvas,
                 hasAdd ? QStringLiteral("+ ") + newL[pre + i] : QString(),
                 m_p.add, hasAdd ? m_p.addBg : m_p.canvas);
        }
        for (int i = oldL.size() - suf; i < oldL.size(); ++i)
            pair(QStringLiteral("  ") + oldL[i], m_p.muted, m_p.canvas,
                 QStringLiteral("  ") + oldL[i], m_p.muted, m_p.canvas);
        html += QStringLiteral("</table>");
    } else {
        html = QStringLiteral("<div style='font-family:monospace;white-space:pre'>");
        auto context = [&](const QString &s) {
            html += QStringLiteral("<div style='color:%1'>  %2</div>").arg(m_p.muted, esc(s));
        };
        for (int i = 0; i < pre; ++i)
            context(oldL[i]);
        for (int i = pre; i < oldL.size() - suf; ++i)
            html += QStringLiteral("<div style='color:%1;background:%2'>- %3</div>")
                        .arg(m_p.del, m_p.delBg, esc(oldL[i]));
        for (int i = pre; i < newL.size() - suf; ++i)
            html += QStringLiteral("<div style='color:%1;background:%2'>+ %3</div>")
                        .arg(m_p.add, m_p.addBg, esc(newL[i]));
        for (int i = oldL.size() - suf; i < oldL.size(); ++i)
            context(oldL[i]);
        html += QStringLiteral("</div>");
    }

    auto *l = new CacheLabel(html);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("background:%1;border:none;border-radius:6px;padding:8px 10px;")
                         .arg(m_p.canvas));
    return l;
}

// ---- transcript search (adhoc #201) ----------------------------------------

// Build the highlighted HTML for one label from its pristine text. Plain/auto
// labels are escaped and wrapped (preserving whitespace) so matches can be
// spanned; markdown/rich labels are re-rendered through a QTextDocument with a
// background applied to each match, preserving their formatting. currentLocalOcc
// is the 0-based index (within this label) of the selected match, or -1.
QString ClaudeTranscriptView::highlightedTextFor(const QString &orig,
                                                 Qt::TextFormat fmt,
                                                 int currentLocalOcc) const
{
    const QString normalSpan =
        QStringLiteral("background:%1;color:%2;").arg(kMatchBg, kMatchFg);
    const QString currentSpan =
        QStringLiteral("background:%1;color:%2;").arg(kCurMatchBg, kMatchFg);

    if (fmt != Qt::MarkdownText && fmt != Qt::RichText) {
        QString out;
        int from = 0, occ = 0;
        const int qlen = m_searchQuery.size();
        while (true) {
            const int i = orig.indexOf(m_searchQuery, from, Qt::CaseInsensitive);
            if (i < 0) {
                out += esc(orig.mid(from));
                break;
            }
            out += esc(orig.mid(from, i - from));
            out += QStringLiteral("<span style='%1'>%2</span>")
                       .arg(occ == currentLocalOcc ? currentSpan : normalSpan,
                            esc(orig.mid(i, qlen)));
            from = i + qlen;
            ++occ;
        }
        // pre-wrap keeps newlines/indentation of mono blocks while still wrapping.
        return QStringLiteral("<div style='white-space:pre-wrap'>%1</div>").arg(out);
    }

    QTextDocument d;
    if (fmt == Qt::MarkdownText)
        d.setMarkdown(orig);
    else
        d.setHtml(orig);
    QTextCharFormat normal;
    normal.setBackground(QColor(kMatchBg));
    normal.setForeground(QColor(kMatchFg));
    QTextCharFormat current;
    current.setBackground(QColor(kCurMatchBg));
    current.setForeground(QColor(kMatchFg));
    int occ = 0;
    for (QTextCursor c = d.find(m_searchQuery); !c.isNull();
         c = d.find(m_searchQuery, c)) {
        c.mergeCharFormat(occ == currentLocalOcc ? current : normal);
        ++occ;
    }
    return d.toHtml();
}

void ClaudeTranscriptView::rebuildSearchMatches()
{
    m_searchLabels.clear();
    m_searchTotal = 0;
    if (m_searchQuery.isEmpty() || !m_container || !m_col)
        return;
    // Walk rows in actual layout order (top to bottom), not QObject child-
    // insertion order: "load earlier" prepends a batch of older rows above
    // ones already in the tree, so the two orders can now disagree — a row's
    // *own* subtree is still built all at once, so findChildren within it
    // stays correct, only the across-row order needed fixing.
    for (int i = 0; i < m_col->count(); ++i) {
        QLayoutItem *item = m_col->itemAt(i);
        QWidget *row = item ? item->widget() : nullptr;
        if (!row)
            continue;
        const QList<QLabel *> labels = row->findChildren<QLabel *>();
        for (QLabel *l : labels) {
            if (!l)
                continue;
            const Qt::TextFormat fmt = l->textFormat();
            const int c = countMatchesIn(l->text(), fmt, m_searchQuery);
            if (c > 0) {
                m_searchLabels.push_back({l, fmt, l->text(), c});
                m_searchTotal += c;
            }
        }
    }
}

void ClaudeTranscriptView::renderSearchHighlights()
{
    int base = 0;
    for (LabelHit &h : m_searchLabels) {
        if (!h.label) {
            base += h.count;
            continue;
        }
        const int curLocal = (m_searchCurrent >= base
                              && m_searchCurrent < base + h.count)
                                 ? m_searchCurrent - base
                                 : -1;
        h.label->setTextFormat(Qt::RichText);
        h.label->setText(highlightedTextFor(h.orig, h.fmt, curLocal));
        base += h.count;
    }
}

void ClaudeTranscriptView::restoreSearchOriginals()
{
    for (LabelHit &h : m_searchLabels) {
        if (!h.label)
            continue;
        h.label->setTextFormat(h.fmt);
        h.label->setText(h.orig);
    }
}

QLabel *ClaudeTranscriptView::currentMatchLabel() const
{
    if (m_searchCurrent < 0)
        return nullptr;
    int base = 0;
    for (const LabelHit &h : m_searchLabels) {
        if (m_searchCurrent < base + h.count)
            return h.label;
        base += h.count;
    }
    return nullptr;
}

void ClaudeTranscriptView::scrollToCurrentMatch()
{
    QLabel *l = currentMatchLabel();
    if (!l)
        return;
    // Reveal the match if it sits inside a folded section (a "Thought" card) or
    // the hidden middle of a peeked output block.
    for (QWidget *w = l->parentWidget(); w; w = w->parentWidget()) {
        if (auto *c = dynamic_cast<Collapsible *>(w))
            c->setExpanded(true);
        else if (auto *o = dynamic_cast<OutputPeek *>(w))
            o->expand();
    }
    m_stickBottom = false; // jumping to a match takes us off the live tail
    ensureWidgetVisible(l, 40, 80);
}

int ClaudeTranscriptView::search(const QString &query)
{
    restoreSearchOriginals();
    m_searchQuery = query;
    rebuildSearchMatches();
    m_searchCurrent = m_searchTotal > 0 ? 0 : -1;
    renderSearchHighlights();
    scrollToCurrentMatch();
    emit searchResultsChanged(m_searchTotal > 0 ? m_searchCurrent + 1 : 0,
                              m_searchTotal);
    return m_searchTotal;
}

void ClaudeTranscriptView::clearSearch()
{
    restoreSearchOriginals();
    m_searchLabels.clear();
    m_searchQuery.clear();
    m_searchTotal = 0;
    m_searchCurrent = -1;
    emit searchResultsChanged(0, 0);
}

void ClaudeTranscriptView::stepMatch(int delta)
{
    if (m_searchTotal <= 0)
        return;
    m_searchCurrent =
        ((m_searchCurrent + delta) % m_searchTotal + m_searchTotal) % m_searchTotal;
    renderSearchHighlights();
    scrollToCurrentMatch();
    emit searchResultsChanged(m_searchCurrent + 1, m_searchTotal);
}

void ClaudeTranscriptView::searchNext() { stepMatch(+1); }
void ClaudeTranscriptView::searchPrev() { stepMatch(-1); }
