#include "TaskRepository.h"
#include "DatabaseManager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

bool TaskRepository::saveTask(const TaskInfo& task)
{
	QSqlDatabase db = DatabaseManager::instance()->database();
	if (!db.isOpen()) return false;

	QSqlQuery query(db);
	query.prepare(R"(
        INSERT INTO tasks 
        (task_id, url, file_name, save_path, total_size, downloaded_bytes, state, 
         thread_count, supports_range, chunks, created_time, updated_time)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
	query.addBindValue(task.taskId);
	query.addBindValue(task.url);
	query.addBindValue(task.fileName);
	query.addBindValue(task.savePath);
	query.addBindValue(task.totalSize);
	query.addBindValue(task.downloadedBytes);
	query.addBindValue(task.state);
	query.addBindValue(task.threadCount);
	query.addBindValue(task.supportsRange ? 1 : 0);
	query.addBindValue(task.chunksJson);
	qint64 now = QDateTime::currentSecsSinceEpoch();
	query.addBindValue(now);
	query.addBindValue(now);

	if (!query.exec()) {
		qWarning() << "保存任务失败:" << query.lastError().text();
		return false;
	}
	return true;
}

bool TaskRepository::deleteTask(const QString& taskId)
{
	QSqlDatabase db = DatabaseManager::instance()->database();
	if (!db.isOpen()) return false;

	QSqlQuery query(db);
	query.prepare("DELETE FROM tasks WHERE task_id = ?");
	query.addBindValue(taskId);

	if (!query.exec()) {
		qWarning() << "删除任务失败:" << query.lastError().text();
		return false;
	}
	return true;
}

QList<TaskRepository::TaskInfo> TaskRepository::loadAllTasks()
{
	QList<TaskInfo> tasks;
	QSqlDatabase db = DatabaseManager::instance()->database();
	if (!db.isOpen()) return tasks;

	QSqlQuery query("SELECT * FROM tasks", db);
	while (query.next()) {
		TaskInfo info;
		info.taskId = query.value("task_id").toString();
		info.url = query.value("url").toString();
		info.fileName = query.value("file_name").toString();
		info.savePath = query.value("save_path").toString();
		info.totalSize = query.value("total_size").toLongLong();
		info.downloadedBytes = query.value("downloaded_bytes").toLongLong();
		info.state = query.value("state").toInt();
		info.threadCount = query.value("thread_count").toInt();
		info.supportsRange = query.value("supports_range").toInt() != 0;
		info.chunksJson = query.value("chunks").toString();
		info.createdTime = query.value("created_time").toLongLong();
		info.updatedTime = query.value("updated_time").toLongLong();
		tasks.append(info);
	}
	return tasks;
}

TaskRepository::TaskInfo TaskRepository::loadTask(const QString& taskId)
{
	TaskInfo info;
	QSqlDatabase db = DatabaseManager::instance()->database();
	if (!db.isOpen()) return info;

	QSqlQuery query(db);
	query.prepare("SELECT * FROM tasks WHERE task_id = ?");
	query.addBindValue(taskId);
	if (query.exec() && query.next()) {
		info.taskId = query.value("task_id").toString();
		info.url = query.value("url").toString();
		info.fileName = query.value("file_name").toString();
		info.savePath = query.value("save_path").toString();
		info.totalSize = query.value("total_size").toLongLong();
		info.downloadedBytes = query.value("downloaded_bytes").toLongLong();
		info.state = query.value("state").toInt();
		info.threadCount = query.value("thread_count").toInt();
		info.supportsRange = query.value("supports_range").toInt() != 0;
		info.chunksJson = query.value("chunks").toString();
		info.createdTime = query.value("created_time").toLongLong();
		info.updatedTime = query.value("updated_time").toLongLong();
	}
	return info;
}

bool TaskRepository::updateChunks(const QString& taskId, const QString& chunksJson)
{
	QSqlDatabase db = DatabaseManager::instance()->database();
	if (!db.isOpen()) return false;
	QSqlQuery query(db);
	query.prepare("UPDATE tasks SET chunks = ?, updated_time = ? WHERE task_id = ?");
	query.addBindValue(chunksJson);
	query.addBindValue(QDateTime::currentSecsSinceEpoch());
	query.addBindValue(taskId);
	return query.exec();
}

bool TaskRepository::updateProgress(const QString& taskId, qint64 downloadedBytes)
{
	QSqlDatabase db = DatabaseManager::instance()->database();
	if (!db.isOpen()) return false;

	QSqlQuery query(db);
	query.prepare("UPDATE tasks SET downloaded_bytes = ?, updated_time = ? WHERE task_id = ?");
	query.addBindValue(downloadedBytes);
	query.addBindValue(QDateTime::currentSecsSinceEpoch());
	query.addBindValue(taskId);

	return query.exec();
}

bool TaskRepository::updateState(const QString& taskId, int state)
{
	QSqlDatabase db = DatabaseManager::instance()->database();
	if (!db.isOpen()) return false;

	QSqlQuery query(db);
	query.prepare("UPDATE tasks SET state = ?, updated_time = ? WHERE task_id = ?");
	query.addBindValue(state);
	query.addBindValue(QDateTime::currentSecsSinceEpoch());
	query.addBindValue(taskId);

	return query.exec();
}