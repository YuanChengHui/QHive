#pragma once

#include <QObject>
#include <QUrl>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkAccessManager>

class SingleThreadDownloader : public QObject
{
	Q_OBJECT

public:
	explicit SingleThreadDownloader(const QString& fullSavePath,
		const QUrl& url,
		qint64 totalSize,
		const QString& taskId,
		QObject* parent = nullptr);
	~SingleThreadDownloader() override;

signals:
	void downloadProgressUpdate(const QString& taskId, qint64 received);
	void downloadEnded(const QString& taskId, bool errorOccurred, const QString& errorString, const QString& savePath);

public slots:
	void start();
	void cancel();
	void retry();

private slots:
	void onReadyRead();
	void onFinished();

private:
	void startNetworkRequest();
	void resetState();

private:
	QUrl m_url;
	QString m_taskId;
	qint64 m_totalSize;
	QString m_fullSavePath;
	QNetworkAccessManager* m_networkManager;

	QFile* m_file;
	QNetworkReply* m_reply;
	QString m_errorString;
	bool m_errorOccurred;
	qint64 m_bytesReceived;
	bool m_requestAborted;
};