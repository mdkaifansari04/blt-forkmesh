#include "ClaudeTranscriptView.h"

#include "ScrollJumpButtons.h"

#include <QDateTime>
#include <QEasingCurve>
#include <QEvent>
#include <QFontDatabase>
#include <functional>
#include <QButtonGroup>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
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

// A foldable section: a clickable header (▸/▾) over a body. Instead of a heavy
// grey card it draws as an open section with a coloured accent bar down the left,
// so the transcript reads as a lively timeline rather than a stack of boxes. The
// body folds open/shut with a height animation, and a section can softly pulse
// while it is live (the streaming "Thinking…" card).
class Collapsible : public QFrame
{
public:
    Collapsible(const QString &header, bool expanded,
                const ClaudeTranscriptView::Palette &p, const QString &accent,
                QWidget *parent = nullptr)
        : QFrame(parent), m_open(expanded), m_label(header)
    {
        const QString bar = accent.isEmpty() ? p.border : accent;
        const QString hdr = accent.isEmpty() ? p.text : accent;
        setObjectName(QStringLiteral("xscript_section"));
        // Transparent body, just an accent stripe on the left — no boxed-in grey.
        setStyleSheet(QStringLiteral(
                          "QFrame#xscript_section{background:transparent;border:none;"
                          "border-left:3px solid %1;border-top-left-radius:0;"
                          "border-bottom-left-radius:0;border-radius:6px;}")
                          .arg(bar));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 6, 6, 6);
        v->setSpacing(6);
        m_btn = new QPushButton(this);
        m_btn->setCursor(Qt::PointingHandCursor);
        m_btn->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:transparent;text-align:left;"
            "color:%1;font-weight:600;padding:0;}"
            "QPushButton:hover{color:%2;}").arg(hdr, p.accent));
        v->addWidget(m_btn);
        m_bodyWidget = new QWidget(this);
        m_bodyWidget->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
        m_bodyLayout = new QVBoxLayout(m_bodyWidget);
        m_bodyLayout->setContentsMargins(0, 2, 0, 0);
        m_bodyLayout->setSpacing(6);
        v->addWidget(m_bodyWidget);
        // Sections no longer collapse/expand
        m_bodyWidget->setVisible(m_open);
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
        m_btn->setText(m_label);
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
// changes. We key the cache on (width, text length): any content change a user
// can see (a streamed delta, a search-highlight span) shifts the text length, so
// a stale height can't survive a real reflow; font/style changes invalidate it
// explicitly. Unchanged rows then answer in O(1) instead of re-laying-out.
class CacheLabel : public QLabel
{
public:
    using QLabel::QLabel;

    int heightForWidth(int w) const override
    {
        const int len = text().size();
        if (m_valid && w == m_w && len == m_len)
            return m_h;
        m_w = w;
        m_len = len;
        m_h = QLabel::heightForWidth(w);
        m_valid = true;
        return m_h;
    }

protected:
    void changeEvent(QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::FontChange:
        case QEvent::ApplicationFontChange:
        case QEvent::StyleChange:
            m_valid = false; // metrics may have shifted; recompute on next query
            break;
        default:
            break;
        }
        QLabel::changeEvent(e);
    }

private:
    mutable int m_w = -1;
    mutable int m_len = -1;
    mutable int m_h = 0;
    mutable bool m_valid = false;
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

    OutputPeek(const QString &text, const QString &fg,
               const ClaudeTranscriptView::Palette &p, QWidget *parent = nullptr)
        : QWidget(parent)
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
        m_toggle->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;background:transparent;text-align:left;"
            "color:%1;font-size:11px;padding:1px 0;}"
            "QPushButton:hover{color:%2;}").arg(p.muted, p.accent));
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
        m_toggle->setText((m_open ? QStringLiteral("hide %1 %2")
                                  : QStringLiteral("⋯ %1 %2"))
                              .arg(m_hidden)
                              .arg(unit));
    }
    QPushButton *m_toggle = nullptr;
    QWidget *m_middle = nullptr;
    int m_hidden = 0;
    bool m_open = false;
};

