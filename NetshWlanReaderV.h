#pragma once

#include <QString>
#include <QStringList>

struct WifiInfo
{
    // ---- WLAN/连接信息 ----
    QString interfaceName;     // WLAN
    QString interfaceDesc;     // Intel(R) Wi-Fi 6 AX203（可选）
    QString state;             // 已连接 / disconnected / connected
    bool    connected = false;

    QString ssid;              // AMNet_VPN_5G
    QString bssid;             // c4:70:ab:20:81:df（AP MAC）
    QString mac;               // 90:09:df:51:20:b1（本机网卡 MAC）

    int     signalPct   = -1;  // 0~100, -1 表示不可用
    int     phyRateMbps = -1;  // 速率(Mbps)/Rate (Mbps)（很多机器没有这行）
    int     rxRateMbps  = -1;  // 接收速率(Mbps)
    int     txRateMbps  = -1;  // 传输速率(Mbps)

    // ---- IP 信息 ----
    QString ipv4;              // 192.168.100.212
    QString gateway;           // 192.168.100.1
};

class NetshWlanReader
{
public:
    // 一次性获取：WiFi + IP（推荐）
    static WifiInfo queryAll(QString *errorText = nullptr);

    // 仅 WiFi（netsh wlan show interfaces）
    static WifiInfo queryWifi(QString *errorText = nullptr);

    // 仅 IP（按 interfaceName 定向解析）
    static bool queryIpForInterface(const QString &interfaceName,
                                    WifiInfo &io,
                                    QString *errorText = nullptr);

    // 列出已保存的 WiFi 配置文件（netsh wlan show profiles）
    static QStringList profiles(QString *errorText = nullptr);

    // 切换 WiFi：name=profile；ssid 可选强制；interface 可选
    static bool connectToProfile(const QString &profileName,
                                 const QString &ssid = QString(),
                                 const QString &interfaceName = QString(),
                                 QString *errorText = nullptr);

    // 断开 WiFi
    static bool disconnect(const QString &interfaceName = QString(),
                           QString *errorText = nullptr);

    // 质量分：0~100（不含状态机，仅作为 UI/告警阈值参考）
    static int rateQuality(const WifiInfo &w);

private:
    static QString runCmd(const QString &cmd, int timeoutMs = 8000);
    static QString valueAfterColon(const QString &line);

    static int parseFirstInt(const QString &s);
    static int parsePercent(const QString &s);

    static bool textIsConnected(const QString &stateText);

    static void parseWlanInterfaces(const QString &output, WifiInfo &info);
    static QStringList parseProfiles(const QString &output);

    static bool looksLikeSuccessConnectOutput(const QString &out);

    // IP 解析：优先 netsh（定向接口），兜底 ipconfig（也尽量按接口段落取）
    static bool parseIpByNetsh(const QString &iface, WifiInfo &io);
    static bool parseIpByIpconfig(const QString &iface, WifiInfo &io);
};
