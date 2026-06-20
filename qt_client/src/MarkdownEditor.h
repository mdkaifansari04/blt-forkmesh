#pragma once

#include <QStringList>
#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

// A markdown source editor with a live rendered preview, a small formatting
// toolbar, and image drag-and-drop. Dropped (or toolbar-inserted) images are
// recorded as pending attachments: the caller passes pendingAttachments() to the
// IssueStore, which copies them into the issue's attachments/ folder. The editor
// inserts an ![name](attachments/name) reference at the cursor for each image.
class MarkdownEditor : public QWidget
{
    Q_OBJECT
public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    void setMarkdown(const QString &text);
    QString markdown() const;

    // Absolute source paths of images added via drop or the toolbar (de-duped).
    QStringList pendingAttachments() const { return m_attachments; }

    // Directory whose attachments/ subfolder holds already-saved images, so the
    // preview can render existing ![](attachments/...) references.
    void setPreviewBasePath(const QString &dir);

    void setPlaceholderText(const QString &text);
    void focusEditor();
    void addImageFile(const QString &path);
    void clearPendingAttachments();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void showWrite();
    void showPreview();
    void wrapSelection(const QString &left, const QString &right);
    void chooseImage();
    void updatePreview();

    QPlainTextEdit *m_source = nullptr;
    QTextBrowser *m_preview = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPushButton *m_writeTab = nullptr;
    QPushButton *m_previewTab = nullptr;
    QStringList m_attachments;
    QString m_basePath;
};
