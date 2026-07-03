#include "ScreenshotMarkupWindow.h"

#include "MarkupCanvas.h"

#include <QButtonGroup>
#include <QColor>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

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
    // ApplicationModal blocks every window in the app while open, so the user
    // can't accidentally interact with the main window before annotating.
    setWindowModality(Qt::ApplicationModal);
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
    auto *btnLine    = new QToolButton(this);
    auto *btnArrow   = new QToolButton(this);
    auto *btnRect    = new QToolButton(this);
    auto *btnEllipse = new QToolButton(this);
    for (auto *b : {btnPencil, btnLine, btnArrow, btnRect, btnEllipse}) {
        b->setCheckable(true);
        b->setObjectName("topNavButton");
    }
    btnPencil->setText(QStringLiteral("Pencil"));
    btnLine->setText(QStringLiteral("Line"));
    btnArrow->setText(QStringLiteral("Arrow"));
    btnRect->setText(QStringLiteral("Rectangle"));
    btnEllipse->setText(QStringLiteral("Ellipse"));
    btnPencil->setChecked(true);

    auto *toolGroup = new QButtonGroup(this);
    toolGroup->addButton(btnPencil);
    toolGroup->addButton(btnLine);
    toolGroup->addButton(btnArrow);
    toolGroup->addButton(btnRect);
    toolGroup->addButton(btnEllipse);

    connect(btnPencil,  &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Pencil); });
    connect(btnLine,    &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Line); });
    connect(btnArrow,   &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Arrow); });
    connect(btnRect,    &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Rect); });
    connect(btnEllipse, &QToolButton::clicked, this,
            [this] { m_canvas->setTool(MarkupTool::Ellipse); });

    toolbarLayout->addWidget(btnPencil);
    toolbarLayout->addWidget(btnLine);
    toolbarLayout->addWidget(btnArrow);
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
