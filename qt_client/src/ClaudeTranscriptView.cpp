#include "ClaudeTranscriptView.h"

#include <QDateTime>
#include <QEasingCurve>
#include <QFontDatabase>
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
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString esc(const QString &s) { return s.toHtmlEscaped(); }
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
        QObject::connect(m_btn, &QPushButton::clicked, m_btn,
                         [this] { setExpanded(!m_open); });
        m_bodyWidget->setVisible(m_open); // initial state: no animation
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
        m_btn->setText((m_open ? QStringLiteral("▾  ") : QStringLiteral("▸  ")) + m_label);
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
    auto mkBtn = [this](const QString &glyph, const QString &tip) {
        auto *b = new QPushButton(glyph, viewport());
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setFixedSize(30, 30);
        b->hide();
        return b;
    };
    m_toTopBtn = mkBtn(QString::fromUtf8("\xE2\x96\xB2"), QStringLiteral("Jump to top"));
    m_toBottomBtn = mkBtn(QString::fromUtf8("\xE2\x96\xBC"), QStringLiteral("Jump to bottom"));
    connect(m_toTopBtn, &QPushButton::clicked, this, &ClaudeTranscriptView::scrollToTop);
    connect(m_toBottomBtn, &QPushButton::clicked, this,
            &ClaudeTranscriptView::scrollToBottom);

    QScrollBar *sb = verticalScrollBar();
    connect(sb, &QScrollBar::valueChanged, this, [this](int v) {
        QScrollBar *b = verticalScrollBar();
        m_stickBottom = v >= b->maximum() - 4;
        updateScrollButtons();
    });
    connect(sb, &QScrollBar::rangeChanged, this, [this](int, int max) {
        if (m_stickBottom)
            verticalScrollBar()->setValue(max); // keep pinned as content grows
        updateScrollButtons();
    });

    applyScheme();
    if (QStyleHints *h = QGuiApplication::styleHints())
        connect(h, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { applyScheme(); });
}

void ClaudeTranscriptView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    positionScrollButtons();
    if (m_stickBottom)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void ClaudeTranscriptView::positionScrollButtons()
{
    if (!m_toBottomBtn || !m_toTopBtn)
        return;
    const int m = 12, w = m_toBottomBtn->width(), h = m_toBottomBtn->height();
    const int x = viewport()->width() - w - m;
    m_toBottomBtn->move(x, viewport()->height() - h - m);
    m_toTopBtn->move(x, viewport()->height() - 2 * h - m - 6);
    m_toTopBtn->raise();
    m_toBottomBtn->raise();
}

void ClaudeTranscriptView::updateScrollButtons()
{
    QScrollBar *sb = verticalScrollBar();
    const bool scrollable = sb->maximum() > sb->minimum();
    if (m_toBottomBtn)
        m_toBottomBtn->setVisible(scrollable && !m_stickBottom);
    if (m_toTopBtn)
        m_toTopBtn->setVisible(scrollable && sb->value() > sb->minimum() + 4);
    positionScrollButtons();
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
    updateScrollButtons();
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
    if (m_toTopBtn)
        m_toTopBtn->setStyleSheet(btnCss);
    if (m_toBottomBtn)
        m_toBottomBtn->setStyleSheet(btnCss);
}

void ClaudeTranscriptView::clear()
{
    clearActivity();
    m_toolCards.clear();
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
    const bool first = m_col->count() <= 1; // only the trailing spacer present
    auto *item = new RailItem(card, nodeColor.isEmpty() ? m_p.muted : nodeColor,
                              m_p.border, first);
    // Keep the live activity ticker pinned as the last content row: new rows slot
    // in just above it.
    int pos = m_col->count() - 1; // before the trailing spacer
    if (m_activity) {
        const int ai = m_col->indexOf(m_activity);
        if (ai >= 0)
            pos = ai;
    }
    m_col->insertWidget(pos, item);
    fadeIn(item);
    // Follow mode does the scrolling: the scrollbar's rangeChanged handler pins
    // the view to the bottom as the new row expands the content.
    if (!m_stickBottom)
        updateScrollButtons();
    return item;
}

