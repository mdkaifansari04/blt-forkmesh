#include "StartupSplash.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QEventLoop>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRadialGradient>
#include <QScreen>
#include <QSettings>
#include <QStyleHints>
#include <QTimer>

#include <cmath>

namespace forkmesh::ui {

namespace {

// --- Card geometry, in logical pixels. The widget is the card plus a margin
// the drop shadow is painted into (the window is translucent, so the shadow is
// ours to draw rather than the compositor's).
constexpr qreal kShadowMargin = 30.0;
constexpr qreal kCardWidth = 640.0;
constexpr qreal kCardHeight = 452.0;
constexpr qreal kCardRadius = 14.0;
constexpr qreal kHeaderHeight = 100.0;
constexpr qreal kFooterHeight = 46.0;
constexpr qreal kSidePadding = 22.0;
constexpr qreal kStepRowHeight = 25.0;
constexpr qreal kDetailRowHeight = 21.0;
constexpr qreal kGlyphSize = 15.0;

// A step slower than this is called out in amber. Startup is a sequence of
// things that should each take a few milliseconds; the one that took 900 is
// the whole reason anyone reads this list.
constexpr qint64 kSlowStepMs = 250;

// Hold the finished list on screen briefly so the last tick is actually seen,
// then fade. Both in milliseconds.
constexpr qint64 kFinishHoldMs = 620;
constexpr qint64 kFinishFadeMs = 340;

// Absolute backstop: however wedged startup gets, the splash leaves.
constexpr int kDeadlineMs = 45000;

constexpr qreal kDegToRad = 3.14159265358979323846 / 180.0;
// Qt's arc angles are sixteenths of a degree, counter-clockwise from 3 o'clock.
constexpr int kArcUnit = 16;

const QString kSplashEnabledSetting = QStringLiteral("ui/startupSplash");
const QString kLastStepCountSetting = QStringLiteral("ui/startupSplashSteps");
const QString kLastDurationSetting = QStringLiteral("ui/startupSplashMs");

QPointer<StartupSplash> g_splash;

bool preferDarkSplash()
{
    // Same rule as currentThemeIsDark(), duplicated rather than included:
    // MainWindowInternal.h drags in the whole widget layer, and the splash is
    // constructed before any of it exists.
    const QString pref =
        QSettings().value(QStringLiteral("app/theme"),
                          QStringLiteral("system")).toString();
    if (pref == QLatin1String("light"))
        return false;
    if (pref == QLatin1String("dark"))
        return true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints())
        return QGuiApplication::styleHints()->colorScheme() !=
               Qt::ColorScheme::Light;
#endif
    return true;
}

QColor alpha(const QColor &color, int a)
{
    return QColor(color.red(), color.green(), color.blue(), a);
}

} // namespace

