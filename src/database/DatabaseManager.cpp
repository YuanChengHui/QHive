#include "DatabaseManager.h"

#include <QDir>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

DatabaseManager::DatabaseManager(QObject* parent)
	: QObject(parent)
{
}

DatabaseManager* DatabaseManager::instance()
{
	static DatabaseManager* inst = new DatabaseManager;
	return inst;
}

bool DatabaseManager::init(const QString& dbPath)
{
	QString path = dbPath;
	if (path.isEmpty()) {
		QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		QDir dir(appDataDir);
		if (!dir.exists()) dir.mkpath(".");
		path = dir.filePath("downloads.db");
	}

	m_db = QSqlDatabase::addDatabase("QSQLITE");
	m_db.setDatabaseName(path);

	if (!m_db.open()) {
		qCritical() << "数据库打开失败：" << m_db.lastError().text();
		return false;
	}

	QSqlQuery query(m_db);
	// 创建表
	if (!query.exec(R"(
    CREATE TABLE IF NOT EXISTS tasks (
        task_id TEXT PRIMARY KEY,
        url TEXT NOT NULL,
        file_name TEXT,
        save_path TEXT,
        total_size INTEGER,
        downloaded_bytes INTEGER,
        state INTEGER,
        thread_count INTEGER,
        supports_range INTEGER DEFAULT 1, 
        chunks TEXT,
        created_time INTEGER,
        updated_time INTEGER
    )
)")) {
		qCritical() << "创建表失败：" << query.lastError().text();
		return false;
	}
	qDebug() << "数据库初始化成功，路径：" << path;
	return true;
}

QSqlDatabase DatabaseManager::database() const
{
	return m_db;
}

void DatabaseManager::close()
{
	if (m_db.isOpen()) m_db.close();
}