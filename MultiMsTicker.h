#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>

class MultiMsTicker final : public QObject {
    Q_OBJECT
public:
    struct TimerConfig {
        int intervalMs = 10;     // 到点周期
        bool enabled = true;     // 是否启用该 ID
        bool oneShot = false;    // 一次触发后自动停用
    };

    struct GlobalConfig {
        int wakeupMs = 1;        // 全局唤醒周期（内部轮询）
        bool emitWake = false;   // 是否发射每次全局唤醒间隔
    };

    explicit MultiMsTicker(QObject* parent = nullptr)
        : QObject(parent)
    {
        m_wakeupTimer.setTimerType(Qt::PreciseTimer);
        m_wakeupTimer.setSingleShot(false);
        connect(&m_wakeupTimer, &QTimer::timeout, this, &MultiMsTicker::onWakeup);
    }

    // ---------- 默认参数 ----------
    void setDefaultTimerConfig(const TimerConfig& cfg) {
        m_defaultCfg = normalizeTimerCfg(cfg);
    }
    TimerConfig defaultTimerConfig() const { return m_defaultCfg; }

    void setGlobalConfig(const GlobalConfig& cfg) {
        m_globalCfg = normalizeGlobalCfg(cfg);
        if (m_wakeupTimer.isActive()) {
            // 运行中修改唤醒周期：立即生效
            m_wakeupTimer.start(m_globalCfg.wakeupMs);
        }
    }
    GlobalConfig globalConfig() const { return m_globalCfg; }

    // ---------- 管理定时器 ----------
    // 创建或更新：若该 id 不存在，则以默认参数创建，再用 cfg 覆盖
    void setTimerConfig(int id, const TimerConfig& cfg) {
        TimerItem& it = ensureItem(id);
        it.cfg = normalizeTimerCfg(cfg);
        // 若 interval 改小/改大，我们不强制重置相位；需要时你可以 reset(id)
    }

    // 只改 interval（最常用）
    void setIntervalMs(int id, int intervalMs) {
        TimerItem& it = ensureItem(id);
        it.cfg.intervalMs = (intervalMs < 1) ? 1 : intervalMs;
    }

    // 获取配置（不存在则返回默认）
    TimerConfig timerConfig(int id) const {
        if (!m_items.contains(id)) return m_defaultCfg;
        return m_items.value(id).cfg;
    }

    // 启用/停用某个 id（不会删除）
    void startTimer(int id) {
        TimerItem& it = ensureItem(id);
        it.cfg.enabled = true;

        if (m_elapsed.isValid()) {
            const qint64 now = m_elapsed.nsecsElapsed();
            if (it.lastFireNs == 0) it.lastFireNs = now;
        }
        ensureWakeupRunning();
        emit timerStarted(id);
    }

    void stopTimer(int id) {
        if (!m_items.contains(id)) return;
        TimerItem& it = m_items[id];
        it.cfg.enabled = false;
        emit timerStopped(id);

        // 如果全部都停了，可选择自动停全局唤醒
        if (!anyEnabled()) {
            stopAll();
        }
    }

    // 删除某个 id（连状态一起删）
    void removeTimer(int id) {
        if (!m_items.contains(id)) return;
        const bool wasEnabled = m_items.value(id).cfg.enabled;
        m_items.remove(id);
        emit timerRemoved(id);

        if (wasEnabled && !anyEnabled()) {
            stopAll();
        }
    }

    bool hasTimer(int id) const { return m_items.contains(id); }

    // 重置相位：从现在开始重新计时（下一次 intervalMs 后触发）
    void resetTimer(int id) {
        if (!m_items.contains(id)) return;
        if (!m_elapsed.isValid()) return;
        m_items[id].lastFireNs = m_elapsed.nsecsElapsed();
    }

