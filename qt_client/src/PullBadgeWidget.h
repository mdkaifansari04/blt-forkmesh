






#pragma once

#include <QIcon>
#include <QList>
#include <QString>
#include <QVector>
#include <QWidget>

class PullBadgeWidget : public QWidget
{
public:
    struct FileEntry {
        QString path;
        int adds = 0;
        int dels = 0;
        QIcon icon;
    };

    explicit PullBadgeWidget(QWidget *parent = nullptr);

    void setPull(const QString &title, int number, const QString &author,
                 int additions, int deletions, const QList<FileEntry> &files);
    void clearPull();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *e) override;

private:
    struct Tile {
        QRect rect;
        int fileIndex = -1;
    };
    struct GroupSegment {
        QRect lineRect;
        bool labeled = false;
        QString label;
    };

    void relayout();

    QString m_title;
    int m_number = 0;
    QString m_author;
    int m_additions = 0;
    int m_deletions = 0;
    QList<FileEntry> m_files;
    QVector<Tile> m_tiles;
    QVector<GroupSegment> m_segments;
    int m_contentHeight = 0;
};
