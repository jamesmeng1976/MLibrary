#include "NetshWlanReaderV.h"
#include <QProcess>
#include <QThread>
#include <QtGlobal>

QString NetshWlanReader::runCmd(const QString &cmd, int timeoutMs)
{
    QProcess p;
    p.start("cmd", {"/c", cmd});
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return QString();
    }
    return QString::fromLocal8Bit(p.readAllStandardOutput());
}

QString NetshWlanReader::valueAfterColon(const QString &line)
{
    int idx = line.indexOf(':');
    if (idx < 0) idx = line.indexOf(QChar(0xFF1A)); // 全角：
    if (idx < 0) return QString();
    return line.mid(idx + 1).trimmed();
}

int NetshWlanReader::parseFirstInt(const QString &s)
{
    QString t = s.trimmed();
    QString num;
    for (QChar c : t) {
        if (c.isDigit()) num.append(c);
        else if (!num.isEmpty()) break;
    }
    return num.isEmpty() ? -1 : num.toInt();
}

int NetshWlanReader::parsePercent(const QString &s)
{
    return parseFirstInt(s);
}

bool NetshWlanReader::textIsConnected(const QString &stateText)
{
    if (stateText.contains("已连接")) return true;
    if (stateText.contains("connected", Qt::CaseInsensitive)) return true;
    return false;
}

void NetshWlanReader::parseWlanInterfaces(const QString &output, WifiInfo &info)
{
    // 防止残留
    info.signalPct   = -1;
    info.phyRateMbps = -1;
    info.rxRateMbps  = -1;
    info.txRateMbps  = -1;

    const QStringList lines = output.split('\n');

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("名称") || line.startsWith("Name")) {
            info.interfaceName = valueAfterColon(line);
            continue;
        }

        if (line.startsWith("说明") || line.startsWith("Description")) {
            info.interfaceDesc = valueAfterColon(line);
            continue;
        }

        if (line.startsWith("状态") || line.startsWith("State")) {
            info.state = valueAfterColon(line);
            info.connected = textIsConnected(info.state);
            continue;
        }

        if (line.startsWith("SSID") && !line.startsWith("BSSID")) {
            info.ssid = valueAfterColon(line);
            continue;
        }

        if (line.startsWith("BSSID")) {
            info.bssid = valueAfterColon(line);
            continue;
        }

        if (line.startsWith("物理地址") || line.startsWith("Physical address")) {
            info.mac = valueAfterColon(line);
            continue;
        }

        if (line.startsWith("信号") || line.startsWith("Signal")) {
            info.signalPct = parsePercent(valueAfterColon(line));
            continue;
        }

        // ✅ 接收速率(Mbps)
        if (line.startsWith("接收速率") || line.startsWith("Receive rate")) {
            if (info.rxRateMbps < 0)
                info.rxRateMbps = parseFirstInt(valueAfterColon(line));
            continue;
        }

        // ✅ 传输速率(Mbps)
        if (line.startsWith("传输速率") || line.startsWith("Transmit rate")) {
            if (info.txRateMbps < 0)
                info.txRateMbps = parseFirstInt(valueAfterColon(line));
            continue;
        }

        // ✅ PHY/协商速率(Mbps)（很多机器没有这行，留空很正常）
        if (line.startsWith("速率") || line.startsWith("Rate")) {
            // 双保险：避免未来字段名变化误伤 RX/TX
            if (!line.startsWith("接收速率") && !line.startsWith("传输速率") &&
                !line.startsWith("Receive rate") && !line.startsWith("Transmit rate")) {
                if (info.phyRateMbps < 0)
                    info.phyRateMbps = parseFirstInt(valueAfterColon(line));
            }
            continue;
        }
    }

    // 兜底：有 SSID 就认为连接（有些系统 state 不稳定）
    if (!info.connected && !info.ssid.isEmpty() && info.ssid != "-") {
        info.connected = true;
        if (info.state.isEmpty()) info.state = "已连接";
    }
}

