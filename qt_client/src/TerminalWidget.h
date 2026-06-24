#pragma once

#include <QColor>
#include <QFont>
#include <QVector>
#include <QWidget>

class QSocketNotifier;

// A self-contained, lightweight terminal emulator that runs a command under a
// real pseudo-terminal (forkpty) and renders the screen itself — no xterm, no
// X11 reparenting, so it behaves identically on X11 and Wayland and resizes
// cleanly inside the app. It implements the subset of VT100/xterm escape
// sequences interactive CLIs like Claude Code use: cursor addressing, SGR
// colors/attributes, erase/insert/delete, scroll regions and the alternate
// screen. The public API mirrors the old EmbeddedTerminal so callers swap in
// place.
class TerminalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // Run a shell command line under a fresh PTY, working in cwd. Any running
    // session is replaced first. extraEnv holds "KEY=VALUE" entries; an entry
    // with no '=' (just "KEY") removes that variable from the child environment.
    void runCommand(const QString &commandLine, const QString &cwd,
                    const QStringList &extraEnv = {});
    bool isRunning() const { return m_childPid > 0; }
    void stop();

signals:
    void started();
    void finished(int exitCode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Cell {
        char32_t ch = U' ';
        QColor fg;
        QColor bg;
        bool bold = false;
        bool inverse = false;
        bool underline = false;
    };
    using Row = QVector<Cell>;

    // --- PTY / process ---
    void onReadyRead();          // drain the master fd into the parser
    void reap(bool emitSignal);  // collect the child's exit status
    void writeToPty(const QByteArray &bytes);
    void applyWinSize();         // TIOCSWINSZ from the current grid size

    // --- screen model ---
    void recomputeGrid();        // size the grid from the widget + font metrics
    void resizeGrid(int rows, int cols);
    Cell blankCell() const;
    void clearRow(Row &row) const;
    void putChar(char32_t ch);
    void newLine();
    void scrollUp(int top, int bottom, int n);
    void scrollDown(int top, int bottom, int n);
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void useAltScreen(bool on);

    // --- escape-sequence parser ---
    void feed(const QByteArray &bytes);
    void handleControl(char32_t ch);
    void handleCsi(char32_t finalByte);
    void handleEsc(char32_t ch);
    void applySgr();

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    // PTY state
    int m_master = -1;
    long long m_childPid = -1;
    QSocketNotifier *m_notifier = nullptr;

    // Rendering metrics
    QFont m_font;
    int m_cellW = 8;
    int m_cellH = 16;
    bool m_hasFocus = false;

    // Screen buffers (primary + alternate)
    QVector<Row> m_screen;
    QVector<Row> m_alt;
    QVector<Row> m_scrollback; // primary-screen lines scrolled off the top
    bool m_altActive = false;
    int m_rows = 24;
    int m_cols = 80;

    // Cursor + saved cursor
    int m_cx = 0, m_cy = 0;
    int m_savedCx = 0, m_savedCy = 0;
    int m_scrollTop = 0, m_scrollBottom = 23;
    bool m_cursorVisible = true;
    bool m_wrapPending = false; // deferred wrap (cursor sits past the last col)

    // Current pen
    QColor m_curFg;
    QColor m_curBg;
    bool m_curBold = false;
    bool m_curInverse = false;
    bool m_curUnderline = false;

    // Modes
    bool m_autoWrap = true;
    bool m_appCursorKeys = false;
    bool m_bracketedPaste = false;

    // View scroll: how many lines up from the bottom the viewport is shifted.
    int m_viewOffset = 0;

    // Parser state machine
    enum class State { Ground, Esc, Csi, Osc, EscIntermediate };
    State m_state = State::Ground;
    QByteArray m_csiParams;
    QVector<int> m_params;
    bool m_csiPrivate = false;
    // UTF-8 decode accumulator
    char32_t m_utf8 = 0;
    int m_utf8Remaining = 0;
};
