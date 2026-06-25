#include "ClaudeTranscriptView.h"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// GitHub-dark palette, matching Theme.h.
const char *kCanvas = "#0d1117";
const char *kSurface = "#161b22";
const char *kBorder = "#30363d";
const char *kText = "#e6edf3";
const char *kMuted = "#8b949e";
const char *kAccent = "#58a6ff";
const char *kAdd = "#3fb950";
const char *kDel = "#f85149";

QString esc(const QString &s) { return s.toHtmlEscaped(); }

QLabel *monoLabel(const QString &richOrPlain, bool rich)
{
    auto *l = new QLabel(richOrPlain);
    l->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return l;
}
} // namespace

ClaudeTranscriptView::ClaudeTranscriptView(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral("QScrollArea{background:%1;border:none;}").arg(kCanvas));

    m_container = new QWidget;
    m_container->setStyleSheet(QStringLiteral("background:%1;").arg(kCanvas));
    m_col = new QVBoxLayout(m_container);
    m_col->setContentsMargins(14, 14, 14, 14);
    m_col->setSpacing(10);
    m_col->addStretch(1);
    setWidget(m_container);
}

void ClaudeTranscriptView::clear()
{
    m_toolCards.clear();
    // Remove every row but keep the trailing stretch.
    while (m_col->count() > 1) {
        QLayoutItem *it = m_col->takeAt(0);
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }
}

void ClaudeTranscriptView::addRow(QWidget *card)
{
    // Insert before the stretch (which is always the last item).
    m_col->insertWidget(m_col->count() - 1, card);
    scrollToBottom();
}

void ClaudeTranscriptView::scrollToBottom()
{
    // Defer so the layout has sized the new card first.
    QTimer::singleShot(0, this, [this] {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    });
}

// ---- event dispatch --------------------------------------------------------

void ClaudeTranscriptView::handleEvent(const QJsonObject &ev)
{
    const QString type = ev.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("system")) {
        if (ev.value(QStringLiteral("subtype")).toString() == QLatin1String("init")) {
            const QString model = ev.value(QStringLiteral("model")).toString();
            const QString cwd = ev.value(QStringLiteral("cwd")).toString();
            auto *l = new QLabel(QStringLiteral("● session started · %1 · %2")
                                     .arg(esc(model), esc(cwd)));
            l->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(kMuted));
            addRow(l);
        }
    } else if (type == QLatin1String("assistant")) {
        addAssistantBlocks(ev.value(QStringLiteral("message")).toObject());
    } else if (type == QLatin1String("user")) {
        const QJsonArray content =
            ev.value(QStringLiteral("message")).toObject()
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
        const double util = info.value(QStringLiteral("utilization")).toDouble();
        const QString kind = info.value(QStringLiteral("rateLimitType")).toString()
                                 .contains(QStringLiteral("seven"))
                             ? QStringLiteral("weekly") : QStringLiteral("session");
        emit usageChanged(QStringLiteral("You've used %1% of your %2 limit")
                              .arg(qRound(util * 100)).arg(kind));
    } else if (type == QLatin1String("result")) {
        addResult(ev);
    }
}

void ClaudeTranscriptView::addAssistantBlocks(const QJsonObject &message)
{
    for (const QJsonValue &bv : message.value(QStringLiteral("content")).toArray()) {
        const QJsonObject b = bv.toObject();
        const QString t = b.value(QStringLiteral("type")).toString();
        if (t == QLatin1String("text")) {
            const QString text = b.value(QStringLiteral("text")).toString();
            if (!text.trimmed().isEmpty())
                addRow(makeBubble(QString(), text, kAccent));
        } else if (t == QLatin1String("thinking")) {
            addThinking(b.value(QStringLiteral("thinking")).toString());
        } else if (t == QLatin1String("tool_use")) {
            addToolUse(b.value(QStringLiteral("id")).toString(),
                       b.value(QStringLiteral("name")).toString(),
                       b.value(QStringLiteral("input")).toObject());
        }
    }
}

// ---- bubbles & cards -------------------------------------------------------

QWidget *ClaudeTranscriptView::makeBubble(const QString &title, const QString &markdown,
                                          const QString &accent)
{
    auto *frame = new QFrame;
    frame->setStyleSheet(QStringLiteral(
        "QFrame{background:%1;border:1px solid %2;border-radius:8px;}")
                             .arg(kSurface, kBorder));
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
    body->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(kText));
    v->addWidget(body);
    return frame;
}

void ClaudeTranscriptView::addUserTurn(const QString &text)
{
    auto *frame = new QFrame;
    frame->setStyleSheet(QStringLiteral(
        "QFrame{background:#1f2630;border:1px solid %1;border-radius:8px;}").arg(kBorder));
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(4);
    auto *h = new QLabel(QStringLiteral("you"));
    h->setStyleSheet(QStringLiteral("color:%1;font-weight:600;background:transparent;border:none;").arg(kMuted));
    v->addWidget(h);
    auto *body = new QLabel(text);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;").arg(kText));
    v->addWidget(body);
    addRow(frame);
}

void ClaudeTranscriptView::addThinking(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    auto *frame = new QFrame;
    frame->setStyleSheet(QStringLiteral("QFrame{background:transparent;border:none;}"));
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    auto *toggle = new QPushButton(QStringLiteral("✦ Thought  ▸"));
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setStyleSheet(QStringLiteral(
        "QPushButton{color:%1;background:transparent;border:none;text-align:left;font-style:italic;}")
                              .arg(kMuted));
    auto *bodyLabel = monoLabel(text, false);
    bodyLabel->setStyleSheet(QStringLiteral(
        "color:%1;background:%2;border-left:2px solid %3;padding:6px 10px;")
                                 .arg(kMuted, kSurface, kBorder));
    bodyLabel->setVisible(false);
    connect(toggle, &QPushButton::clicked, this, [toggle, bodyLabel] {
        const bool show = !bodyLabel->isVisible();
        bodyLabel->setVisible(show);
        toggle->setText(show ? QStringLiteral("✦ Thought  ▾") : QStringLiteral("✦ Thought  ▸"));
    });
    v->addWidget(toggle);
    v->addWidget(bodyLabel);
    addRow(frame);
}

