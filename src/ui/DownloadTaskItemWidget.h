#pragma once

#include "ui_DownloadTaskItemWidget.h"

#include <QWidget>
#include <QUrl>
#include <QMouseEvent>
#include <QElapsedTimer>

class DownloadTaskItemWidget : public QWidget
{
	Q_OBJECT

public:
	explicit DownloadTaskItemWidget(const QString& taskId,
		const QUrl& url,
		const QString& fileName,
		const QString& savePath,
		qint64 totalSize,
		QWidget* parent = nullptr);
	~DownloadTaskItemWidget() override;

	bool isChecked();
	void setFailedUI();
	void setSuccessUI();
	void resetForRetry();
	void updateProgress(qint64 received);
	bool isDownloading() const { return m_isDownloading; }
	void setCheckState(bool newState);

protected:
	void mousePressEvent(QMouseEvent* event) override;

private:
	void initUI();
	void updateSpeed(qint64 delta);
	void updateStatus(const QString& status);
	void showRetryButton(bool show);
	void showCancelButton(bool show);
	void showOpenFolderButton(bool show);

signals:
	void requestCancel(const QString& taskId);
	void requestRetry(const QString& taskId);
	void checkedStateChanged(bool checked);

private slots:
	void on_cancelButton_clicked();
	void on_retryButton_clicked();
	void on_openFolderButton_clicked();

private:
	Ui::DownloadTaskItemWidgetClass ui;
	QUrl m_taskUrl;
	QString m_taskId;
	QString m_fileName;
	QString m_savedPath;
	qint64 m_totalSize = 0;
	qint64 m_lastReceived = 0;
	bool m_isDownloading;
	QElapsedTimer m_lastTime;
};