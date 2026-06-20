#include "MarkdownEditor.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
const QStringList kImageSuffixes = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "svg"};

bool isImagePath(const QString &path)
{
    return kImageSuffixes.contains(QFileInfo(path).suffix().toLower());
}
}  // namespace

MarkdownEditor::MarkdownEditor(QWidget *parent) : QWidget(parent)
{
    m_source = new QPlainTextEdit;
    m_source->setObjectName("markdownSource");
    m_source->setAcceptDrops(false);  // drops are handled by this widget instead
    m_preview = new QTextBrowser;
    m_preview->setObjectName("markdownPreview");
    m_preview->setOpenExternalLinks(true);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(0);
    m_writeTab = new QPushButton("Write");
    m_previewTab = new QPushButton("Preview");
    for (QPushButton *tab : {m_writeTab, m_previewTab}) {
        tab->setObjectName("markdownTab");
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
    }
    m_writeTab->setChecked(true);
    header->addWidget(m_writeTab);
    header->addWidget(m_previewTab);
    header->addSpacing(8);

    // Formatting toolbar: each button wraps the selection or inserts a snippet.
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(2);
    struct Tool { const char *label; const char *left; const char *right; };
    const Tool tools[] = {
        {"H", "\n## ", ""},
        {"B", "**", "**"},
        {"I", "*", "*"},
        {"Code", "`", "`"},
        {"List", "\n- ", ""},
        {"<>", "\n```\n", "\n```\n"},
        {"Link", "[", "](https://)"},
    };
    for (const Tool &t : tools) {
        auto *btn = new QToolButton;
        btn->setObjectName("markdownTool");
        btn->setText(QString::fromUtf8(t.label));
        const QString left = QString::fromUtf8(t.left);
        const QString right = QString::fromUtf8(t.right);
        connect(btn, &QToolButton::clicked, this,
                [this, left, right]() { wrapSelection(left, right); });
        toolbar->addWidget(btn);
    }
    auto *imgBtn = new QToolButton;
    imgBtn->setObjectName("markdownTool");
    imgBtn->setText("Image");
    connect(imgBtn, &QToolButton::clicked, this, &MarkdownEditor::chooseImage);
    toolbar->addWidget(imgBtn);
    toolbar->addStretch();
    header->addLayout(toolbar, 1);

    m_stack = new QStackedWidget;
    m_stack->setObjectName("markdownStack");
    m_stack->addWidget(m_source);
    m_stack->addWidget(m_preview);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_stack, 1);

    connect(m_source, &QPlainTextEdit::textChanged, this,
            &MarkdownEditor::updatePreview);
    connect(m_writeTab, &QPushButton::clicked, this, &MarkdownEditor::showWrite);
    connect(m_previewTab, &QPushButton::clicked, this, &MarkdownEditor::showPreview);

    // Accept image drops on the whole editor and on the text area's viewport.
    setAcceptDrops(true);
    m_source->viewport()->installEventFilter(this);
}

void MarkdownEditor::setMarkdown(const QString &text)
{
    m_source->setPlainText(text);
    updatePreview();
}

QString MarkdownEditor::markdown() const { return m_source->toPlainText(); }

void MarkdownEditor::setPreviewBasePath(const QString &dir)
{
    m_basePath = dir;
    if (!dir.isEmpty())
        m_preview->setSearchPaths({dir});
    updatePreview();
}

void MarkdownEditor::setPlaceholderText(const QString &text)
{
    m_source->setPlaceholderText(text);
}

void MarkdownEditor::focusEditor() { m_source->setFocus(); }

void MarkdownEditor::addImageFile(const QString &path)
{
    if (path.isEmpty() || !isImagePath(path))
        return;
    if (!m_attachments.contains(path))
        m_attachments.append(path);
    const QString name = QFileInfo(path).fileName();
    QTextCursor cursor = m_source->textCursor();
    cursor.insertText(QStringLiteral("\n![%1](attachments/%1)\n").arg(name));
    m_source->setFocus();
}

void MarkdownEditor::clearPendingAttachments()
{
    m_attachments.clear();
}

void MarkdownEditor::showWrite()
{
    m_stack->setCurrentWidget(m_source);
    m_writeTab->setChecked(true);
    m_previewTab->setChecked(false);
    m_source->setFocus();
}

void MarkdownEditor::showPreview()
{
    updatePreview();
    m_stack->setCurrentWidget(m_preview);
    m_writeTab->setChecked(false);
    m_previewTab->setChecked(true);
}

void MarkdownEditor::updatePreview()
{
    m_preview->setMarkdown(m_source->toPlainText());
}

void MarkdownEditor::wrapSelection(const QString &left, const QString &right)
{
    QTextCursor cursor = m_source->textCursor();
    const QString selected = cursor.selectedText();
    cursor.insertText(left + selected + right);
    m_source->setFocus();
}

void MarkdownEditor::chooseImage()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Add image", QString(),
        "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg)");
    for (const QString &f : files)
        addImageFile(f);
}

bool MarkdownEditor::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_source->viewport()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *de = static_cast<QDragEnterEvent *>(event);
            if (de->mimeData()->hasUrls()) {
                de->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(event);
            if (de->mimeData()->hasUrls()) {
                bool any = false;
                for (const QUrl &url : de->mimeData()->urls()) {
                    if (url.isLocalFile() && isImagePath(url.toLocalFile())) {
                        addImageFile(url.toLocalFile());
                        any = true;
                    }
                }
                if (any) {
                    de->acceptProposedAction();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
