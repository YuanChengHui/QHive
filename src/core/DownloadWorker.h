#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>
#include <QFile>
#include <QUrl>

class DownloadWorker : public QObject
{
	Q_OBJECT

public:
	explicit DownloadWorker(int id, QObject* parent = nullptr);
	~DownloadWorker() override;

signals:
	void needChunk();
	void workEnded(int chunkId, bool success, const QString& error);
	void workUpdated(int chunkId, qint64 bytesReceived);
	void workPaused(int chunkId, bool isWorking);


public slots:
	void requestChunk();
	void pause();
	void cancel();
	void startDownload(const QUrl& url, qint64 startByte, qint64 endByte,
		const QString& tempFilePath, int chunkId, qint64 downloadedBytes = 0);

private slots:
	void onReadyRead();
	void onFinished();

private:
	void cleanup();

	int m_id;
	QUrl m_url;	
	QNetworkAccessManager* m_manager;
	bool m_isWorking;
	bool m_canceled;
	bool m_failed;
	bool m_paused;
	
	int m_chunkId;
	qint64 m_startByte;
	qint64 m_endByte;
	qint64 m_downloadedBytes;
	QString m_tempFilePath;
	QFile* m_tempFile;	
	QNetworkReply* m_reply;

	QElapsedTimer m_lastProgressTime;	// 上次发送进度更新的时间，用于节流
	static constexpr int PROGRESS_INTERVAL_MS = 150;
};