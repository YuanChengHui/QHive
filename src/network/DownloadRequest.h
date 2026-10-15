#pragma once

#include "NetworkRequest.h"
#include <QNetworkReply>
#include <QUrl>
#include <QString>

class DownloadRequest : public NetworkRequest
{
	Q_OBJECT
public:
	DownloadRequest(const QUrl& url, qint64 startByte, qint64 endByte,
		const QString& tempFilePath, int chunkId, const QString& taskId,
		QObject* parent = nullptr);
	~DownloadRequest() override;

	void start(QNetworkAccessManager* nam) override;
	void abort() override;

	int chunkId() const { return m_chunkId; }
	QString taskId() const { return m_taskId; }

signals:
	void dataReceived(int chunkId, const QByteArray& data);
	void progress(int chunkId, qint64 received);
	void finished(int chunkId, bool success, const QString& error);

private slots:
	void onReadyRead();
	void onFinished();
	void onErrorOccurred(QNetworkReply::NetworkError code);

private:
	QUrl m_url;
	qint64 m_startByte;
	qint64 m_endByte;
	QString m_tempFilePath;
	int m_chunkId;
	QString m_taskId;

	QNetworkAccessManager* m_nam = nullptr;
	QNetworkReply* m_reply = nullptr;
	qint64 m_receivedBytes = 0;
	bool m_aborted = false;
	bool m_released = false;
};