StartupSplash::StartupSplash(bool dark, const QString &version,
                             const QString &commit)
    : QWidget(nullptr,
              Qt::SplashScreen | Qt::FramelessWindowHint |
                  Qt::WindowStaysOnTopHint),
      m_version(version),
      m_commit(commit.left(7))
{
    m_clock.start();
    setAttribute(Qt::WA_TranslucentBackground);
    // Never take focus: the main window is about to appear behind this and
    // should own the keyboard the moment it does.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(int(kCardWidth + 2 * kShadowMargin),
                 int(kCardHeight + 2 * kShadowMargin));

    if (dark) {
        m_palette = {QColor(0, 0, 0),         QColor("#161b22"),
                     QColor("#1b2230"),       QColor("#161b22"),
                     QColor("#30363d"),       QColor("#21262d"),
                     QColor("#e6edf3"),       QColor("#8b949e"),
                     QColor("#6e7681"),       QColor("#21262d"),
                     QColor("#58a6ff"),       QColor("#3fb950"),
                     QColor("#e3b341"),       QColor("#f85149")};
    } else {
        m_palette = {QColor(60, 70, 85),      QColor("#ffffff"),
                     QColor("#f6f8fa"),       QColor("#ffffff"),
                     QColor("#d0d7de"),       QColor("#eaeef2"),
                     QColor("#1f2328"),       QColor("#57606a"),
                     QColor("#8c959f"),       QColor("#eaeef2"),
                     QColor("#0969da"),       QColor("#1a7f37"),
                     QColor("#9a6700"),       QColor("#cf222e")};
    }

    syncFonts();

    // Last launch's shape, so the bar is calibrated rather than guessed. A
    // first run falls back to the seeded defaults.
    QSettings settings;
    const int steps = settings.value(kLastStepCountSetting, 0).toInt();
    const qint64 duration = settings.value(kLastDurationSetting, 0).toLongLong();
    if (steps >= 4 && steps <= 500)
        m_expectedSteps = steps;
    if (duration >= 150 && duration <= 120000)
        m_expectedMs = duration;

    // 60fps while there is an event loop to drive it. Before that (the whole
    // MainWindow constructor) pump() carries the repaints instead, and every
    // animated value reads the same wall clock either way, so the animation
    // simply advances in bigger steps rather than stopping.
    m_animation = new QTimer(this);
    m_animation->setInterval(16);
    QObject::connect(m_animation, &QTimer::timeout, this, [this] {
        if (m_finishAtMs >= 0) {
            const qint64 since = m_clock.elapsed() - m_finishAtMs;
            if (since > kFinishHoldMs) {
                const qreal fade =
                    1.0 - qreal(since - kFinishHoldMs) / qreal(kFinishFadeMs);
                if (fade <= 0.0) {
                    dismiss();
                    return;
                }
                setWindowOpacity(fade);
            }
        }
        update();
    });
    m_animation->start();

    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    m_deadline->setInterval(kDeadlineMs);
    QObject::connect(m_deadline, &QTimer::timeout, this,
                     [this] { dismiss(); });
    m_deadline->start();
}

// --- Step model

void StartupSplash::beginStep(const QString &label)
{
    if (m_finishAtMs >= 0)
        return;
    completeCurrentStep();
    Row row;
    row.text = label;
    row.state = RowState::Running;
    row.startedMs = m_clock.elapsed();
    m_rows.append(row);
    m_runningRow = int(m_rows.size()) - 1;
    pump();
}

void StartupSplash::addDetail(const QString &text)
{
    if (m_finishAtMs >= 0)
        return;
    Row row;
    row.text = text;
    row.state = RowState::Detail;
    row.startedMs = m_clock.elapsed();
    row.endedMs = row.startedMs;
    m_rows.append(row);
    pump();
}

void StartupSplash::completeCurrentStep()
{
    if (m_runningRow < 0 || m_runningRow >= m_rows.size())
        return;
    Row &row = m_rows[m_runningRow];
    row.state = RowState::Done;
    row.endedMs = m_clock.elapsed();
    m_runningRow = -1;
    ++m_completedSteps;
    // Show the tick landing, rather than only when the next step starts — the
    // last step of a phase would otherwise sit spinning until something else
    // happened to repaint.
    pump();
}

void StartupSplash::failCurrentStep(const QString &reason)
{
    if (m_runningRow >= 0 && m_runningRow < m_rows.size()) {
        Row &row = m_rows[m_runningRow];
        row.state = RowState::Failed;
        row.endedMs = m_clock.elapsed();
        m_runningRow = -1;
        ++m_completedSteps;
    }
    if (!reason.isEmpty()) {
        Row row;
        row.text = reason;
        row.state = RowState::Failed;
        row.startedMs = m_clock.elapsed();
        row.endedMs = row.startedMs;
        m_rows.append(row);
    }
    pump();
}

void StartupSplash::finish(const QString &label)
{
    if (m_finishAtMs >= 0)
        return;
    completeCurrentStep();
    if (!label.isEmpty()) {
        Row row;
        row.text = label;
        row.state = RowState::Done;
        row.startedMs = m_clock.elapsed();
        row.endedMs = row.startedMs;
        m_rows.append(row);
        ++m_completedSteps;
    }
    m_finishAtMs = m_clock.elapsed();
    m_shownProgress = 1.0;

    // Calibrate the next launch's progress bar against this one.
    QSettings settings;
    settings.setValue(kLastStepCountSetting, m_completedSteps);
    settings.setValue(kLastDurationSetting, m_finishAtMs);

    if (m_deadline)
        m_deadline->stop();
    pump();
}

