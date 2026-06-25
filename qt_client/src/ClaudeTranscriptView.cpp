#include "ClaudeTranscriptView.h"

#include <QEasingCurve>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
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

ClaudeTranscriptView::ClaudeTranscriptView(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    m_container = new QWidget;
    m_col = new QVBoxLayout(m_container);
    m_col->setContentsMargins(14, 14, 14, 14);
    m_col->setSpacing(10);
    // A spacer that grows to a viewport height so the newest card can scroll all
    // the way to the top (chat-style "latest pinned up").
    m_bottomSpacer = new QWidget;
    m_col->addWidget(m_bottomSpacer);
    setWidget(m_container);

    applyScheme();
    if (QStyleHints *h = QGuiApplication::styleHints())
        connect(h, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { applyScheme(); });
}

void ClaudeTranscriptView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    if (m_bottomSpacer)
        m_bottomSpacer->setMinimumHeight(qMax(0, viewport()->height() - 80));
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
}

void ClaudeTranscriptView::clear()
{
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
    applyScheme();
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

void ClaudeTranscriptView::addRow(QWidget *card)
{
    m_col->insertWidget(m_col->count() - 1, card); // before the spacer
    fadeIn(card);
    scrollToNewCard(card);
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

void ClaudeTranscriptView::scrollToNewCard(QWidget *card)
{
    QPointer<QWidget> c = card;
    QTimer::singleShot(0, this, [this, c] {
        if (c)
            smoothScrollTo(qMax(0, c->y() - 8));
    });
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

    for (const QJsonValue &bv : content) {
        const QJsonObject b = bv.toObject();
        const QString t = b.value(QStringLiteral("type")).toString();
        if (t == QLatin1String("text")) {
            const QString text = b.value(QStringLiteral("text")).toString();
            if (!text.trimmed().isEmpty())
                addRow(makeBubble(QString(), text, m_p.accent));
        } else if (t == QLatin1String("tool_use")) {
            addToolUse(b.value(QStringLiteral("id")).toString(),
                       b.value(QStringLiteral("name")).toString(),
                       b.value(QStringLiteral("input")).toObject());
        }
    }
}

// ---- thinking lifecycle ----------------------------------------------------

void ClaudeTranscriptView::ensureLiveThinking()
{
    if (m_liveThinking)
        return;
    m_thinkingText.clear();
    m_thinkingTokens = 0;
    m_thinkingBody = new QLabel(QStringLiteral("…"));
    m_thinkingBody->setWordWrap(true);
    m_thinkingBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_thinkingBody->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.muted));
    m_liveThinking = makeCollapsible(QStringLiteral("✦ Thinking…"), m_thinkingBody, false);
    addRow(m_liveThinking);
}

void ClaudeTranscriptView::setThinkingTokens(int tokens)
{
    m_thinkingTokens = tokens;
    if (m_liveThinking)
        m_liveThinking->setHeaderText(
            QStringLiteral("✦ Thinking… ~%1 tokens").arg(QLocale().toString(tokens)));
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
    m_liveThinking->setHeaderText(
        m_thinkingTokens > 0
            ? QStringLiteral("✦ Thought · ~%1 tokens").arg(QLocale().toString(m_thinkingTokens))
            : QStringLiteral("✦ Thought"));
    m_liveThinking->setExpanded(false);
    m_liveThinking = nullptr;
    m_thinkingBody = nullptr;
    m_thinkingText.clear();
    m_thinkingTokens = 0;
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
    auto *body = new QLabel(text);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(m_p.text));
    v->addWidget(body);
    addRow(frame);
}

void ClaudeTranscriptView::addToolUse(const QString &id, const QString &name,
                                      const QJsonObject &input)
{
    const QString sub = toolSubtitle(name, input);
    const QString header = sub.isEmpty() ? name : name + QStringLiteral(" · ") + sub;
    auto *card = new Collapsible(header, true, m_p, accentFor(name));
    if (QWidget *body = toolBody(name, input))
        card->body()->addWidget(body);
    addRow(card);
    m_toolCards.insert(id, ToolCard{card, card->body()});
}

void ClaudeTranscriptView::addToolResult(const QString &id, const QString &text,
                                         bool isError)
{
    auto it = m_toolCards.find(id);
    if (it == m_toolCards.end() || text.trimmed().isEmpty())
        return;
    Collapsible *res = makeCollapsible(
        isError ? QStringLiteral("Result · error") : QStringLiteral("Result"),
        makeCode(text, true), false);
    it.value().body->addWidget(res);
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
    addRow(l);
}

// ---- per-tool rendering ----------------------------------------------------

QString ClaudeTranscriptView::toolSubtitle(const QString &name,
                                           const QJsonObject &input) const
{
    if (name == QLatin1String("Bash"))
        return input.value(QStringLiteral("description")).toString();
    if (name == QLatin1String("Edit") || name == QLatin1String("Write")
        || name == QLatin1String("Read") || name == QLatin1String("MultiEdit")
        || name == QLatin1String("NotebookEdit"))
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
        return makeCode(QStringLiteral("$ ") + input.value(QStringLiteral("command")).toString(), false);
    if (name == QLatin1String("Edit"))
        return makeDiff(input.value(QStringLiteral("old_string")).toString(),
                        input.value(QStringLiteral("new_string")).toString());
    if (name == QLatin1String("Write"))
        return makeCode(input.value(QStringLiteral("content")).toString(), true);
    if (name == QLatin1String("MultiEdit")) {
        auto *holder = new QWidget;
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
        return makeCode(text.trimmed(), false);
    }
    if (name == QLatin1String("Read") || name == QLatin1String("Grep")
        || name == QLatin1String("Glob"))
        return nullptr; // the subtitle already says the file/pattern
    if (!input.isEmpty())
        return makeCode(QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)), true);
    return nullptr;
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

    QString html = QStringLiteral("<div style='font-family:monospace;white-space:pre'>");
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

    auto *l = new QLabel(html);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("background:%1;border:none;border-radius:6px;padding:8px 10px;")
                         .arg(m_p.canvas));
    return l;
}
