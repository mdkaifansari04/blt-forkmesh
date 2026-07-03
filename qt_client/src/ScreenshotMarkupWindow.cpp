#include "ScreenshotMarkupWindow.h"

#include <QButtonGroup>
#include <QColor>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

// ---------------------------------------------------------------------------
// Markup data
// ---------------------------------------------------------------------------

enum class MarkupTool { Pencil, Rect, Ellipse };

struct MarkupOp {
    enum class Kind { Stroke, Shape };
    Kind kind = Kind::Stroke;
    QColor color;
    QVector<QPoint> points; // used when kind == Stroke
    MarkupTool shapeType = MarkupTool::Rect; // used when kind == Shape
    QRect rect;
};

// ---------------------------------------------------------------------------
// MarkupCanvas — the drawable viewport over the screenshot
// ---------------------------------------------------------------------------

class MarkupCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit MarkupCanvas(const QImage &base, QWidget *parent = nullptr)
        : QWidget(parent), m_base(base), m_color(QColor(255, 50, 50))
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setCursor(Qt::CrossCursor);
    }

    void setTool(MarkupTool tool) { m_tool = tool; }
    void setColor(const QColor &color) { m_color = color; }

    void undo()
    {
        if (!m_ops.isEmpty()) {
            m_ops.removeLast();
            update();
        }
    }

    // Composite all markup onto the base image and return the flattened result.
    QImage flattenedImage() const
    {
        QImage result = m_base.convertToFormat(QImage::Format_ARGB32);
        QPainter p(&result);
        renderOps(p, m_ops);
        return result;
    }

    QSize sizeHint() const override { return m_base.size(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawImage(0, 0, m_base);
        renderOps(painter, m_ops);
        if (m_drawing)
            renderOp(painter, m_current);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        m_drawing = true;
        m_current = MarkupOp{};
        m_current.color = m_color;
        if (m_tool == MarkupTool::Pencil) {
            m_current.kind = MarkupOp::Kind::Stroke;
            m_current.points.append(event->position().toPoint());
        } else {
            m_current.kind = MarkupOp::Kind::Shape;
            m_current.shapeType = m_tool;
            m_dragOrigin = event->position().toPoint();
            m_current.rect = QRect(m_dragOrigin, m_dragOrigin);
        }
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_drawing)
            return;
        const QPoint pos = event->position().toPoint();
        if (m_current.kind == MarkupOp::Kind::Stroke)
            m_current.points.append(pos);
        else
            m_current.rect = QRect(m_dragOrigin, pos).normalized();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_drawing || event->button() != Qt::LeftButton)
            return;
        m_drawing = false;
        const QPoint pos = event->position().toPoint();
        if (m_current.kind == MarkupOp::Kind::Stroke) {
            m_current.points.append(pos);
            if (!m_current.points.isEmpty())
                m_ops.append(m_current);
        } else {
            m_current.rect = QRect(m_dragOrigin, pos).normalized();
            if (m_current.rect.width() >= 3 && m_current.rect.height() >= 3)
                m_ops.append(m_current);
        }
        m_current = MarkupOp{};
        update();
    }

private:
    static void renderOp(QPainter &painter, const MarkupOp &op)
    {
        painter.save();
        QPen pen(op.color);
        pen.setWidth(3);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (op.kind == MarkupOp::Kind::Stroke) {
            if (op.points.size() == 1)
                painter.drawPoint(op.points.first());
            else if (op.points.size() > 1)
                painter.drawPolyline(op.points.constData(), op.points.size());
        } else if (op.shapeType == MarkupTool::Rect) {
            painter.drawRect(op.rect);
        } else {
            painter.drawEllipse(op.rect);
        }
        painter.restore();
    }

    static void renderOps(QPainter &painter, const QVector<MarkupOp> &ops)
    {
        for (const MarkupOp &op : ops)
            renderOp(painter, op);
    }

    QImage m_base;
    QVector<MarkupOp> m_ops;
    MarkupOp m_current;
    bool m_drawing = false;
    QPoint m_dragOrigin;
    MarkupTool m_tool = MarkupTool::Pencil;
    QColor m_color;
};

// ---------------------------------------------------------------------------
// ScreenshotMarkupWindow
// ---------------------------------------------------------------------------

// A small filled-circle swatch button for picking the ink colour.
static QToolButton *makeColorSwatch(const QColor &color, QWidget *parent)
{
    auto *btn = new QToolButton(parent);
    btn->setCheckable(true);
    constexpr int sz = 22;
    btn->setFixedSize(sz, sz);

    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(color);
        p.setPen(QPen(QColor(255, 255, 255, 80), 1));
        p.drawEllipse(QRect(2, 2, sz - 4, sz - 4));
    }

    btn->setIcon(QIcon(pm));
    btn->setIconSize(QSize(sz, sz));
    btn->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; padding: 0; background: transparent; }"
        "QToolButton:checked { border: 2px solid #2f81f7; border-radius: 13px; }"));
    return btn;
}

