#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class ForkMeshIdentity;




struct CoveDocument {
    QString id;
    QString name;
    QString mime = "text/markdown";
    QString body;
    qint64 updatedAtMs = 0;

    QJsonObject toJson() const;
    static CoveDocument fromJson(const QJsonObject &obj);
};



struct CoveAccessEntry {
    QString who;
    QString name;
    QString node;
    qint64 ts = 0;
    QString action = "open";

    QJsonObject toJson() const;
    static CoveAccessEntry fromJson(const QJsonObject &obj);
};









struct Cove {
    QString id;
    QString name;
    QString slug;
    QString relPath;
    int version = 1;
    QString creator;
    QString accessMode = "password";
    QString creatorAccount;
    QStringList invitedAccounts;
    qint64 createdAtMs = 0;
    bool notifyOnOpen = false;
    QByteArray salt;
    int rounds = 0;
    QJsonObject cipher;
    QJsonArray grants;


    bool unlocked = false;
    QByteArray contentKey;
    QList<CoveDocument> documents;
    QList<CoveAccessEntry> accessLog;

    bool createdByMe(const ForkMeshIdentity *identity) const;
    bool accountScoped() const { return accessMode == QStringLiteral("account"); }
};




class CoveStore
{
public:
    CoveStore(QString workTreePath, QString mirrorPath,
              const ForkMeshIdentity *identity, QString authorName = QString());


    bool canWrite() const;


    QList<Cove> listCoves(QString *error = nullptr) const;

    bool loadEnvelope(const QString &relPath, Cove &out, QString *error = nullptr) const;



    static bool unlock(Cove &cove, const QString &password);




    static bool accountCanAccess(const Cove &cove, const QString &accountName);


    static bool unlockForAccount(Cove &cove, const QString &accountName);


    bool createCove(const QString &name, const QString &password, bool notifyOnOpen,
                    const QList<CoveDocument> &documents, Cove *out,
                    QString *error = nullptr);
    bool createAccountCove(const QString &name, const QString &creatorAccount,
                           const QStringList &invitedAccounts, bool notifyOnOpen,
                           const QList<CoveDocument> &documents, Cove *out,
                           QString *error = nullptr);


    bool save(const Cove &cove, const QString &password, QString *error = nullptr);
    bool saveAccountCove(const Cove &cove, QString *error = nullptr);



    static void appendAccess(Cove &cove, const CoveAccessEntry &entry);

    static QString covesDirRel() { return QStringLiteral(".forkmesh/coves"); }

private:
    QString covesDir() const;
    QByteArray readCoveBytes(const QString &relPath, bool *ok) const;
    bool commit(const QString &message, const QString &relPath, QString *error) const;
    QString mirrorRef() const;
    QString uniqueSlug() const;

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;
};
