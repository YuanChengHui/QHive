#pragma once

#include <QObject>
#include <QUrl>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QElapsedTimer>
#include <QTimer>

class SingleThreadDownloader : public QObject
{
	Q_OBJECT

public:
	explicit SingleThreadDownloader(const QString& fullSavePath,
		const QUrl& url,
		qint64 totalSize,
		const QString& taskId,
		bool supportsRange,
		QNetworkAccessManager* networkManager,
		QObject* parent = nullptr);
	~SingleThreadDownloader() override;

	void restorePausedState(qint64 downloadedBytes);
	void restoreFailedState(qint64 downloadedBytes);
	void restoreDownloadingState(qint64 downloadedBytes);

signals:
	// UI 进度信号
	void downloadProgressUpdate(const QString& taskId, qint64 received);
	// 下载结束信号
	void downloadEnded(const QString& taskId, bool errorOccurred, const QString& errorString, const QString& savePath);
	void downloadPaused(const QString& taskId);
	void downloadResumed(const QString& taskId);

public slots:
	void start();
	void pause();
	void resume();
	void cancel();
	void retry();

private slots:
	void onReadyRead();
	void onFinished();
	void retryRequest();

private:
	void startNetworkRequest(qint64 startByte);
	void cleanupFile();

	QUrl m_url;
	QString m_taskId;
	qint64 m_totalSize;
	QString m_fullSavePath;
	qint64 m_bytesReceived;
	bool m_supportsRange;
	bool m_isResumable = false;
	bool m_isDownloadEnded = false;
	bool m_isPaused = false;
	bool m_isCanceled = false;
	bool m_errorOccurred = false;
	bool m_requestAborted = false;
	QString m_errorString;

	QFile* m_file;
	QNetworkReply* m_reply;
	QNetworkAccessManager* m_networkManager;

	QElapsedTimer m_dbSaveTimer;
	static constexpr int DB_SAVE_INTERVAL_MS = 2000;
	QElapsedTimer m_lastProgressTime;
	static constexpr int PROGRESS_INTERVAL_MS = 150;

	int m_retryCount = 0;
	bool m_retryScheduled = false;
	static constexpr int MAX_RETRIES = 3;
	QTimer m_retryTimer;
};