#pragma once

#include "ui_QHive.h"
#include "HttpClient.h"
#include "DownloadDialog.h"
#include "DownloadTaskItemWidget.h"

#include <QMainWindow>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QAction>
#include <QHash>

class QHive : public QMainWindow
{
	Q_OBJECT

public:
	explicit QHive(QWidget* parent = nullptr);
	~QHive() override;

private:
	void initToolbar();
	void initConnections();
	void updateSelectAllAction();

private slots:
	void openDownloadDialog();
	void pauseAllTasks();
	void resumeAllTasks();	
	void removeTaskListItem();
	void createDownloadTask(const QString& taskId, const QUrl& taskUrl,
		const QString& fileName, const QString& fullSavePath,
		qint64 totalSize, bool supportsRange);
	void loadUnfinishedTasks();
	void handleTaskPause(const QString& taskId);
	void handleTaskResume(const QString& taskId);
	void handleFailure(const QString& taskId, const QString& savedPath, const QString& errorString);
	void handleSuccess(const QString& taskId, const QString& savedPath);
	void onTaskPause(const QString& taskId);
	void onTaskResume(const QString& taskId);
	void onTaskCancell(const QString& taskId);
	void onTaskRetry(const QString& taskId);
	void onSelectAllTriggered();
	void onCheckedStateChanged(bool checked);

private:
	Ui::QHiveClass ui;

	QAction* addDownload;
	QAction* removeTask;
	QAction* selectAll;
	QAction* pauseDownload;
	QAction* resumeDownload;
	HttpClient* httpClient;
	DownloadDialog* downloadDialog;
	QScrollArea* m_downloadListArea;
	QVBoxLayout* m_downloadListLayout;
	int m_selectedCount = 0;	// 当前选中的任务数量
	QHash<QString, DownloadTaskItemWidget*> m_activeDownloads;
	QHash<QString, bool> m_taskResults;	// 记录每个任务的结果状态，true 表示成功，false 表示失败或未完成
};