// One row on the transcript's timeline: a left rail (a vertical connecting line
// with a coloured node dot) beside the item's content. Consecutive rows abut, so
// their rails join into one continuous thread.
class RailItem : public QWidget
{
public:
    static constexpr int kRailW = 22;
    static constexpr int kGap = 12;          // vertical space between items
    static constexpr int kNodeY = kGap + 9;  // node aligned to the first text line

    RailItem(QWidget *content, const QString &nodeColor, const QString &lineColor,
             bool first, QWidget *parent = nullptr)
        : QWidget(parent), m_node(nodeColor), m_line(lineColor), m_first(first)
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

    // A prepended batch can displace this item from the top of the timeline —
    // flatten its rail line so the spine reads as continuous (see addRow()).
    void setFirst(bool first)
    {
        if (m_first == first)
            return;
        m_first = first;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter g(this);
        g.setRenderHint(QPainter::Antialiasing);
        const double cx = kRailW / 2.0;
        // The connecting line: from the node down for the first item, full height
        // otherwise, so abutting rows form one unbroken spine.
        g.setPen(QPen(QColor(m_line), 2));
        g.drawLine(QPointF(cx, m_first ? kNodeY : 0), QPointF(cx, height()));
        g.setPen(Qt::NoPen);
        g.setBrush(QColor(m_node));
        g.drawEllipse(QPointF(cx, kNodeY), 4.0, 4.0);
    }

private:
    QString m_node, m_line;
    bool m_first;
};

// An image attached to a user turn, shown as a small thumbnail; clicking it
// toggles between a thumbnail and a larger preview (issue #56).
class ThumbImage : public QLabel
{
public:
    ThumbImage(const QPixmap &full, const QString &border, QWidget *parent = nullptr)
        : QLabel(parent), m_full(full)
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Click to expand"));
        setStyleSheet(
            QStringLiteral("border:1px solid %1;border-radius:6px;").arg(border));
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
        const int w = qMin(m_full.width(), cap);
        setPixmap(m_full.scaledToWidth(w, Qt::SmoothTransformation));
    }
    QPixmap m_full;
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

// A playful, ForkMesh-flavoured gerund for the live "what it's doing" ticker.
void ClaudeTranscriptView::cycleActivityWord()
{
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
    m_activityLabel->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.muted));
    cycleActivityWord();
    m_activity = addRow(m_activityLabel, m_p.accent);
    if (!m_activityTimer) {
        m_activityTimer = new QTimer(this);
        connect(m_activityTimer, &QTimer::timeout, this,
                &ClaudeTranscriptView::cycleActivityWord);
    }
    m_activityTimer->start(2200);
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
    m_askCards.clear();
    m_skippedNotice = nullptr;
    m_skippedCount = 0;
    m_loadEarlierPending = false;
    m_prependCompensationPending = false;
    m_prependAt = -1;
    m_priorFirstRow = nullptr;
    m_liveThinking = nullptr;
    m_thinkingBody = nullptr;
    m_thinkingText.clear();
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

