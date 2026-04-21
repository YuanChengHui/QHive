#pragma once

#include <QObject>
#include <QSqlDatabase>

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    static DatabaseManager* instance();
    bool init(const QString& dbPath = QString());
    QSqlDatabase database() const;
    void close();

private:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() = default;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    QSqlDatabase m_db;
};