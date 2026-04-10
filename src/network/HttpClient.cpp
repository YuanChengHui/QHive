#include "HttpClient.h"
#include "HeadRequestTask.h"

#include <QCoreApplication>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMessageBox>
#include <QFileInfo>
#include <QString>
#include <QThread>
#include <QDebug>

HttpClient::HttpClient(QObject* parent)
	: QObject(parent)
{
}

HttpClient::~HttpClient() = default;

// 根据文件总大小返回推荐的线程数
static int getOptimalThreadCount(qint64 totalSize)
{
	if (totalSize < 10 * 1024 * 1024) return 1;   // <10MB 单线程
	if (totalSize < 50 * 1024 * 1024) return 2;   // <50MB 双线程
	if (totalSize < 200 * 1024 * 1024) return 4;  // <200MB 四线程
	if (totalSize < 1024 * 1024 * 1024) return 8;
	return 16;                           // >= 1 GB
}

HttpClient* HttpClient::instance()
{
	static HttpClient* instance_obj = new HttpClient(QCoreApplication::instance());
	return instance_obj;
}

void HttpClient::headRequest(const QString& taskId, const QUrl& url, const QString& initialFileName, const QString& saveDir)
{
	QThread* headThread = new QThread;
	HeadRequestTask* headTask = new HeadRequestTask(url, initialFileName, saveDir);
	headTask->moveToThread(headThread);

	connect(headThread, &QThread::started, headTask, &HeadRequestTask::startRequest);
	connect(headThread, &QThread::finished, headThread, &QObject::deleteLater);
	connect(headTask, &HeadRequestTask::headRequestCompleted, this,
		[this, taskId, url, headThread, headTask](const QString& fileName,
			const QString& fullSavePath,
			const QString& errorString,
			qint64 totalSize,
			bool hasError,
			bool supportsRange) {
				if (hasError) {
					emit headRequestError(errorString);
				}
				else {
					emit headRequestSuccess(taskId, url, fileName, fullSavePath, totalSize);
					launchDownload(taskId, url, supportsRange, totalSize, fullSavePath);
				}
				headTask->deleteLater();
				headThread->quit();
		});

	headThread->start();
}

void HttpClient::launchDownload(const QString& taskId, const QUrl& url, bool supportsRange, qint64 totalSize, const QString& fullSavePath)
{
	if (totalSize > 0 && supportsRange) {
		int threadCount = getOptimalThreadCount(totalSize);
		if (threadCount > 1) {
			startMultiDownload(taskId, fullSavePath, url, totalSize, threadCount);
		}
		else {
			// 单线程更优，直接走单线程下载
			startSingleDownload(taskId, fullSavePath, url, totalSize);
		}
	}
	else {
		// 不支持断点续传或总大小未知，只能单线程
		startSingleDownload(taskId, fullSavePath, url, totalSize);
	}
}

void HttpClient::startMultiDownload(const QString& taskId, const QString& fullSavePath, const QUrl& taskUrl, qint64 totalSize, int threadCount)
{
	if (m_activeMultiDownloads.contains(taskId)) return;
	auto* downloader = new MultiThreadDownloader(fullSavePath, taskUrl, totalSize, taskId, threadCount, nullptr);
	m_activeMultiDownloads.insert(taskId, downloader);

	connect(downloader, &MultiThreadDownloader::downloadEnded, this, &HttpClient::handleResult);
	connect(downloader, &MultiThreadDownloader::downloadProgressUpdate, this,
		[this](const QString& tid, qint64 received) {
			emit updateDownloadProgress(tid, received);
		});

	downloader->start();
}

void HttpClient::startSingleDownload(const QString& taskId, const QString& fullSavePath, const QUrl& taskUrl, qint64 totalSize)
{
	if (m_activeSingleDownloads.contains(taskId)) return;
	auto* downloader = new SingleThreadDownloader(fullSavePath, taskUrl, totalSize, taskId, nullptr);
	m_activeSingleDownloads.insert(taskId, downloader);

	QThread* downloadThread = new QThread;
	downloader->moveToThread(downloadThread);

	connect(downloadThread, &QThread::started, downloader, &SingleThreadDownloader::start);
	connect(downloadThread, &QThread::finished, downloadThread, &QObject::deleteLater);

	connect(downloader, &SingleThreadDownloader::downloadEnded, this, &HttpClient::handleResult);
	connect(downloader, &SingleThreadDownloader::downloadProgressUpdate, this,
		[this](const QString& tid, qint64 received) {
			emit updateDownloadProgress(tid, received);
		});

	downloadThread->start();
}

void HttpClient::handleResult(const QString& taskId, bool errorOccurred, const QString& errorString, const QString& savePath)
{
	if (errorOccurred) {
		qWarning() << "[HttpClient] 下载失败，任务ID：" << taskId << "\n错误：" << errorString;
		emit downloadFailed(taskId, savePath, errorString);
	}
	else {
		qDebug() << "[HttpClient] 下载成功，任务ID：" << taskId << " 保存路径：" << savePath;
		cleanTaskResources(taskId);
		emit downloadSucceeded(taskId, savePath);
	}
}

MultiThreadDownloader* HttpClient::findMultiDownloadTask(const QString& taskId)
{
	auto it = m_activeMultiDownloads.find(taskId);
	if (it != m_activeMultiDownloads.end()) {
		return it.value();
	}
	return nullptr;
}

SingleThreadDownloader* HttpClient::findSingleDownloadTask(const QString& taskId)
{
	auto it = m_activeSingleDownloads.find(taskId);
	if (it != m_activeSingleDownloads.end()) {
		return it.value();
	}
	return nullptr;
}

void HttpClient::cleanTaskResources(const QString& taskId)
{
	MultiThreadDownloader* multiDownloader = findMultiDownloadTask(taskId);
	if (multiDownloader) {
		m_activeMultiDownloads.remove(taskId);
		multiDownloader->deleteLater();
	}
	else {
		SingleThreadDownloader* singleDownloader = findSingleDownloadTask(taskId);
		if (singleDownloader) {
			m_activeSingleDownloads.remove(taskId);
			QThread* thread = singleDownloader->thread();
			singleDownloader->deleteLater();
			if (thread && thread->isRunning()) {
				thread->quit();
				thread->wait(3000);
			}
		}
		else {
			qWarning() << "[HttpClient] 清理任务资源失败，未找到任务 ID：" << taskId;
		}
	}
}

void HttpClient::cancelDownload(const QString& taskId)
{
	MultiThreadDownloader* multiDownloader = findMultiDownloadTask(taskId);
	if (multiDownloader) {
		multiDownloader->cancel();
	}
	else {
		SingleThreadDownloader* singleDownloader = findSingleDownloadTask(taskId);
		if (singleDownloader) {
			singleDownloader->cancel();
		}
		else {
			qWarning() << "取消下载: 未找到任务 ID:" << taskId;
		}
	}
}

void HttpClient::retryDownload(const QString& taskId)
{
	MultiThreadDownloader* multiDownloader = findMultiDownloadTask(taskId);
	if (multiDownloader) {
		multiDownloader->retry();
	}
	else {
		SingleThreadDownloader* singleDownloader = findSingleDownloadTask(taskId);
		if (singleDownloader) {
			singleDownloader->retry();
		}
		else {
			qWarning() << "重试下载: 未找到任务 ID:" << taskId;
		}
	}
}