QWidget *ClaudeTranscriptView::addRow(QWidget *card, const QString &nodeColor)
{
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
    const bool first = pos == 0; // truly the top row of the timeline
    auto *item = new RailItem(card, nodeColor.isEmpty() ? m_p.muted : nodeColor,
                              m_p.border, first);
    m_col->insertWidget(pos, item);
    if (first) {
        // A row that used to be first (its rail line starts at the node, not
        // the top) may have just been displaced by a prepended batch — flatten
        // its line to the top so the spine reads as continuous.
        if (auto *old = dynamic_cast<RailItem *>(m_priorFirstRow.data()))
            old->setFirst(false);
        m_priorFirstRow = item;
    }
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
    m_skippedNotice = addRow(btn);
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
            auto *l = new QLabel(QStringLiteral("session started %1 %2")
                                     .arg(esc(ev.value(QStringLiteral("model")).toString()),
                                          esc(ev.value(QStringLiteral("cwd")).toString())));
            l->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(m_p.muted));
            addRow(l);
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
        const int pct = qRound(info.value(QStringLiteral("utilization")).toDouble() * 100);
        const QString rlt = info.value(QStringLiteral("rateLimitType")).toString();
        const bool weekly = rlt.contains(QStringLiteral("seven"))
                            || rlt.contains(QStringLiteral("week"));
        emit usageChanged(weekly ? QStringLiteral("weekly") : QStringLiteral("5h"),
                          QStringLiteral("%1%").arg(pct), pct);
    } else if (type == QLatin1String("result")) {
        finalizeThinking(QString());
        clearActivity(); // the turn is done
        const double cost = ev.value(QStringLiteral("total_cost_usd")).toDouble();
        if (cost > 0 && countStats) {
            m_totalCost = cost; // result carries the run's cumulative cost
            emit statsChanged(m_totalTokens, m_totalCost);
        }
        addResult(ev);
    } else if (type == QLatin1String("_local_ask_answer")) {
        // Synthetic, host-injected event that records the user's answer to an
        // AskUserQuestion card so the answered state is rebuilt on replay.
        markAskAnswered(ev.value(QStringLiteral("tool_use_id")).toString(),
                        ev.value(QStringLiteral("text")).toString());
    } else if (type == QLatin1String("_local_notice")) {
        // Synthetic, host-injected status row — e.g. the auto model router
        // explaining which model it picked and why (adhoc #91). Muted, like
        // the "session started" divider; persists and replays with the stream.
        auto *l = new QLabel(QStringLiteral("● ")
                             + ev.value(QStringLiteral("text")).toString());
        l->setTextFormat(Qt::PlainText);
        l->setWordWrap(true);
        l->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;").arg(m_p.muted));
        addRow(l);
    }
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
    m_thinkingTokens = 0;
    m_thinkingStartMs = QDateTime::currentMSecsSinceEpoch();
    m_thinkingBody = new CacheLabel(QStringLiteral("…"));
    m_thinkingBody->setWordWrap(true);
    m_thinkingBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_thinkingBody->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.muted));
    m_liveThinking = makeCollapsible(QStringLiteral("Thinking…"), m_thinkingBody, false);
    m_liveThinking->setPulsing(true);
    addRow(m_liveThinking, m_p.accent);
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
    if (!m_liveThinking)
        return;
    const QString text = !fullText.isEmpty() ? fullText : m_thinkingText;
    if (m_thinkingBody)
        m_thinkingBody->setText(text.isEmpty() ? QStringLiteral("(thinking)") : text);
    m_liveThinking->setPulsing(false);
    const qint64 secs = m_thinkingStartMs > 0
        ? (QDateTime::currentMSecsSinceEpoch() - m_thinkingStartMs) / 1000
        : 0;
    m_liveThinking->setHeaderText(secs > 0
                                      ? QStringLiteral("Thought for %1s").arg(secs)
                                      : QStringLiteral("Thought"));
    m_liveThinking->setExpanded(false);
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
    auto *c = new Collapsible(header, expanded, m_p, QString());
    if (body)
        c->body()->addWidget(body);
    return c;
}

QWidget *ClaudeTranscriptView::makeBubble(const QString &title, const QString &markdown,
                                          const QString &accent)
{
    auto *frame = new QFrame;
    frame->setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;border-radius:8px;}")
                             .arg(m_p.surface, m_p.border));
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(4);
    if (!title.isEmpty()) {
        auto *h = new QLabel(title);
        h->setStyleSheet(QStringLiteral("color:%1;font-weight:600;background:transparent;border:none;").arg(accent));
        v->addWidget(h);
    }
    auto *body = new CacheLabel(markdown);
    body->setTextFormat(Qt::MarkdownText);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    body->setOpenExternalLinks(true);
    body->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.text));
    v->addWidget(body);
    return frame;
}