ScreenshotMarkupWindow::ScreenshotMarkupWindow(const QImage &screenshot, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Annotate Screenshot"));
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose);

    // ---- Canvas ----
    m_canvas = new MarkupCanvas(screenshot, this);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(m_canvas);
    scrollArea->setWidgetResizable(false);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Size dialog to fit the screenshot, capped at 90 % of available screen.
    const QSize screenSz = QGuiApplication::primaryScreen()
                               ? QGuiApplication::primaryScreen()->availableSize()
                               : QSize(1920, 1080);
    const int maxW = qRound(screenSz.width()  * 0.90);
    const int maxH = qRound(screenSz.height() * 0.85);
    constexpr int kToolbarH = 48;
    constexpr int kFooterH  = 52;
    constexpr int kPad      = 24;
    const QSize imgSz = screenshot.size();
    resize(qMin(imgSz.width()  + kPad, maxW),
           qMin(imgSz.height() + kToolbarH + kFooterH + kPad, maxH));

    // ---- Toolbar ----
    auto *toolbar = new QWidget(this);
    toolbar->setFixedHeight(kToolbarH);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(4);

    auto *btnPencil  = new QToolButton(this);
    auto *btnRect    = new QToolButton(this);
    auto *btnEllipse = new QToolButton(this);
    for (auto *b : {btnPencil, btnRect, btnEllipse}) {
        b->setCheckable(true);
        b->setObjectName("topNavButton");
    }
    btnPencil->setText(QStringLiteral("Pencil"));
    btnRect->setText(QStringLiteral("Rectangle"));
    btnEllipse->setText(QStringLiteral("Ellipse"));
    btnPencil->setChecked(true);

    auto *toolGroup = new QButtonGroup(this);
    toolGroup->addButton(btnPencil);
    toolGroup->addButton(btnRect);
    toolGroup->addButton(btnEllipse);

    connect(btnPencil,  &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Pencil); });
    connect(btnRect,    &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Rect); });
    connect(btnEllipse, &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Ellipse); });

    toolbarLayout->addWidget(btnPencil);
    toolbarLayout->addWidget(btnRect);
    toolbarLayout->addWidget(btnEllipse);

    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(sep);

    // Colour swatches
    const QList<QColor> colors = {
        QColor(255,  50,  50), // red (default)
        QColor(255, 165,   0), // orange
        QColor(255, 230,   0), // yellow
        QColor( 50, 200,  80), // green
        QColor( 50, 130, 255), // blue
        QColor(200,  50, 255), // purple
        QColor(  0,   0,   0), // black
        QColor(255, 255, 255), // white
    };
    auto *colorGroup = new QButtonGroup(this);
    bool firstColor  = true;
    for (const QColor &c : colors) {
        auto *swatch = makeColorSwatch(c, this);
        swatch->setChecked(firstColor);
        colorGroup->addButton(swatch);
        toolbarLayout->addWidget(swatch);
        const QColor cap = c;
        connect(swatch, &QToolButton::clicked, this,
                [this, cap] { m_canvas->setColor(cap); });
        firstColor = false;
    }

    toolbarLayout->addStretch();

    auto *btnUndo = new QToolButton(this);
    btnUndo->setText(QStringLiteral("Undo"));
    btnUndo->setObjectName("ghostButton");
    connect(btnUndo, &QToolButton::clicked, this, [this] { m_canvas->undo(); });
    toolbarLayout->addWidget(btnUndo);

    // ---- Footer ----
    auto *footer = new QWidget(this);
    footer->setFixedHeight(kFooterH);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(8, 6, 8, 6);

    auto *btnDiscard = new QPushButton(QStringLiteral("Discard"), this);
    btnDiscard->setObjectName("ghostButton");
    connect(btnDiscard, &QPushButton::clicked, this, &QDialog::reject);

    auto *btnAdd = new QPushButton(QStringLiteral("Add to Prompt"), this);
    btnAdd->setObjectName("primaryButton");
    btnAdd->setDefault(true);
    connect(btnAdd, &QPushButton::clicked, this,
            &ScreenshotMarkupWindow::onAccept);

    footerLayout->addStretch();
    footerLayout->addWidget(btnDiscard);
    footerLayout->addWidget(btnAdd);

    // ---- Root layout ----
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(toolbar);
    root->addWidget(scrollArea, 1);
    root->addWidget(footer);
}

void ScreenshotMarkupWindow::onAccept()
{
    emit imageAccepted(m_canvas->flattenedImage());
    accept();
}

#include "ScreenshotMarkupWindow.moc"
