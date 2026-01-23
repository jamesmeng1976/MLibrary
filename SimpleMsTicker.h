#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>

class SimpleMsTicker final : public QObject {
    Q_OBJECT
public:
    struct Config {
        int intervalMs = 10;   // 逻辑周期：到点就触发 timeout()
        int wakeupMs   = 1;    // 唤醒周期：内部轮询频率（1~2ms 常用）
        bool emitWake  = false; // 是否每次唤醒都发 wake(deltaMs)
    };

    explicit SimpleMsTicker(QObject* parent = nullptr)
        : QObject(parent)
    {
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setSingleShot(false);
        connect(&m_timer, &QTimer::timeout, this, &SimpleMsTicker::onWakeup);
    }

    void setConfig(const Config& cfg) {
        m_cfg = cfg;
        if (m_cfg.intervalMs < 1) m_cfg.intervalMs = 1;
        if (m_cfg.wakeupMs   < 1) m_cfg.wakeupMs   = 1;
    }

    Config config() const { return m_cfg; }
    bool isRunning() const { return m_timer.isActive(); }

public slots:
    void start() {
        if (m_timer.isActive()) return;

        m_elapsed.restart();
        m_lastWakeNs = m_elapsed.nsecsElapsed();
        m_lastFireNs = m_lastWakeNs;

        m_timer.start(m_cfg.wakeupMs);
        emit started();
    }

    void stop() {
        if (!m_timer.isActive()) return;
        m_timer.stop();
        emit stopped();
    }

signals:
    void started();
    void stopped();

    // 每次内部唤醒（可选），deltaMs=距上次唤醒真实间隔
    void wake(int deltaMs);

    // 到点触发：deltaMs=距上次 timeout 的真实间隔（用于你观察抖动/漂移）
    void timeout(int deltaMs);

private slots:
    void onWakeup() {
        if (!m_elapsed.isValid()) return;

        const qint64 nowNs = m_elapsed.nsecsElapsed();

        // 1) 计算距上次“唤醒”的真实间隔
        int wakeDeltaMs = int((nowNs - m_lastWakeNs) / 1000000);
        if (wakeDeltaMs < 0) wakeDeltaMs = 0;
        m_lastWakeNs = nowNs;

        if (m_cfg.emitWake) {
            emit wake(wakeDeltaMs);
        }

        // 2) 计算距上次“到点触发”的经过时间
        const qint64 sinceFireNs = nowNs - m_lastFireNs;
        if (sinceFireNs < 0) return;

        // 3) 到点就触发一次 timeout（不补帧）
        if (sinceFireNs >= qint64(m_cfg.intervalMs) * 1000000LL) {
            const int fireDeltaMs = int(sinceFireNs / 1000000);
            m_lastFireNs = nowNs;
            emit timeout(fireDeltaMs);
        }
    }

private:
    Config m_cfg;

    QTimer m_timer;
    QElapsedTimer m_elapsed;

    qint64 m_lastWakeNs = 0;
    qint64 m_lastFireNs = 0;
};