void ClaudeTranscriptView::addToolUse(const QString &id, const QString &name,
                                      const QJsonObject &input)
{
    auto *frame = new QFrame;
    frame->setStyleSheet(QStringLiteral(
        "QFrame{background:%1;border:1px solid %2;border-radius:8px;}")
                             .arg(kSurface, kBorder));
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(6);

    auto *header = new QLabel(QStringLiteral("<b style='color:%1'>%2</b>  <span style='color:%3'>%4</span>")
                                  .arg(kAccent, esc(name), kMuted, esc(toolSubtitle(name, input))));
    header->setTextFormat(Qt::RichText);
    header->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(header);

    if (QWidget *body = toolBody(name, input))
        v->addWidget(body);

    addRow(frame);
    m_toolCards.insert(id, ToolCard{frame, v});
}

void ClaudeTranscriptView::addToolResult(const QString &id, const QString &text,
                                         bool isError)
{
    auto it = m_toolCards.find(id);
    if (it == m_toolCards.end() || text.trimmed().isEmpty())
        return;
    QWidget *code = makeCode(text, true);
    if (isError)
        code->setStyleSheet(code->styleSheet() +
                            QStringLiteral("color:%1;").arg(kDel));
    it.value().body->addWidget(code);
    scrollToBottom();
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
                         .arg(err ? kDel : kAdd));
    addRow(l);
}

// ---- per-tool rendering ----------------------------------------------------

QString ClaudeTranscriptView::toolSubtitle(const QString &name,
                                           const QJsonObject &input) const
{
    if (name == QLatin1String("Bash"))
        return input.value(QStringLiteral("description")).toString();
    if (name == QLatin1String("Edit") || name == QLatin1String("Write")
        || name == QLatin1String("Read") || name == QLatin1String("MultiEdit"))
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
        QString md;
        for (const QJsonValue &tv : input.value(QStringLiteral("todos")).toArray()) {
            const QJsonObject t = tv.toObject();
            const QString st = t.value(QStringLiteral("status")).toString();
            const QString mark = st == QLatin1String("completed") ? QStringLiteral("☑")
                                : st == QLatin1String("in_progress") ? QStringLiteral("◐")
                                                                     : QStringLiteral("☐");
            md += mark + QLatin1Char(' ')
                  + t.value(QStringLiteral("content")).toString() + QLatin1Char('\n');
        }
        return makeCode(md.trimmed(), false);
    }
    if (name == QLatin1String("Read") || name == QLatin1String("Grep")
        || name == QLatin1String("Glob"))
        return nullptr; // subtitle already says the file/pattern
    // Fallback: compact JSON of the input.
    if (!input.isEmpty())
        return makeCode(QString::fromUtf8(
                            QJsonDocument(input).toJson(QJsonDocument::Compact)), true);
    return nullptr;
}

QWidget *ClaudeTranscriptView::makeCode(const QString &text, bool collapsedIfLong)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    const bool longText = collapsedIfLong && lines.size() > 16;
    const QString shown = longText
        ? lines.mid(0, 16).join(QLatin1Char('\n'))
          + QStringLiteral("\n… (%1 more lines)").arg(lines.size() - 16)
        : text;
    auto *l = monoLabel(esc(shown), false);
    l->setStyleSheet(QStringLiteral(
        "color:%1;background:%2;border:none;border-radius:6px;padding:8px 10px;")
                         .arg(kText, kCanvas));
    return l;
}

QWidget *ClaudeTranscriptView::makeDiff(const QString &oldText, const QString &newText)
{
    QStringList oldL = oldText.split(QLatin1Char('\n'));
    QStringList newL = newText.split(QLatin1Char('\n'));
    // Trim common leading/trailing context so the diff shows only the change.
    int pre = 0;
    while (pre < oldL.size() && pre < newL.size() && oldL[pre] == newL[pre])
        ++pre;
    int suf = 0;
    while (suf < oldL.size() - pre && suf < newL.size() - pre
           && oldL[oldL.size() - 1 - suf] == newL[newL.size() - 1 - suf])
        ++suf;

    QString html = QStringLiteral("<div style='font-family:monospace;white-space:pre'>");
    auto context = [&](const QString &s) {
        html += QStringLiteral("<div style='color:%1'>  %2</div>").arg(kMuted, esc(s));
    };
    for (int i = 0; i < pre; ++i)
        context(oldL[i]);
    for (int i = pre; i < oldL.size() - suf; ++i)
        html += QStringLiteral("<div style='color:%1;background:#2d1416'>- %2</div>")
                    .arg(kDel, esc(oldL[i]));
    for (int i = pre; i < newL.size() - suf; ++i)
        html += QStringLiteral("<div style='color:%1;background:#12261a'>+ %2</div>")
                    .arg(kAdd, esc(newL[i]));
    for (int i = oldL.size() - suf; i < oldL.size(); ++i)
        context(oldL[i]);
    html += QStringLiteral("</div>");

    auto *l = monoLabel(html, true);
    l->setStyleSheet(QStringLiteral("background:%1;border:none;border-radius:6px;padding:8px 10px;")
                         .arg(kCanvas));
    return l;
}
