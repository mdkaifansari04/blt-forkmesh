#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>

class QTimer;






namespace stallwatch {
void noteBlockingCall(const QString &what);
QString blockingCall();
}





class BlockingCallScope
{
public:
    explicit BlockingCallScope(const QString &what);
    ~BlockingCallScope();
    BlockingCallScope(const BlockingCallScope &) = delete;
    BlockingCallScope &operator=(const BlockingCallScope &) = delete;

private:
    QString m_prev;
};







class StallWatchdog : public QObject
{
    Q_OBJECT
public:
    explicit StallWatchdog(QObject *parent = nullptr);
    ~StallWatchdog() override;





    void start(int stallThresholdMs = 1500, const QString &logPath = QString(),
               const QString &buildInfo = QString());

signals:



    void stalled(qint64 peakMs, const QString &blockingCall, const QString &backtrace);

private:
    void beat();
    void watchLoop();

    std::atomic<qint64> m_lastBeatMs{0};
    std::atomic<bool> m_running{false};
    int m_thresholdMs = 1500;
    QString m_logPath;
    QString m_buildInfo;
    QString m_exePath;
    qint64 m_pid = 0;
    QTimer *m_beatTimer = nullptr;
    std::thread m_worker;
};