// Assistant prose renders as plain, full-width text (no card), matching the
// Claude Code conversation view where only the user's turns are boxed.
bool ClaudeTranscriptView::parseInlineChoices(const QString &markdown, QStringList &options)
{
    // A markdown ordered-list item: "1. ..." or "1) ...", one per line.
    static const QRegularExpression item(
        QStringLiteral("(?m)^[ \\t]{0,3}\\d{1,2}[.)][ \\t]+(.+)$"));
    QStringList found;
    auto it = item.globalMatch(markdown);
    while (it.hasNext())
        found << it.next().captured(1).trimmed();
    // Require at least two options and something that actually reads like a
    // question, so a plain numbered list (e.g. steps in a plan) isn't mistaken
    // for a clarifying question.
    if (found.size() < 2 || !markdown.contains(QLatin1Char('?')))
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
    addRow(l);
    return false;
}

void ClaudeTranscriptView::addUserTurn(const QString &text)
{
    if (m_openInlineChoices)
        lockInlineChoices(m_openInlineChoices); // a reply arrived; stop offering it

    auto *frame = new QFrame;
    frame->setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;"
                                        "border-left:3px solid %3;border-radius:8px;}")
                             .arg(m_p.userBg, m_p.border, m_p.accent));
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(4);
    auto *h = new QLabel(QStringLiteral("you"));
    h->setStyleSheet(QStringLiteral("color:%1;font-weight:600;background:transparent;border:none;").arg(m_p.accent));
    v->addWidget(h);

    // Lift any "Attached image: <path>" lines out of the prose and show each as a
    // small clickable thumbnail (issue #56); the rest renders as plain text.
    static const QRegularExpression imgLine(
        QStringLiteral("^Attached image:\\s*(.+?)\\s*$"));
    QStringList prose;
    QStringList images;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch m = imgLine.match(line);
        if (m.hasMatch() && !QPixmap(m.captured(1)).isNull())
            images << m.captured(1);
        else
            prose << line;
    }

    const QString bodyText = prose.join(QLatin1Char('\n')).trimmed();
    if (!bodyText.isEmpty()) {
        auto *body = new CacheLabel(bodyText);
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        body->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.text));
        v->addWidget(body);
    }
    for (const QString &path : images)
        v->addWidget(new ThumbImage(QPixmap(path), m_p.border), 0, Qt::AlignLeft);
    addRow(frame, m_p.accent);
}

