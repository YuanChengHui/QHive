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
	void initConnections();
	void updateSelectAllAction();

private slots:
	void openDownloadDialog();
	void removeTaskListItem();
	void createDownloadTask(const QString& taskId,
		const QUrl& taskUrl,
		const QString& fileName,
		const QString& fullSavePath,
		qint64 totalSize);
	void handleFailure(const QString& taskId, const QString& savedPath, const QString& errorString);
	void handleSuccess(const QString& taskId, const QString& savedPath);
	void onTaskCancelled(const QString& taskId);
	void onTaskRetry(const QString& taskId);
	void onSelectAllTriggered();
	void onCheckedStateChanged(bool checked);

private:
	Ui::QHiveClass ui;

	QAction* addDownload;
	QAction* removeTask;
	QAction* selectAll;
	HttpClient* httpClient;
	DownloadDialog* downloadDialog;
	QScrollArea* m_downloadListArea;
	QVBoxLayout* m_downloadListLayout;
	int m_selectedCount = 0;
	QHash<QString, DownloadTaskItemWidget*> m_activeDownloads;
	QHash<QString, bool> m_taskResults;
};