WifiInfo NetshWlanReader::queryWifi(QString *errorText)
{
    WifiInfo info;
    const QString out = runCmd("netsh wlan show interfaces", 8000);
    if (out.isEmpty()) {
        if (errorText) *errorText = "netsh wlan show interfaces 无输出（可能超时或 WLAN 服务异常）";
        return info;
    }
    parseWlanInterfaces(out, info);
    return info;
}

bool NetshWlanReader::parseIpByNetsh(const QString &iface, WifiInfo &io)
{
    QString out = runCmd(QString("netsh interface ipv4 show config name=\"%1\"").arg(iface), 8000);
    if (out.isEmpty())
        out = runCmd(QString("netsh interface ip show config name=\"%1\"").arg(iface), 8000);
    if (out.isEmpty())
        return false;

    const QStringList lines = out.split('\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;

        if (line.contains("IP 地址") || line.contains("IP Address")) {
            const QString v = valueAfterColon(line);
            if (!v.isEmpty()) io.ipv4 = v;
            continue;
        }

        if (line.contains("默认网关") || line.contains("Default Gateway")) {
            const QString v = valueAfterColon(line);
            if (!v.isEmpty() && v != "无") io.gateway = v;
            continue;
        }
    }

    return !io.ipv4.isEmpty() || !io.gateway.isEmpty();
}

bool NetshWlanReader::parseIpByIpconfig(const QString &iface, WifiInfo &io)
{
    const QString out = runCmd("ipconfig", 8000);
    if (out.isEmpty()) return false;

    const QStringList lines = out.split('\n');
    int start = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].contains(iface, Qt::CaseInsensitive)) {
            start = i;
            break;
        }
    }
    if (start < 0) return false;

    int end = lines.size();
    for (int i = start + 1; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty()) { end = i; break; }
        if (t.contains("适配器") || t.contains("adapter", Qt::CaseInsensitive)) { end = i; break; }
    }

    for (int i = start; i < end; ++i) {
        QString line = lines[i].trimmed();
        if (line.isEmpty()) continue;

        if (line.contains("IPv4") || line.contains("IPv4 Address")) {
            io.ipv4 = valueAfterColon(line);
            continue;
        }
        if (line.contains("默认网关") || line.contains("Default Gateway")) {
            const QString v = valueAfterColon(line);
            if (!v.isEmpty() && v != "无") io.gateway = v;
            continue;
        }
    }
    return !io.ipv4.isEmpty() || !io.gateway.isEmpty();
}

bool NetshWlanReader::queryIpForInterface(const QString &interfaceName, WifiInfo &io, QString *errorText)
{
    if (interfaceName.trimmed().isEmpty()) {
        if (errorText) *errorText = "interfaceName 为空，无法定向解析 IP/网关";
        return false;
    }

    io.ipv4.clear();
    io.gateway.clear();

    if (parseIpByNetsh(interfaceName, io))
        return true;

    if (parseIpByIpconfig(interfaceName, io))
        return true;

    if (errorText) *errorText = "未能解析到 IPv4/默认网关（netsh/ipconfig 均失败）。";
    return false;
}

WifiInfo NetshWlanReader::queryAll(QString *errorText)
{
    WifiInfo info = queryWifi(errorText);
    if (!info.interfaceName.isEmpty()) {
        QString ipErr;
        queryIpForInterface(info.interfaceName, info, &ipErr);
        (void)ipErr; // IP 失败不影响 WiFi 主信息
    }
    return info;
}

QStringList NetshWlanReader::parseProfiles(const QString &output)
{
    QStringList list;
    const QStringList lines = output.split('\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) continue;

        if (line.contains("所有用户配置文件") || line.contains("All User Profile")) {
            const QString name = valueAfterColon(line);
            if (!name.isEmpty()) list << name;
        }
    }
    list.removeDuplicates();
    return list;
}

QStringList NetshWlanReader::profiles(QString *errorText)
{
    const QString out = runCmd("netsh wlan show profiles", 8000);
    if (out.isEmpty()) {
        if (errorText) *errorText = "netsh wlan show profiles 无输出";
        return {};
    }
    const QStringList ps = parseProfiles(out);
    if (ps.isEmpty() && errorText)
        *errorText = "未解析到 WiFi 配置文件（可能未保存过 WiFi）。";
    return ps;
}

