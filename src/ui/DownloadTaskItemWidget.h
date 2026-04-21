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
		bool supportsRange = true,
		QWidget* parent = nullptr);
	~DownloadTaskItemWidget() override;

	bool isChecked() const;
	bool isPaused() const;
	void setPauseState();
	void setResumeState();
	void setFailedState();
	void setSuccessState();
	void resetForRetry();
	void updateProgress(qint64 received);
	bool isDownloading() const { return !m_isDownloadEnded; }
	void setCheckState(bool newState);
	void setProgressState(qint64 received);

protected:
	void mousePressEvent(QMouseEvent* event) override;

private:
	void initUI();
	void updateSpeed(qint64 delta);
	void updateStatus(const QString& status);
	void showPauseButton(bool show);
	void showResumeButton(bool show);
	void showRetryButton(bool show);
	void showCancelButton(bool show);
	void showOpenFolderButton(bool show);

signals:
	void requestPause(const QString& taskId);
	void requestResume(const QString& taskId);
	void requestCancel(const QString& taskId);
	void requestRetry(const QString& taskId);
	void checkedStateChanged(bool checked);

private slots:
	void on_pauseButton_clicked();
	void on_resumeButton_clicked();
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
	bool m_supportsRange;
	bool m_isPaused;
	bool m_isDownloadEnded;
	QElapsedTimer m_lastTime;
};