void ClaudeTranscriptView::fadeIn(QWidget *card)
{
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

void ClaudeTranscriptView::handleEvent(const QJsonObject &ev)
{
    const QString type = ev.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("system")) {
        const QString sub = ev.value(QStringLiteral("subtype")).toString();
        if (sub == QLatin1String("init")) {
            auto *l = new QLabel(QStringLiteral("● session started · %1 · %2")
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
        if (out > 0) {
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
        if (cost > 0) {
            m_totalCost = cost; // result carries the run's cumulative cost
            emit statsChanged(m_totalTokens, m_totalCost);
        }
        addResult(ev);
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
    for (const QJsonValue &bv : content) {
        const QJsonObject b = bv.toObject();
        const QString t = b.value(QStringLiteral("type")).toString();
        if (t == QLatin1String("text")) {
            const QString text = b.value(QStringLiteral("text")).toString();
            if (!text.trimmed().isEmpty())
                addAssistantText(text);
        } else if (t == QLatin1String("tool_use")) {
            hadTool = true;
            addToolUse(b.value(QStringLiteral("id")).toString(),
                       b.value(QStringLiteral("name")).toString(),
                       b.value(QStringLiteral("input")).toObject());
        }
    }
    // Tools running => keep the "what it's doing" ticker; a plain reply ends it.
    if (hadTool)
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
    m_thinkingBody = new QLabel(QStringLiteral("…"));
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
    auto *body = new QLabel(markdown);
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
void ClaudeTranscriptView::addAssistantText(const QString &markdown)
{
    auto *l = new QLabel;
    l->setTextFormat(Qt::MarkdownText);
    l->setText(markdown);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    l->setOpenExternalLinks(true);
    l->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.text));
    addRow(l);
}

void ClaudeTranscriptView::addUserTurn(const QString &text)
{
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
        auto *body = new QLabel(bodyText);
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
    QWidget *out = makeMono(text, true);
    if (isError)
        out->setStyleSheet(
            QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.del));
    tc.io->addWidget(ioRow(isError ? QStringLiteral("ERR") : QStringLiteral("OUT"), out));
    tc.hasResult = true;
}

void ClaudeTranscriptView::addResult(const QJsonObject &ev)
{
    const bool err = ev.value(QStringLiteral("is_error")).toBool();
    const double cost = ev.value(QStringLiteral("total_cost_usd")).toDouble();
    const double ms = ev.value(QStringLiteral("duration_ms")).toDouble();
    const int turns = ev.value(QStringLiteral("num_turns")).toInt();
    auto *l = new QLabel(QStringLiteral("%1 done · %2 turns · %3s · $%4")
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
    auto *l = new QLabel;
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

// Monospace content with no panel background — it sits inside a tool card box.
QWidget *ClaudeTranscriptView::makeMono(const QString &text, bool collapsedIfLong)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    const bool longText = collapsedIfLong && lines.size() > 16;
    const QString shown = longText
        ? lines.mid(0, 16).join(QLatin1Char('\n'))
          + QStringLiteral("\n… (%1 more lines)").arg(lines.size() - 16)
        : text;
    auto *l = new QLabel;
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
    const QStringList lines = text.split(QLatin1Char('\n'));
    const bool longText = collapsedIfLong && lines.size() > 16;
    const QString shown = longText
        ? lines.mid(0, 16).join(QLatin1Char('\n'))
          + QStringLiteral("\n… (%1 more lines)").arg(lines.size() - 16)
        : text;
    auto *l = new QLabel;
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

    auto *l = new QLabel(html);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("background:%1;border:none;border-radius:6px;padding:8px 10px;")
                         .arg(m_p.canvas));
    return l;
}
