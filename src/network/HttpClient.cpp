#include "HttpClient.h"
#include "HeadRequestTask.h"
#include "TaskRepository.h"

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

static int getOptimalThreadCount(qint64 totalSize)
{
	if (totalSize < 10 * 1024 * 1024) return 1;
	if (totalSize < 50 * 1024 * 1024) return 2;
	if (totalSize < 200 * 1024 * 1024) return 4;
	if (totalSize < 1024 * 1024 * 1024) return 8;
	return 16;
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
		[this, taskId, url, headThread, headTask](const QString& fileName, const QString& fullSavePath,
			const QString& errorString, qint64 totalSize, bool hasError, bool supportsRange) {
				if (hasError) {
					emit headRequestError(errorString);
				}
				else {
					emit headRequestSuccess(taskId, url, fileName, fullSavePath, totalSize, supportsRange);
					launchDownload(taskId, url, supportsRange, totalSize, fullSavePath);
				}
				headTask->deleteLater();
				headThread->quit();
		});

	headThread->start();
}

void HttpClient::launchDownload(const QString& taskId, const QUrl& url, bool supportsRange, qint64 totalSize, const QString& fullSavePath)
{
	TaskRepository repo;
	TaskRepository::TaskInfo info;
	info.taskId = taskId;
	info.url = url.toString();
	info.fileName = QFileInfo(fullSavePath).fileName();
	info.savePath = fullSavePath;
	info.totalSize = totalSize;
	info.state = static_cast<int>(TaskRepository::TaskState::Downloading);
	info.supportsRange = supportsRange;
	info.threadCount = (totalSize > 0 && supportsRange) ? getOptimalThreadCount(totalSize) : 1;
	repo.saveTask(info);

	if (totalSize > 0 && supportsRange) {
		int threadCount = getOptimalThreadCount(totalSize);
		if (threadCount > 1) {
			startMultiDownload(taskId, fullSavePath, url, totalSize, threadCount);
		}
		else {
			startSingleDownload(taskId, fullSavePath, url, totalSize, supportsRange);
		}
	}
	else {
		startSingleDownload(taskId, fullSavePath, url, totalSize, supportsRange);
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

MultiThreadDownloader* HttpClient::createMultiDownloader(const QString& taskId,
	const QString& fullSavePath,
	const QUrl& url,
	qint64 totalSize,
	int threadCount)
{
	auto* downloader = new MultiThreadDownloader(fullSavePath, url, totalSize, taskId, threadCount, nullptr);

	connect(downloader, &MultiThreadDownloader::downloadEnded, this, &HttpClient::handleResult);
	connect(downloader, &MultiThreadDownloader::downloadProgressUpdate, this,
		[this](const QString& tid, qint64 received) {
			emit updateDownloadProgress(tid, received);
		});
	connect(downloader, &MultiThreadDownloader::paused, this, [this](const QString& tid) {
		emit downloadPaused(tid);
		});
	connect(downloader, &MultiThreadDownloader::resumed, this, [this](const QString& tid) {
		emit downloadResumed(tid);
		});

	return downloader;
}

SingleThreadDownloader* HttpClient::createSingleDownloader(const QString& taskId,
	const QString& fullSavePath,
	const QUrl& url,
	qint64 totalSize,
	bool supportsRange)
{
	auto* downloader = new SingleThreadDownloader(fullSavePath, url, totalSize, taskId, supportsRange, nullptr);

	QThread* thread = new QThread;
	downloader->moveToThread(thread);

	connect(thread, &QThread::started, downloader, &SingleThreadDownloader::start);
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	connect(downloader, &SingleThreadDownloader::downloadPaused, this, [this](const QString& tid) {
		emit downloadPaused(tid);
		});
	connect(downloader, &SingleThreadDownloader::downloadResumed, this, [this](const QString& tid) {
		emit downloadResumed(tid);
		});
	connect(downloader, &SingleThreadDownloader::downloadEnded, this, &HttpClient::handleResult);
	connect(downloader, &SingleThreadDownloader::downloadProgressUpdate, this,
		[this](const QString& tid, qint64 received) {
			emit updateDownloadProgress(tid, received);
		});

	connect(downloader, &SingleThreadDownloader::needSaveProgress,
		this, [this](const QString& tid, qint64 progress) {
			TaskRepository().updateProgress(tid, progress);
		});

	connect(downloader, &SingleThreadDownloader::needSaveState,
		this, [this](const QString& tid, int state) {
			TaskRepository().updateState(tid, state);
		});

	return downloader;
}

void HttpClient::startMultiDownload(const QString& taskId, const QString& fullSavePath,
	const QUrl& taskUrl, qint64 totalSize, int threadCount)
{
	if (m_activeMultiDownloads.contains(taskId)) return;
	auto* downloader = createMultiDownloader(taskId, fullSavePath, taskUrl, totalSize, threadCount);
	m_activeMultiDownloads.insert(taskId, downloader);
	downloader->start();
}

void HttpClient::startSingleDownload(const QString& taskId, const QString& fullSavePath,
	const QUrl& taskUrl, qint64 totalSize, bool supportsRange)
{
	if (m_activeSingleDownloads.contains(taskId)) return;
	auto* downloader = createSingleDownloader(taskId, fullSavePath, taskUrl, totalSize, supportsRange);
	m_activeSingleDownloads.insert(taskId, downloader);
	downloader->thread()->start();
}

void HttpClient::restoreDownloadTask(const QString& taskId)
{
	TaskRepository repo;
	TaskRepository::TaskInfo info = repo.loadTask(taskId);
	if (info.taskId.isEmpty()) return;

	if (info.totalSize > 0 && info.supportsRange && info.threadCount > 1) {
		if (info.state == static_cast<int>(TaskRepository::TaskState::Completed)) {
			return;		// 已完成状态，直接返回
		}
		// 多线程恢复
		auto* downloader = createMultiDownloader(taskId, info.savePath, info.url, info.totalSize, info.threadCount);
		m_activeMultiDownloads.insert(taskId, downloader);
		if (info.state == static_cast<int>(TaskRepository::TaskState::Paused)) {
			downloader->restorePausedState();
		}
		else if (info.state == static_cast<int>(TaskRepository::TaskState::Failed)) {
			downloader->restoreFailedState();
		}
		else if (info.state == static_cast<int>(TaskRepository::TaskState::Downloading)) {
			downloader->restoreDownloadingState();
		}
		else {
			// 其他状态（如 Waiting）直接启动
			downloader->start();
		}
	}
	else {
		if (info.state == static_cast<int>(TaskRepository::TaskState::Completed)) return;
		// 单线程恢复
		auto* downloader = createSingleDownloader(taskId, info.savePath, info.url, info.totalSize, info.supportsRange);
		m_activeSingleDownloads.insert(taskId, downloader);
		if (info.state == static_cast<int>(TaskRepository::TaskState::Paused)) {
			downloader->restorePausedState(info.downloadedBytes);
		}
		else if (info.state == static_cast<int>(TaskRepository::TaskState::Failed)) {
			downloader->restoreFailedState(info.downloadedBytes);
		}
		else if (info.state == static_cast<int>(TaskRepository::TaskState::Downloading)) {
			if (info.supportsRange) {
				downloader->restoreDownloadingState(info.downloadedBytes);
			}
			else {
				downloader->thread()->start();   // 不支持断点续传，直接启动线程下载
			}
		}
		else {
			downloader->thread()->start();   // 其他状态，直接启动线程下载
		}
	}
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
			singleDownloader->deleteLater(); // 让 Qt 的事件系统在合适的时机删除对象，确保线程安全
			singleDownloader->thread()->quit(); // 停止线程事件循环，让子线程自己完成清理
		}
		else {
			qWarning() << "[HttpClient] 清理任务资源失败，未找到任务 ID：" << taskId;
		}
	}
}

void HttpClient::pauseDownload(const QString& taskId)
{
	MultiThreadDownloader* multiDownloader = findMultiDownloadTask(taskId);
	if (multiDownloader) {
		multiDownloader->pause();
	}
	else {
		SingleThreadDownloader* singleDownloader = findSingleDownloadTask(taskId);
		if (singleDownloader) {
			singleDownloader->pause();
		}
		else {
			qWarning() << "暂停下载: 未找到任务 ID:" << taskId;
		}
	}
}

void HttpClient::resumeDownload(const QString& taskId)
{
	MultiThreadDownloader* multiDownloader = findMultiDownloadTask(taskId);
	if (multiDownloader) {
		multiDownloader->resume();
	}
	else {
		SingleThreadDownloader* singleDownloader = findSingleDownloadTask(taskId);
		if (singleDownloader) {
			singleDownloader->resume();
		}
		else {
			qWarning() << "恢复下载: 未找到任务 ID:" << taskId;
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