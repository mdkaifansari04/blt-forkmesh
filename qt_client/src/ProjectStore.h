#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

class ForkMeshIdentity;




struct ProjectEvent {
    QString type;
    QString id;
    QString author;
    QString authorName;
    qint64 ts = 0;
    QString title;
    QString body;
    QString status;
    qint64 startDate = 0;
    qint64 endDate = 0;
    QString milestone;
    QList<int> issues;
    QString target;
    QString sig;

    QJsonObject toJson() const;
    static ProjectEvent fromJson(const QJsonObject &obj);
};


struct Project {
    int number = 0;
    QString title;
    QString body;
    QString status = "open";
    qint64 startDate = 0;
    qint64 endDate = 0;
    QString milestone;
    QList<int> issues;
    qint64 createdAt = 0;
    QString author;
    QString authorName;
    QList<ProjectEvent> events;

    QJsonObject toJson() const;
    static Project fromJson(const QJsonObject &obj);
    bool isDeleted() const;
};




class ProjectStore
{
public:



    ProjectStore(QString workTreePath, QString mirrorPath,
                 const ForkMeshIdentity *identity, QString authorName = QString());


    bool canWrite() const;

    QList<Project> loadAll(QString *error = nullptr) const;



    int createProject(const QString &title, const QString &body,
                      qint64 startDate, qint64 endDate, const QString &milestone,
                      const QList<int> &issues, QString *error = nullptr);
    bool setTitle(int number, const QString &newTitle, QString *error = nullptr);
    bool setBody(int number, const QString &newBody, QString *error = nullptr);
    bool setStatus(int number, const QString &status, QString *error = nullptr);
    bool setDates(int number, qint64 startDate, qint64 endDate,
                  QString *error = nullptr);
    bool setMilestone(int number, const QString &milestone, QString *error = nullptr);
    bool setIssues(int number, const QList<int> &issues, QString *error = nullptr);
    bool tombstoneProject(int number, QString *error = nullptr);


    ProjectEvent makeSignedEvent(int number, ProjectEvent ev) const;



    static QByteArray canonicalString(int number, const ProjectEvent &ev);
    static QString contentForSigning(const ProjectEvent &ev);

private:
    QString projectsDir() const;
    QString projectDir(int number) const;
    QString projectFilePath(int number) const;
    bool readProjectFile(int number, Project &out) const;
    bool writeProjectFile(const Project &project, QString *error) const;
    void recomputeMetadata(Project &project) const;
    int nextNumber() const;
    bool commit(const QString &message, QString *error) const;


    QList<Project> loadFromMirror(QString *error) const;
    QString mirrorRef() const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;

    mutable bool m_mirrorRefResolved = false;
    mutable QString m_cachedMirrorRef;
};
