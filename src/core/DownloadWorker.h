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
	void chunkFinished(int chunkId, bool success, const QString& error);
	void chunkProgress(int chunkId, qint64 bytesReceived);

public slots:
	void startWorking();
	void cancel();
	void startDownload(const QUrl& url, qint64 startByte, qint64 endByte,
		const QString& tempFilePath, int chunkId);

private slots:
	void onReadyRead();
	void onFinished();

private:
	int m_id;
	QNetworkAccessManager* m_manager;
	QNetworkReply* m_reply;
	QUrl m_url;
	qint64 m_startByte;
	qint64 m_endByte;
	qint64 m_downloadedBytes;
	QFile* m_file;
	QString m_tempFilePath;
	int m_chunkId;
	bool m_cancelled;
	bool m_running;

	QElapsedTimer m_lastProgressTime;
	static constexpr int PROGRESS_INTERVAL_MS = 150;
};