    // ---------- 全局启动/停止 ----------
    bool isRunning() const { return m_wakeupTimer.isActive(); }

public slots:
    void startAll() {
        ensureWakeupRunning();
        // 把所有 enabled 的 timer 初始化 lastFireNs（避免刚 start 立即触发）
        const qint64 now = m_elapsed.nsecsElapsed();
        for (auto it = m_items.begin(); it != m_items.end(); ++it) {
            if (it.value().cfg.enabled && it.value().lastFireNs == 0) {
                it.value().lastFireNs = now;
            }
        }
        emit started();
    }

    void stopAll() {
        if (!m_wakeupTimer.isActive()) return;
        m_wakeupTimer.stop();
        emit stopped();
    }

signals:
    // 全局
    void started();
    void stopped();
    void wake(int deltaMs);

    // 单个定时器事件：id 区分
    void timerStarted(int id);
    void timerStopped(int id);
    void timerRemoved(int id);

    // 到点触发：deltaMs = 距离该 id 上次触发的真实间隔（ms）
    void timeout(int id, int deltaMs);

private slots:
    void onWakeup() {
        if (!m_elapsed.isValid()) return;

        const qint64 nowNs = m_elapsed.nsecsElapsed();

        // 全局唤醒间隔（可选）
        if (m_globalCfg.emitWake) {
            int wakeDeltaMs = int((nowNs - m_lastWakeNs) / 1000000);
            if (wakeDeltaMs < 0) wakeDeltaMs = 0;
            emit wake(wakeDeltaMs);
        }
        m_lastWakeNs = nowNs;

        // 扫描所有 enabled 的 timer：到点就触发一次（不补帧）
        const qint64 nowMsNs = nowNs; // just naming
        for (auto it = m_items.begin(); it != m_items.end(); ++it) {
            TimerItem& item = it.value();
            if (!item.cfg.enabled) continue;

            if (item.lastFireNs == 0) {
                // 第一次运行：从现在开始计时
                item.lastFireNs = nowMsNs;
                continue;
            }

            const qint64 sinceFireNs = nowMsNs - item.lastFireNs;
            if (sinceFireNs < 0) continue;

            const qint64 intervalNs = qint64(item.cfg.intervalMs) * 1000000LL;
            if (sinceFireNs >= intervalNs) {
                const int deltaMs = int(sinceFireNs / 1000000);
                item.lastFireNs = nowMsNs;

                emit timeout(it.key(), deltaMs);

                if (item.cfg.oneShot) {
                    item.cfg.enabled = false;
                    emit timerStopped(it.key());
                }
            }
        }

        // 如果没有任何 enabled 的 timer，自动停全局唤醒（可选行为：这里直接停）
        if (!anyEnabled()) {
            stopAll();
        }
    }

private:
    struct TimerItem {
        TimerConfig cfg;
        qint64 lastFireNs = 0; // 上次触发时刻（单调 ns）
    };

    TimerConfig normalizeTimerCfg(TimerConfig c) const {
        if (c.intervalMs < 1) c.intervalMs = 1;
        return c;
    }
    GlobalConfig normalizeGlobalCfg(GlobalConfig c) const {
        if (c.wakeupMs < 1) c.wakeupMs = 1;
        return c;
    }

    TimerItem& ensureItem(int id) {
        if (!m_items.contains(id)) {
            TimerItem item;
            item.cfg = m_defaultCfg; // 默认参数
            item.lastFireNs = 0;
            m_items.insert(id, item);
        }
        return m_items[id];
    }

    void ensureWakeupRunning() {
        if (!m_elapsed.isValid()) {
            m_elapsed.restart();
            m_lastWakeNs = m_elapsed.nsecsElapsed();
        }
        if (!m_wakeupTimer.isActive()) {
            m_wakeupTimer.start(m_globalCfg.wakeupMs);
        }
    }

    bool anyEnabled() const {
        for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
            if (it.value().cfg.enabled) return true;
        }
        return false;
    }

private:
    // 全局
    GlobalConfig m_globalCfg;
    TimerConfig  m_defaultCfg;

    QTimer m_wakeupTimer;
    QElapsedTimer m_elapsed;
    qint64 m_lastWakeNs = 0;

    // id -> timer
    QHash<int, TimerItem> m_items;
};
