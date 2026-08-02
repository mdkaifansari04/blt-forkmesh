#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QTimer;

namespace forkmesh::ui {

// Launch splash (adhoc #39). Everything ForkMesh does before its window can
// paint — restoring servers, building the two big repository surfaces, loading
// the repo list, arming timers — happens inside MainWindow's constructor, with
// no event loop running. Until now that was a second or two of nothing at all
// on screen (or, on a slow disk, considerably more), and the only account of
// what was going on went to the terminal via logStartup().
//
// This is that account, on screen: a centred card with a live mesh animation
// and the running list of startup steps, each one ticking its own elapsed time
// and settling into a tick when it finishes.
//
// Two constraints shape the implementation:
//
//  * No event loop. The splash cannot rely on timers to animate or on posted
//    paint events to update while the constructor blocks the GUI thread, so
//    every animated value is a pure function of the wall clock (see
//    animationClock()) and pump() forces a synchronous repaint(). The same
//    paint code then animates smoothly off a 16ms timer once the loop starts.
//    We deliberately never call processEvents() from inside the constructor:
//    that would deliver the ctor's own zero-delay singleShots (startDiagnostics,
//    maybeAutoStartDirectMirrorServices) and in-flight favicon replies into a
//    half-built MainWindow.
//
//  * No child widgets. The whole card is painted in one paintEvent, so a
//    repaint() is one pass with no layout, and the global stylesheet (which
//    matches plain QWidget) cannot paint over the translucent background.
class StartupSplash : public QWidget
{
public:
    StartupSplash(bool dark, const QString &version, const QString &commit);

    // Announce work that is about to happen. Closes whichever step is running
    // and starts a new one, which then shows a live elapsed counter.
    void beginStep(const QString &label);
    // A sub-line under the running step, for the things a step does inside
    // itself ("Code tab built in 214ms"). Never closes the step.
    void addDetail(const QString &text);
    // Close the running step, if any, without starting another.
    void completeCurrentStep();
    // Close the running step as failed and leave the reason on screen.
    void failCurrentStep(const QString &reason);
    // Last step, then hold on the completed list for a beat and fade out.
    void finish(const QString &label);
    // Fade skipped: leave the screen immediately (a bail-out path — a second
    // instance, the root refusal — is about to show a dialog or exit).
    void dismiss();

    bool isFinishing() const { return m_finishAtMs >= 0; }
    int stepCount() const { return m_completedSteps; }
    // Flat text of every row, in order. Diagnostics/tests.
    QStringList transcript() const;

    // Force a synchronous repaint. Safe with no event loop running.
    void pump();
    // Additionally drain posted events. Only safe where the caller knows no
    // half-built state is reachable — main() uses it around its own steps, the
    // MainWindow constructor must not.
    void pumpEvents();

protected:
    void paintEvent(QPaintEvent *event) override;
    // Click anywhere to dismiss, so a wedged startup can never trap the splash
    // on top of the user's screen.
    void mousePressEvent(QMouseEvent *event) override;

private:
    enum class RowState { Running, Done, Failed, Detail };

    struct Row {
        QString text;
        RowState state = RowState::Running;
        qint64 startedMs = 0;
        qint64 endedMs = -1; // still running while negative
    };

    struct Palette {
        QColor shadow, card, headerTop, headerBottom, border, separator;
        QColor text, muted, faint, track, accent, ok, warn, bad;
    };

    // Rasterise the app mark at size x devicePixelRatio and tag the pixmap with
    // that ratio, or HiDPI upscales a 34px raster into mush.
    // The splash is created before main() picks the app's UI family, so the
    // fonts are rebuilt whenever QApplication's family changes under us.
    void syncFonts();
    void ensureLogo();
    // The drop shadow is a stack of translucent rounded rects and never
    // changes, so it is rasterised once and blitted. Startup repaints the card
    // on every step, and re-running that stack each time was the only part of
    // the paint with a cost worth caring about.
    void ensureShadow();
    void paintShadow(QPainter &p, const QRectF &card) const;
    void paintHeader(QPainter &p, const QRectF &card, qint64 now) const;
    void paintMeshOrbit(QPainter &p, const QPointF &centre, qint64 now) const;
    void paintProgress(QPainter &p, const QRectF &card, qint64 now);
    void paintRows(QPainter &p, const QRectF &list, qint64 now) const;
    void paintRowGlyph(QPainter &p, const QRectF &box, const Row &row,
                       qint64 now) const;
    void paintFooter(QPainter &p, const QRectF &card) const;

    qreal rowHeight(const Row &row) const;
    qreal contentHeight() const;
    // 0..1, eased toward the live estimate so it never jumps or stalls.
    qreal progressTarget(qint64 now) const;
    QString elapsedText(qint64 ms) const;

    Palette m_palette;
    QString m_version;
    QString m_commit;
    QPixmap m_logo;
    qreal m_logoDpr = 0.0;
    QPixmap m_shadow;
    qreal m_shadowDpr = 0.0;
    QVector<Row> m_rows;
    int m_runningRow = -1;
    int m_completedSteps = 0;
    // Adaptive progress: the previous launch's totals, so a second run's bar
    // tracks reality instead of a guess. Seeded with a sane default.
    int m_expectedSteps = 30;
    qint64 m_expectedMs = 2500;
    // Wall clock shared by every animated value, started at construction.
    QElapsedTimer m_clock;
    qint64 m_finishAtMs = -1;   // when finish() was called, else -1
    qint64 m_lastPaintMs = 0;   // for time-based progress easing
    qreal m_shownProgress = 0.0;
    QTimer *m_animation = nullptr;
    QTimer *m_deadline = nullptr;
    QString m_fontFamily;
    QFont m_titleFont, m_subtitleFont, m_rowFont, m_detailFont, m_metaFont;
};

// --- Process-wide splash, owned by main(). Every hook below is a no-op when
// no splash exists (headless, tests, after it has faded), so call sites never
// need to know whether one is on screen.

// Creates and shows the splash centred on the screen under the cursor. Returns
// null (and does nothing) when headless, when FORKMESH_NO_SPLASH is set, or
// when the user turned it off in settings.
StartupSplash *showStartupSplash(bool headless, const QString &version,
                                 const QString &commit);
StartupSplash *activeStartupSplash();

void startupStep(const QString &label);
void startupDetail(const QString &text);
void startupStepFailed(const QString &reason);
// Final step, then fade out. Idempotent.
void finishStartupSplash(const QString &label = QString());
// Immediate teardown for the paths that exit or show a dialog instead.
void dismissStartupSplash();

} // namespace forkmesh::ui