// A tool call: a "Name  subtitle" header over a bordered box whose first row is
// the input (IN); the result lands later as an OUT row in the same box.
void ClaudeTranscriptView::addToolUse(const QString &id, const QString &name,
                                      const QJsonObject &input)
{
    auto *card = new QFrame;
    card->setStyleSheet(QStringLiteral("QFrame{background:transparent;border:none;}"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);
    v->addWidget(dotHeader(name, toolSubtitle(name, input)));

    if (QWidget *inBody = toolBody(name, input)) {
        auto *box = new QFrame;
        box->setStyleSheet(QStringLiteral(
            "QFrame{background:%1;border:1px solid %2;border-radius:8px;}")
            .arg(m_p.surface, m_p.border));
        auto *io = new QVBoxLayout(box);
        io->setContentsMargins(0, 0, 0, 0);
        io->setSpacing(0);
        io->addWidget(ioRow(QStringLiteral("IN"), inBody));
        v->addWidget(box);
        m_toolCards.insert(id, ToolCard{box, io, false});
    } else {
        // Header-only tools (Read/Grep/Glob): the subtitle says it all.
        m_toolCards.insert(id, ToolCard{nullptr, nullptr, false});
    }
    addRow(card, accentFor(name));
}

void ClaudeTranscriptView::addToolResult(const QString &id, const QString &text,
                                         bool isError)
{
    auto it = m_toolCards.find(id);
    if (it == m_toolCards.end() || text.trimmed().isEmpty())
        return;
    ToolCard &tc = it.value();
    if (!tc.box || !tc.io || tc.hasResult)
        return; // header-only tool, or a result already attached
    auto *divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background:%1;border:none;").arg(m_p.border));
    tc.io->addWidget(divider);
    // Peek the output: first/last two lines visible, the middle behind a toggle
    // that says how many lines it hides (errors tinted red).
    QWidget *out = new OutputPeek(text, isError ? m_p.del : m_p.text, m_p);
    tc.io->addWidget(ioRow(isError ? QStringLiteral("ERR") : QStringLiteral("OUT"), out));
    tc.hasResult = true;
}

// Claude Code's AskUserQuestion tool: render an interactive multiple-choice card
// (Anthropic Agent SDK "Handle approvals and user input" shape — a `questions`
// array, each with a question/header/options[label,description]/multiSelect) so
// the user can answer the clarifying question right in the transcript. Each
// option is a checkable button; a free-text "Other" field covers answers the
// options don't. On submit it emits questionAnswered(id, answer); the host turns
// that into a tool_result for the CLI and records a "_local_ask_answer" event so
// the answered state replays on rebuild.
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

    auto *heading = new QLabel(QStringLiteral("Claude has a question"));
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
        QList<QPushButton *> optionButtons;
        QStringList optionLabels;
        QLineEdit *other = nullptr;
    };
    QVector<QState> states;

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

    m_askCards.insert(id, AskCard{controls, status});

    connect(submit, &QPushButton::clicked, this, [this, id, states] {
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
        emit questionAnswered(id, answer);
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
        QString shown = answer;
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
    auto *l = new QLabel(QStringLiteral("%1 Done · %2 turns · %3s · $%4")
                             .arg(err ? QStringLiteral("✗") : QStringLiteral("✓"))
                             .arg(turns)
                             .arg(ms / 1000.0, 0, 'f', 1)
                             .arg(cost, 0, 'f', 4));
    l->setStyleSheet(QStringLiteral("color:%1;font-weight:600;background:transparent;")
                         .arg(err ? m_p.del : m_p.add));
    addRow(l, err ? m_p.del : m_p.add);
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
    return QString();
}

QWidget *ClaudeTranscriptView::toolBody(const QString &name, const QJsonObject &input)
{
    if (name == QLatin1String("Bash"))
        return makeMono(input.value(QStringLiteral("command")).toString(), false);
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
        QString text;
        for (const QJsonValue &tv : input.value(QStringLiteral("todos")).toArray()) {
            const QJsonObject t = tv.toObject();
            const QString st = t.value(QStringLiteral("status")).toString();
            const QString mark = st == QLatin1String("completed") ? QStringLiteral("☑")
                                : st == QLatin1String("in_progress") ? QStringLiteral("◐")
                                                                     : QStringLiteral("☐");
            text += mark + QLatin1Char(' ') + t.value(QStringLiteral("content")).toString()
                    + QLatin1Char('\n');
        }
        return makeMono(text.trimmed(), false);
    }
    if (name == QLatin1String("Read") || name == QLatin1String("Grep")
        || name == QLatin1String("Glob"))
        return nullptr; // the subtitle already says the file/pattern
    if (!input.isEmpty())
        return makeMono(QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)), true);
    return nullptr;
}

// "Name  subtitle" — the timeline rail supplies the coloured node dot.
QWidget *ClaudeTranscriptView::dotHeader(const QString &name, const QString &subtitle)
{
    auto *l = new CacheLabel;
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QString html = QStringLiteral("<span style='color:%1;font-weight:700'>%2</span>")
                       .arg(m_p.text, esc(name));
    if (!subtitle.trimmed().isEmpty())
        html += QStringLiteral("&nbsp;&nbsp;<span style='color:%1'>%2</span>")
                    .arg(m_p.muted, esc(subtitle.trimmed()));
    l->setText(html);
    l->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    return l;
}

// One labelled row inside a tool box: a small uppercase "IN"/"OUT" gutter label
// on the left and the monospace content on the right.
QWidget *ClaudeTranscriptView::ioRow(const QString &label, QWidget *content)
{
    auto *row = new QWidget;
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(10, 8, 10, 8);
    h->setSpacing(10);
    auto *lab = new QLabel(label);
    lab->setFixedWidth(28);
    lab->setStyleSheet(QStringLiteral(
        "color:%1;background:transparent;border:none;font-weight:700;font-size:10px;")
        .arg(m_p.muted));
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