void StartupSplash::dismiss()
{
    if (m_animation)
        m_animation->stop();
    if (m_deadline)
        m_deadline->stop();
    hide();
    if (g_splash == this)
        g_splash = nullptr;
    deleteLater();
}

QStringList StartupSplash::transcript() const
{
    QStringList lines;
    lines.reserve(int(m_rows.size()));
    for (const Row &row : m_rows) {
        const QString prefix = row.state == RowState::Detail
                                   ? QStringLiteral("    ")
                                   : QString();
        const QString duration =
            row.endedMs >= 0
                ? QStringLiteral(" (%1)").arg(elapsedText(row.endedMs -
                                                          row.startedMs))
                : QStringLiteral(" (running)");
        lines << prefix + row.text + duration;
    }
    return lines;
}

// --- Repaint pumps

void StartupSplash::pump()
{
    if (!isVisible())
        return;
    // repaint(), not update(): update() only posts an event, and during the
    // MainWindow constructor nothing is ever going to deliver it.
    repaint();
    // Deliver events posted to the splash itself (deferred update/resize work
    // Qt queued during that paint). Scoped to this object on purpose — a full
    // processEvents() here would hand the constructor's own zero-delay
    // singleShots and in-flight replies to a half-built MainWindow.
    QCoreApplication::sendPostedEvents(this, 0);
}

void StartupSplash::pumpEvents()
{
    if (!isVisible())
        return;
    repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 4);
}

void StartupSplash::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    dismiss();
}

// --- Painting

qreal StartupSplash::rowHeight(const Row &row) const
{
    return row.state == RowState::Detail ? kDetailRowHeight : kStepRowHeight;
}

qreal StartupSplash::contentHeight() const
{
    qreal total = 0.0;
    for (const Row &row : m_rows)
        total += rowHeight(row);
    return total;
}

qreal StartupSplash::progressTarget(qint64 now) const
{
    if (m_finishAtMs >= 0)
        return 1.0;
    const qreal byStep =
        qreal(m_completedSteps) / qreal(qMax(m_expectedSteps, 1));
    const qreal byTime = qreal(now) / qreal(qMax<qint64>(m_expectedMs, 1));
    // Whichever estimate is further along wins, so a launch that is slower
    // than last time keeps creeping and one that is faster doesn't lag. Never
    // reaches the end before finish() actually says so.
    return qBound(0.02, qMax(byStep, byTime * 0.92), 0.97);
}

QString StartupSplash::elapsedText(qint64 ms) const
{
    if (ms < 1000)
        return QStringLiteral("%1 ms").arg(ms);
    return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', 2);
}

void StartupSplash::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const qint64 now = m_clock.elapsed();

    syncFonts();
    ensureLogo();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF card(kShadowMargin, kShadowMargin, kCardWidth, kCardHeight);
    ensureShadow();
    if (!m_shadow.isNull())
        p.drawPixmap(0, 0, m_shadow);

    QPainterPath cardPath;
    cardPath.addRoundedRect(card, kCardRadius, kCardRadius);
    p.save();
    p.setClipPath(cardPath);

    QLinearGradient body(card.topLeft(), QPointF(card.left(), card.top() + 160));
    body.setColorAt(0.0, m_palette.headerTop);
    body.setColorAt(1.0, m_palette.card);
    p.fillPath(cardPath, body);

    paintHeader(p, card, now);
    paintProgress(p, card, now);

    const QRectF list(card.left() + kSidePadding,
                      card.top() + kHeaderHeight + 10.0,
                      card.width() - 2 * kSidePadding,
                      card.height() - kHeaderHeight - kFooterHeight - 16.0);
    paintRows(p, list, now);
    paintFooter(p, card);
    p.restore();

    // Hairline border last, so it sits over the fills rather than under them.
    QPen border(m_palette.border);
    border.setWidthF(1.0);
    p.setPen(border);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), kCardRadius,
                      kCardRadius);
}

