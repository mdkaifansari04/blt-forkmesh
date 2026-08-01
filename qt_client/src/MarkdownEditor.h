#pragma once

#include <QStringList>
#include <QWidget>

class QKeyEvent;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;








class MarkdownEditor : public QWidget
{
    Q_OBJECT
public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    void setMarkdown(const QString &text);
    QString markdown() const;


    QStringList pendingAttachments() const { return m_attachments; }
    QStringList pendingAttachmentPlaceholders() const { return m_attachmentPlaceholders; }



    void setPreviewBasePath(const QString &dir);

    void setPlaceholderText(const QString &text);
    void focusEditor();



    QPlainTextEdit *sourceEdit() const { return m_source; }
    void showWriteArea();
    void addImageFile(const QString &path);
    void clearPendingAttachments();




    void setMentionCandidates(const QStringList &names);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void showWrite();
    void showPreview();
    void wrapSelection(const QString &left, const QString &right);
    void chooseImage();
    bool pasteImageFromClipboard();
    void updatePreview();




    void updateMentionPopup();
    void hideMentionPopup();
    void acceptMention(const QString &name);
    bool handleMentionKey(QKeyEvent *event);

    QPlainTextEdit *m_source = nullptr;
    QTextBrowser *m_preview = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPushButton *m_writeTab = nullptr;
    QPushButton *m_previewTab = nullptr;
    QStringList m_attachments;
    QStringList m_attachmentPlaceholders;
    QString m_basePath;

    QStringList m_mentionCandidates;
    QListWidget *m_mentionPopup = nullptr;
    int m_mentionAnchor = -1;
};
