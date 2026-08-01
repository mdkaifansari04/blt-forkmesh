#pragma once

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QUrl>
#include <QVector>
#include <QWidget>

class QContextMenuEvent;
class QMouseEvent;
class QSocketNotifier;









class TerminalWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;




    void runCommand(const QString &commandLine, const QString &cwd,
                    const QStringList &extraEnv = {});
    bool isRunning() const { return m_childPid > 0; }
    void stop();

signals:
    void started();
    void finished(int exitCode);



    void signInUrlDetected(const QUrl &url);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
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


    void onReadyRead();
    void reap(bool emitSignal);
    void writeToPty(const QByteArray &bytes);
    void applyWinSize();
    void pasteFromClipboard();


    void recomputeGrid();
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


    void feed(const QByteArray &bytes);
    void handleControl(char32_t ch);
    void handleCsi(char32_t finalByte);
    void handleEsc(char32_t ch);
    void applySgr();

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }


    struct CellPos {
        int row = 0;
        int col = 0;
    };
    CellPos cellPosAt(const QPoint &widgetPos) const;
    const Row *rowAt(int absRow) const;
    int streamRowCount() const;
    QString selectedText() const;
    void copySelection() const;
    void clearSelection();


    void scanForSignInUrl(const QByteArray &chunk);


    int m_master = -1;
    long long m_childPid = -1;
    QSocketNotifier *m_notifier = nullptr;


    QFont m_font;
    int m_cellW = 8;
    int m_cellH = 16;
    bool m_hasFocus = false;


    QVector<Row> m_screen;
    QVector<Row> m_alt;
    QVector<Row> m_scrollback;
    bool m_altActive = false;
    int m_rows = 24;
    int m_cols = 80;


    int m_cx = 0, m_cy = 0;
    int m_savedCx = 0, m_savedCy = 0;
    int m_scrollTop = 0, m_scrollBottom = 23;
    bool m_cursorVisible = true;
    bool m_wrapPending = false;


    QColor m_curFg;
    QColor m_curBg;
    bool m_curBold = false;
    bool m_curInverse = false;
    bool m_curUnderline = false;


    bool m_autoWrap = true;
    bool m_appCursorKeys = false;
    bool m_bracketedPaste = false;


    int m_viewOffset = 0;


    bool m_selecting = false;
    bool m_hasSelection = false;
    CellPos m_selAnchor;
    CellPos m_selCursor;




    QByteArray m_urlScanBuffer;
    bool m_signInUrlEmitted = false;


    enum class State { Ground, Esc, Csi, Osc, EscIntermediate };
    State m_state = State::Ground;
    QByteArray m_csiParams;
    QVector<int> m_params;
    bool m_csiPrivate = false;

    char32_t m_utf8 = 0;
    int m_utf8Remaining = 0;
};
