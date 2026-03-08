#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QVector>
#include <QDebug>

class SQLiteManager {
public:
    static SQLiteManager& getInstance() {
        static SQLiteManager instance;
        return instance;
    }

    // 初始化连接池
    void initPool(const QString& dbPath, int maxConnections = 5) {
        QMutexLocker locker(&m_poolMutex);
        m_dbPath = dbPath;
        m_maxConnections = maxConnections;
        
        // 预创建连接
        for (int i = 0; i < m_maxConnections; ++i) {
            QString connName = QString("conn_%1").arg(i);
            QSqlDatabase db = createConnection(connName);
            if (db.isOpen()) {
                m_connections.append(db);
            }
        }
    }

    // 获取数据库连接
    QSqlDatabase getConnection() {
        QMutexLocker locker(&m_poolMutex);
        if (!m_connections.isEmpty()) {
            QSqlDatabase db = m_connections.takeFirst();
            if (!db.isOpen()) {
                db = createConnection(db.connectionName());
            }
            return db;
        }
        
        // 连接池耗尽时创建新连接
        QString connName = QString("conn_%1").arg(m_connections.size());
        return createConnection(connName);
    }

    // 释放连接
    void releaseConnection(QSqlDatabase db) {
        QMutexLocker locker(&m_poolMutex);
        if (m_connections.size() < m_maxConnections) {
            m_connections.prepend(db);
        } else {
            db.close();
        }
    }

    // 执行SQL语句
    bool executeSQL(const QString& sql, QSqlDatabase db) {
        QSqlQuery query(db);
        if (!query.exec(sql)) {
            qWarning() << "SQL error:" << query.lastError().text();
            return false;
        }
        return true;
    }

    // 查询操作
    QSqlQuery querySQL(const QString& sql, QSqlDatabase db) {
        QSqlQuery query(db);
        if (!query.exec(sql)) {
            qWarning() << "Query error:" << query.lastError().text();
        }
        return query;
    }

private:
    SQLiteManager() {} // 私有构造函数
    ~SQLiteManager() {
        // 清理所有连接
        QMutexLocker locker(&m_poolMutex);
        for (auto& db : m_connections) {
            if (db.isOpen()) {
                QSqlDatabase::removeDatabase(db.connectionName());
            }
        }
    }

    QSqlDatabase createConnection(const QString& connName) {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            qCritical() << "Failed to open database:" << db.lastError().text();
            return QSqlDatabase();
        }
        return db;
    }

    QString m_dbPath;
    int m_maxConnections;
    QVector<QSqlDatabase> m_connections;
    QMutex m_poolMutex;
};