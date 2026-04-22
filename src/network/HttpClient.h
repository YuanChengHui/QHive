#pragma once

#include "MultiThreadDownloader.h"
#include "SingleThreadDownloader.h"

#include <QObject>
#include <QUrl>
#include <QHash>
#include <QNetworkAccessManager>

class HttpClient : public QObject
{
	Q_OBJECT

public:
	static HttpClient* instance();

	void restoreDownloadTask(const QString& taskId);
	void pauseDownload(const QString& taskId);
	void resumeDownload(const QString& taskId);
	void cancelDownload(const QString& taskId);
	void retryDownload(const QString& taskId);
	void cleanTaskResources(const QString& taskId);
	void headRequest(const QString& taskId, const QUrl& url, const QString& initialFileName, const QString& saveDir);

signals:
	void headRequestError(const QString& errorString);
	void headRequestSuccess(const QString& taskId,
		const QUrl& url,
		const QString& fileName,
		const QString& fullSavePath,
		qint64 totalSize,
		bool supportsRange);

	void downloadFailed(const QString& taskId, const QString& savePath, const QString& errorString);
	void downloadSucceeded(const QString& taskId, const QString& savePath);
	void updateDownloadProgress(const QString& taskId, qint64 received);
	void downloadPaused(const QString& taskId);
	void downloadResumed(const QString& taskId);

private slots:
	void handleResult(const QString& taskId, bool errorOccurred, const QString& errorString, const QString& savePath);

private:
	explicit HttpClient(QObject* parent = nullptr);
	~HttpClient() = default;
	HttpClient(const HttpClient&) = delete;
	HttpClient& operator=(const HttpClient&) = delete;

	QNetworkAccessManager* m_networkManager = nullptr;

	void launchDownload(const QString& taskId,
		const QUrl& url,
		bool supportsRange,
		qint64 totalSize,
		const QString& fullSavePath);

	MultiThreadDownloader* createMultiDownloader(const QString& taskId,
		const QString& fullSavePath,
		const QUrl& url,
		qint64 totalSize,
		int threadCount);

	SingleThreadDownloader* createSingleDownloader(const QString& taskId,
		const QString& fullSavePath,
		const QUrl& url,
		qint64 totalSize,
		bool supportsRange);

	void startMultiDownload(const QString& taskId,
		const QString& fullSavePath,
		const QUrl& taskUrl,
		qint64 totalSize,
		int threadCount);

	void startSingleDownload(const QString& taskId,
		const QString& fullSavePath,
		const QUrl& taskUrl,
		qint64 totalSize,
		bool supportsRange);

	MultiThreadDownloader* findMultiDownloadTask(const QString& taskId);
	SingleThreadDownloader* findSingleDownloadTask(const QString& taskId);

	QHash<QString, MultiThreadDownloader*> m_activeMultiDownloads;
	QHash<QString, SingleThreadDownloader*> m_activeSingleDownloads;
};