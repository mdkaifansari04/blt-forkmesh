#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>



struct ActionStep {
    QString name;
    QString run;
};




struct ActionWorkflow {
    QString path;
    QString content;
    QString name;
    QStringList on;
    QMap<QString, QString> env;





    QStringList runsOn;






    QStringList needs;
    QList<ActionStep> steps;
    bool valid = false;
    QString error;

    bool triggersOnPush() const { return on.contains(QStringLiteral("push")); }



    bool triggersOnRelease() const
    {
        return on.contains(QStringLiteral("release"));
    }


    bool allowsManualRun() const
    {
        return on.contains(QStringLiteral("workflow_dispatch"));
    }




    bool runsOnNode(const QStringList &nodeLabels) const;
};












class ActionFile
{
public:
    static ActionWorkflow parse(const QString &relPath, const QString &content);


    static QList<ActionWorkflow> parseWorkflowsInDir(const QString &checkoutDir);






    static QStringList nodeLabels(const QString &machineNode,
                                  const QString &mirrorNode,
                                  const QString &configured);


    static QStringList parseLabelList(const QString &configured);





    static QString pinnedNode(const QStringList &pins, const QString &path);


    static QStringList setPinnedNode(const QStringList &pins,
                                     const QStringList &paths,
                                     const QString &node);






    static QString substitute(const QString &input,
                              const QMap<QString, QString> &vars);
};
