#pragma once

#include <QString>
#include <QList>

class TaskRepository
{
public:
	// 任务状态枚举	
	enum class TaskState {
		Waiting = 0,
		Downloading = 1,
		Paused = 2,
		Completed = 3,
		Failed = 4
	};
	// 任务信息结构体
	struct TaskInfo {
		QString taskId;
		QString url;
		QString fileName;
		QString savePath;
		qint64 totalSize = -1;
		qint64 downloadedBytes = 0;
		int state = 0;
		int threadCount = 1;
		bool supportsRange = true;
		QString chunksJson;
		qint64 createdTime = 0;
		qint64 updatedTime = 0;
	};

	TaskRepository() = default;

	bool saveTask(const TaskInfo& task);
	bool deleteTask(const QString& taskId);
	QList<TaskInfo> loadAllTasks();
	TaskInfo loadTask(const QString& taskId);
	bool updateChunks(const QString& taskId, const QString& chunksJson);
	bool updateProgress(const QString& taskId, qint64 downloadedBytes);
	bool updateState(const QString& taskId, int state);
};