void StartupSplash::syncFonts()
{
    const QString family = QApplication::font().family();
    if (family == m_fontFamily && !m_fontFamily.isEmpty())
        return;
    m_fontFamily = family;
    const auto sized = [&family](int pixels, QFont::Weight weight) {
        QFont font(family);
        font.setPixelSize(pixels);
        font.setWeight(weight);
        return font;
    };
    m_titleFont = sized(19, QFont::Bold);
    m_subtitleFont = sized(11, QFont::Normal);
    m_rowFont = sized(13, QFont::Normal);
    m_detailFont = sized(12, QFont::Normal);
    m_metaFont = sized(11, QFont::Normal);
}

void StartupSplash::ensureLogo()
{
    const qreal dpr = devicePixelRatioF();
    if (!m_logo.isNull() && qFuzzyCompare(m_logoDpr, dpr))
        return;
    const QPixmap source(QStringLiteral(":/app/forkmesh.png"));
    if (source.isNull()) {
        m_logoDpr = dpr;
        return;
    }
    const int edge = int(30.0 * dpr);
    m_logo = source.scaled(edge, edge, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    m_logo.setDevicePixelRatio(dpr);
    m_logoDpr = dpr;
}

void StartupSplash::ensureShadow()
{
    const qreal dpr = devicePixelRatioF();
    if (!m_shadow.isNull() && qFuzzyCompare(m_shadowDpr, dpr))
        return;
    m_shadow = QPixmap(int(width() * dpr), int(height() * dpr));
    m_shadow.setDevicePixelRatio(dpr);
    m_shadow.fill(Qt::transparent);
    QPainter shadow(&m_shadow);
    shadow.setRenderHint(QPainter::Antialiasing, true);
    paintShadow(shadow,
                QRectF(kShadowMargin, kShadowMargin, kCardWidth, kCardHeight));
    m_shadowDpr = dpr;
}

void StartupSplash::paintShadow(QPainter &p, const QRectF &card) const
{
    // Concentric rounded rects with falling alpha. Cheaper and more predictable
    // than QGraphicsDropShadowEffect on a translucent top-level window, which
    // several compositors render badly or not at all.
    p.setPen(Qt::NoPen);
    for (int i = int(kShadowMargin); i > 0; i -= 2) {
        const qreal spread = qreal(i);
        const int a = int(46.0 * std::pow(1.0 - spread / kShadowMargin, 2.2));
        if (a <= 0)
            continue;
        p.setBrush(alpha(m_palette.shadow, a));
        p.drawRoundedRect(card.adjusted(-spread, -spread + 3.0, spread,
                                        spread + 3.0),
                          kCardRadius + spread, kCardRadius + spread);
    }
}

void StartupSplash::paintHeader(QPainter &p, const QRectF &card,
                                qint64 now) const
{
    const QPointF mark(card.left() + 58.0, card.top() + 46.0);
    paintMeshOrbit(p, mark, now);

    if (!m_logo.isNull()) {
        // The app mark is a square raster with an opaque backdrop; clipped to a
        // disc it sits inside the orbit as a coin rather than a square with
        // corners poking at the ring.
        const QSizeF logical = QSizeF(m_logo.size()) / m_logo.devicePixelRatio();
        const qreal radius = logical.width() / 2.0;
        p.save();
        QPainterPath disc;
        disc.addEllipse(mark, radius, radius);
        p.setClipPath(disc);
        p.drawPixmap(QPointF(mark.x() - radius, mark.y() - radius), m_logo);
        p.restore();
        // A hairline keeps the coin from bleeding into the card on whichever
        // theme happens to match its backdrop.
        p.setPen(QPen(alpha(m_palette.border, 170), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(mark, radius, radius);
    }

    const qreal textLeft = card.left() + 104.0;
    p.setFont(m_titleFont);
    p.setPen(m_palette.text);
    p.drawText(QPointF(textLeft, card.top() + 44.0), QStringLiteral("ForkMesh"));

    p.setFont(m_subtitleFont);
    p.setPen(m_finishAtMs >= 0 ? m_palette.ok : m_palette.muted);
    p.drawText(QPointF(textLeft, card.top() + 62.0),
               m_finishAtMs >= 0
                   ? QStringLiteral("Ready")
                   : QStringLiteral("Starting the desktop node…"));

    // Total elapsed, large and right-aligned: the number anyone opening a
    // slow launch actually wants.
    const qint64 shown = m_finishAtMs >= 0 ? m_finishAtMs : now;
    QFont clock = m_titleFont;
    clock.setWeight(QFont::Normal);
    p.setFont(clock);
    p.setPen(m_finishAtMs >= 0 ? m_palette.ok : m_palette.muted);
    const QRectF clockBox(card.right() - 160.0, card.top() + 24.0, 136.0, 24.0);
    p.drawText(clockBox, Qt::AlignRight | Qt::AlignVCenter, elapsedText(shown));
    p.setFont(m_metaFont);
    p.setPen(m_palette.faint);
    p.drawText(QRectF(card.right() - 160.0, card.top() + 48.0, 136.0, 14.0),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("elapsed"));
}

void StartupSplash::paintMeshOrbit(QPainter &p, const QPointF &centre,
                                   qint64 now) const
{
    const bool done = m_finishAtMs >= 0;
    const QColor accent = done ? m_palette.ok : m_palette.accent;

    // Breathing glow behind the mark.
    const qreal pulse = 0.5 + 0.5 * std::sin(qreal(now) / 380.0);
    QRadialGradient glow(centre, 34.0);
    glow.setColorAt(0.0, alpha(accent, int(34 + 30 * pulse)));
    glow.setColorAt(0.62, alpha(accent, int(12 + 12 * pulse)));
    glow.setColorAt(1.0, alpha(accent, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(centre, 34.0, 34.0);

    p.setBrush(Qt::NoBrush);

    // Full faint track, so the ring always reads as a closed circle.
    QPen track(alpha(accent, 46));
    track.setWidthF(2.4);
    p.setPen(track);
    p.drawEllipse(centre, 30.0, 30.0);

    const QRectF outer(centre.x() - 30.0, centre.y() - 30.0, 60.0, 60.0);
    const QRectF inner(centre.x() - 23.0, centre.y() - 23.0, 46.0, 46.0);

    // Two arcs at different speeds and directions; negating an angle runs its
    // sweep clockwise. On finish both close into full circles.
    QPen sweep(accent);
    sweep.setWidthF(2.4);
    sweep.setCapStyle(Qt::RoundCap);
    p.setPen(sweep);
    p.drawArc(outer, int(-(now * 0.20)) % 360 * kArcUnit,
              (done ? 360 : 105) * kArcUnit);

    QPen counter(alpha(accent, 130));
    counter.setWidthF(1.8);
    counter.setCapStyle(Qt::RoundCap);
    p.setPen(counter);
    p.drawArc(inner, int(now * 0.13) % 360 * kArcUnit,
              (done ? 360 : 78) * kArcUnit);

    // Three mesh peers orbiting the mark — the app is, after all, joining a
    // network of them.
    const QColor peers[3] = {accent, m_palette.ok, QColor("#bc8cff")};
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 3; ++i) {
        const qreal angle = (qreal(now) * 0.075 + i * 120.0) * kDegToRad;
        const QPointF dot(centre.x() + 30.0 * std::cos(angle),
                          centre.y() - 30.0 * std::sin(angle));
        p.setBrush(alpha(peers[i], 90));
        p.drawEllipse(dot, 4.6, 4.6);
        p.setBrush(peers[i]);
        p.drawEllipse(dot, 2.6, 2.6);
    }
}

void StartupSplash::paintProgress(QPainter &p, const QRectF &card, qint64 now)
{
    // Time-based easing rather than per-frame: paints are regular once the
    // event loop runs but arrive only on step changes before it does, and the
    // bar should travel the same distance per millisecond either way.
    const qreal target = progressTarget(now);
    const qint64 delta = qMax<qint64>(0, now - m_lastPaintMs);
    m_lastPaintMs = now;
    m_shownProgress +=
        (target - m_shownProgress) * qMin(1.0, qreal(delta) / 220.0);
    m_shownProgress = qBound(0.0, m_shownProgress, 1.0);

    const QRectF bar(card.left() + kSidePadding, card.top() + kHeaderHeight - 12.0,
                     card.width() - 2 * kSidePadding, 4.0);
    p.setPen(Qt::NoPen);
    p.setBrush(m_palette.track);
    p.drawRoundedRect(bar, 2.0, 2.0);

    const qreal filledWidth = bar.width() * m_shownProgress;
    if (filledWidth > 1.0) {
        const QRectF filled(bar.left(), bar.top(), filledWidth, bar.height());
        const QColor head = m_finishAtMs >= 0 ? m_palette.ok : m_palette.accent;
        QLinearGradient fill(filled.topLeft(), filled.topRight());
        fill.setColorAt(0.0, alpha(head, 150));
        fill.setColorAt(1.0, head);
        p.setBrush(fill);
        p.drawRoundedRect(filled, 2.0, 2.0);

        // A highlight sweeping along the filled portion, so the bar still
        // reads as live during a long step that isn't moving it.
        if (m_finishAtMs < 0) {
            p.save();
            QPainterPath clip;
            clip.addRoundedRect(filled, 2.0, 2.0);
            p.setClipPath(clip);
            const qreal span = filled.width() + 160.0;
            const qreal x =
                filled.left() + std::fmod(qreal(now) * 0.34, span) - 80.0;
            const QColor sheen(255, 255, 255);
            QLinearGradient shimmer(QPointF(x, 0), QPointF(x + 80.0, 0));
            shimmer.setColorAt(0.0, alpha(sheen, 0));
            shimmer.setColorAt(0.5, alpha(sheen, 70));
            shimmer.setColorAt(1.0, alpha(sheen, 0));
            p.fillRect(filled, shimmer);
            p.restore();
        }
    }

    p.setPen(QPen(m_palette.separator, 1.0));
    p.drawLine(QPointF(card.left(), card.top() + kHeaderHeight),
               QPointF(card.right(), card.top() + kHeaderHeight));
}

void StartupSplash::paintRows(QPainter &p, const QRectF &list, qint64 now) const
{
    p.save();
    p.setClipRect(list);

    // Always pinned to the newest row.
    const qreal offset = qMax(0.0, contentHeight() - list.height());
    qreal y = list.top() - offset;

    const QFontMetricsF stepMetrics(m_rowFont);
    const QFontMetricsF detailMetrics(m_detailFont);
    const QFontMetricsF metaMetrics(m_metaFont);

    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        const qreal height = rowHeight(row);
        const QRectF band(list.left(), y, list.width(), height);
        y += height;
        if (band.bottom() < list.top() - 2.0 || band.top() > list.bottom() + 2.0)
            continue;

        const bool detail = row.state == RowState::Detail;
        const qreal indent = detail ? 22.0 : 0.0;
        const QRectF glyph(band.left() + indent + 3.0,
                           band.center().y() - kGlyphSize / 2.0, kGlyphSize,
                           kGlyphSize);
        paintRowGlyph(p, glyph, row, now);

        // Elapsed on the right: live for the running step, final for the rest,
        // amber when a step took long enough to be worth noticing.
        QString duration;
        QColor durationColor = m_palette.faint;
        if (row.state == RowState::Running) {
            duration = elapsedText(now - row.startedMs);
            durationColor = m_palette.accent;
        } else if (!detail && row.endedMs >= 0) {
            const qint64 took = row.endedMs - row.startedMs;
            if (took >= 1) {
                duration = elapsedText(took);
                if (took >= kSlowStepMs)
                    durationColor = m_palette.warn;
            }
        }
        qreal durationWidth = 0.0;
        if (!duration.isEmpty()) {
            p.setFont(m_metaFont);
            p.setPen(durationColor);
            durationWidth = metaMetrics.horizontalAdvance(duration) + 12.0;
            p.drawText(QRectF(list.right() - durationWidth + 12.0, band.top(),
                              durationWidth - 12.0, height),
                       Qt::AlignRight | Qt::AlignVCenter, duration);
        }

        const qreal textLeft = glyph.right() + 9.0;
        const qreal textWidth =
            qMax(20.0, list.right() - durationWidth - textLeft);
        QColor textColor;
        switch (row.state) {
        case RowState::Running:
            textColor = m_palette.text;
            break;
        case RowState::Failed:
            textColor = m_palette.bad;
            break;
        case RowState::Detail:
            textColor = m_palette.faint;
            break;
        case RowState::Done:
            textColor = m_palette.muted;
            break;
        }
        QFont font = detail ? m_detailFont : m_rowFont;
        if (row.state == RowState::Running)
            font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.setPen(textColor);
        const QFontMetricsF &metrics = detail ? detailMetrics : stepMetrics;
        p.drawText(QRectF(textLeft, band.top(), textWidth, height),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   metrics.elidedText(row.text, Qt::ElideRight, textWidth));
    }

    // Fade rows out at the top edge as they scroll away, rather than slicing
    // them off mid-glyph.
    if (offset > 0.0) {
        QLinearGradient fade(list.topLeft(),
                             QPointF(list.left(), list.top() + 26.0));
        fade.setColorAt(0.0, m_palette.card);
        fade.setColorAt(1.0, alpha(m_palette.card, 0));
        p.fillRect(QRectF(list.left(), list.top(), list.width(), 26.0), fade);
    }
    p.restore();
}

void StartupSplash::paintRowGlyph(QPainter &p, const QRectF &box,
                                  const Row &row, qint64 now) const
{
    const QPointF centre = box.center();
    p.setBrush(Qt::NoBrush);
    switch (row.state) {
    case RowState::Running: {
        // The same ring as the header mark, in miniature, on the same clock.
        QPen track(alpha(m_palette.accent, 60));
        track.setWidthF(1.8);
        p.setPen(track);
        p.drawEllipse(centre, 5.6, 5.6);
        QPen arc(m_palette.accent);
        arc.setWidthF(1.8);
        arc.setCapStyle(Qt::RoundCap);
        p.setPen(arc);
        p.drawArc(QRectF(centre.x() - 5.6, centre.y() - 5.6, 11.2, 11.2),
                  int(-(now * 0.30)) % 360 * 16, 100 * 16);
        break;
    }
    case RowState::Done: {
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(m_palette.ok, 42));
        p.drawEllipse(centre, 6.4, 6.4);
        QPen tick(m_palette.ok);
        tick.setWidthF(1.7);
        tick.setCapStyle(Qt::RoundCap);
        tick.setJoinStyle(Qt::RoundJoin);
        p.setPen(tick);
        p.setBrush(Qt::NoBrush);
        QPainterPath check;
        check.moveTo(centre.x() - 3.1, centre.y() + 0.2);
        check.lineTo(centre.x() - 0.9, centre.y() + 2.5);
        check.lineTo(centre.x() + 3.3, centre.y() - 2.5);
        p.drawPath(check);
        break;
    }
    case RowState::Failed: {
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(m_palette.bad, 46));
        p.drawEllipse(centre, 6.4, 6.4);
        QPen cross(m_palette.bad);
        cross.setWidthF(1.7);
        cross.setCapStyle(Qt::RoundCap);
        p.setPen(cross);
        p.drawLine(QPointF(centre.x() - 2.8, centre.y() - 2.8),
                   QPointF(centre.x() + 2.8, centre.y() + 2.8));
        p.drawLine(QPointF(centre.x() + 2.8, centre.y() - 2.8),
                   QPointF(centre.x() - 2.8, centre.y() + 2.8));
        break;
    }
    case RowState::Detail: {
        p.setPen(Qt::NoPen);
        p.setBrush(alpha(m_palette.faint, 150));
        p.drawEllipse(centre, 1.9, 1.9);
        break;
    }
    }
}

void StartupSplash::paintFooter(QPainter &p, const QRectF &card) const
{
    const qreal top = card.bottom() - kFooterHeight;
    p.setPen(QPen(m_palette.separator, 1.0));
    p.drawLine(QPointF(card.left(), top), QPointF(card.right(), top));

    const QRectF band(card.left() + kSidePadding, top, card.width() - 2 * kSidePadding,
                      kFooterHeight);

    QString status;
    QColor statusColor = m_palette.muted;
    if (m_finishAtMs >= 0) {
        status = QStringLiteral("Ready — %1 steps in %2")
                     .arg(m_completedSteps)
                     .arg(elapsedText(m_finishAtMs));
        statusColor = m_palette.ok;
    } else if (m_runningRow >= 0 && m_runningRow < m_rows.size()) {
        status = m_rows.at(m_runningRow).text;
    } else {
        status = QStringLiteral("Working…");
    }

    QString build = m_version.isEmpty() ? QString()
                                        : QStringLiteral("v") + m_version;
    if (!m_commit.isEmpty() && m_commit != QLatin1String("unknown"))
        build += QStringLiteral(" · ") + m_commit;

    p.setFont(m_metaFont);
    const QFontMetricsF metrics(m_metaFont);
    const qreal buildWidth =
        build.isEmpty() ? 0.0 : metrics.horizontalAdvance(build) + 16.0;
    p.setPen(m_palette.faint);
    if (!build.isEmpty())
        p.drawText(band, Qt::AlignRight | Qt::AlignVCenter, build);
    const qreal statusWidth = qMax(40.0, band.width() - buildWidth);
    p.setPen(statusColor);
    p.drawText(QRectF(band.left(), band.top(), statusWidth, band.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               metrics.elidedText(status, Qt::ElideRight, statusWidth));
}

// --- Process-wide splash

StartupSplash *activeStartupSplash()
{
    return g_splash.data();
}

StartupSplash *showStartupSplash(bool headless, const QString &version,
                                 const QString &commit)
{
    if (g_splash)
        return g_splash.data();
    // No splash where there is nobody to see it, or where the user said no.
    if (headless || !qobject_cast<QApplication *>(QCoreApplication::instance()))
        return nullptr;
    if (qEnvironmentVariableIsSet("FORKMESH_NO_SPLASH"))
        return nullptr;
    if (!QSettings().value(kSplashEnabledSetting, true).toBool())
        return nullptr;

    auto *splash = new StartupSplash(preferDarkSplash(), version, commit);
    // Centred on the screen the pointer is on — on a multi-head desk that is
    // the one the user just double-clicked the icon on, which is not
    // necessarily the primary.
    const QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        splash->move(available.center() -
                     QPoint(splash->width() / 2, splash->height() / 2));
    }
    splash->show();
    g_splash = splash;
    // Two bounded passes so the window is mapped and has painted once before
    // the caller starts blocking work. Safe here: this runs from main() before
    // MainWindow exists, so there is no half-built state to deliver events to.
    splash->pumpEvents();
    splash->pumpEvents();
    return splash;
}

void startupStep(const QString &label)
{
    if (g_splash)
        g_splash->beginStep(label);
}

void startupDetail(const QString &text)
{
    if (g_splash)
        g_splash->addDetail(text);
}

void startupStepFailed(const QString &reason)
{
    if (g_splash)
        g_splash->failCurrentStep(reason);
}

void finishStartupSplash(const QString &label)
{
    if (g_splash)
        g_splash->finish(label);
}

void dismissStartupSplash()
{
    if (g_splash)
        g_splash->dismiss();
}

} // namespace forkmesh::ui