bool NetshWlanReader::looksLikeSuccessConnectOutput(const QString &out)
{
    if (out.contains("已成功", Qt::CaseInsensitive)) return true;
    if (out.contains("successfully", Qt::CaseInsensitive)) return true;
    return false;
}

bool NetshWlanReader::connectToProfile(const QString &profileName,
                                       const QString &ssid,
                                       const QString &interfaceName,
                                       QString *errorText)
{
    if (profileName.trimmed().isEmpty()) {
        if (errorText) *errorText = "profileName 不能为空";
        return false;
    }

    QString cmd = QString("netsh wlan connect name=\"%1\"").arg(profileName);
    if (!ssid.trimmed().isEmpty())
        cmd += QString(" ssid=\"%1\"").arg(ssid);
    if (!interfaceName.trimmed().isEmpty())
        cmd += QString(" interface=\"%1\"").arg(interfaceName);

    const QString out = runCmd(cmd, 12000);
    if (looksLikeSuccessConnectOutput(out))
        return true;

    // 二次确认：轮询 SSID 是否变更（给系统一点时间切换）
    const QString target = ssid.trimmed();
    for (int i = 0; i < 10; ++i) {
        QThread::msleep(300);
        WifiInfo cur = queryWifi();
        if (!target.isEmpty()) {
            if (cur.ssid.compare(target, Qt::CaseInsensitive) == 0)
                return true;
        } else {
            if (cur.connected && !cur.ssid.isEmpty() && cur.ssid != "-")
                return true;
        }
    }

    if (errorText) {
        QString msg = out.trimmed();
        if (msg.isEmpty()) msg = "连接失败（无命令输出）。建议：传入 ssid 参数或检查 profile 是否可用。";
        *errorText = msg;
    }
    return false;
}

bool NetshWlanReader::disconnect(const QString &interfaceName, QString *errorText)
{
    QString cmd = "netsh wlan disconnect";
    if (!interfaceName.trimmed().isEmpty())
        cmd += QString(" interface=\"%1\"").arg(interfaceName);

    const QString out = runCmd(cmd, 8000);

    if (out.contains("断开", Qt::CaseInsensitive) ||
        out.contains("disconnected", Qt::CaseInsensitive)) {
        return true;
    }

    WifiInfo cur = queryWifi();
    if (!cur.connected)
        return true;

    if (errorText) {
        QString msg = out.trimmed();
        if (msg.isEmpty()) msg = "断开失败（无命令输出）。";
        *errorText = msg;
    }
    return false;
}

static int clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int NetshWlanReader::rateQuality(const WifiInfo &w)
{
    if (!w.connected) return 0;

    // 信号占 50 分
    int s = (w.signalPct < 0) ? 0 : clamp100(w.signalPct);
    int scoreSignal = (s * 50) / 100;

    // 速率占 50 分：用 min(rx,tx) 作为短板；缺失则 fallback
    int bottleneck = -1;
    if (w.rxRateMbps >= 0 && w.txRateMbps >= 0) bottleneck = qMin(w.rxRateMbps, w.txRateMbps);
    else if (w.rxRateMbps >= 0) bottleneck = w.rxRateMbps;
    else if (w.txRateMbps >= 0) bottleneck = w.txRateMbps;
    else if (w.phyRateMbps >= 0) bottleneck = w.phyRateMbps;

    int scoreRate = 0;
    if (bottleneck > 0) {
        const int x = qMin(bottleneck, 1200);
        if (x < 20) scoreRate = 5;
        else if (x < 50) scoreRate = 15;
        else if (x < 150) scoreRate = 25;
        else if (x < 300) scoreRate = 32;
        else if (x < 500) scoreRate = 38;
        else if (x < 866) scoreRate = 45;
        else scoreRate = 50;
    }

    return clamp100(scoreSignal + scoreRate);
}
