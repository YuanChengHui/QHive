#include "HttpClient.h"
#include "HeadRequest.h"
#include "NetworkContextPool.h"
#include "TaskRepository.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QMimeDatabase>
#include <QMimeType>
#include <QStorageInfo>
#include <QDir>
#include <QDebug>

HttpClient::HttpClient(QObject* parent)
	: QObject(parent)
{
}

HttpClient* HttpClient::instance()
{
	static HttpClient* instance_obj = new HttpClient(QCoreApplication::instance());
	return instance_obj;
}

static int getOptimalChunkCount(qint64 totalSize)
{
	constexpr qint64 MIN_CHUNK_SIZE = 4 * 1024 * 1024;    // 4MB
	constexpr qint64 MAX_CHUNK_SIZE = 32 * 1024 * 1024;   // 32MB
	constexpr int MIN_CHUNKS = 1;
	constexpr int MAX_CHUNKS = 16;

	if (totalSize <= 0) return 1;

	// 先按最大分片算最小分片数
	int minChunks = static_cast<int>((totalSize + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE);
	// 再按最小分片算最大分片数
	int maxChunks = static_cast<int>((totalSize + MIN_CHUNK_SIZE - 1) / MIN_CHUNK_SIZE);

	// 在 [minChunks, maxChunks] 内选最优
	int chunks = std::clamp(8, minChunks, maxChunks);  // 默认目标 8 个分片
	chunks = std::clamp(chunks, MIN_CHUNKS, MAX_CHUNKS);
	return chunks;
}

static QString formatBytes(qint64 bytes)
{
	if (bytes < 1024) return QString("%1 B").arg(bytes);
	if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
	if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 2);
	return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

void HttpClient::headRequest(const QString& taskId, const QUrl& url,
	const QString& initialFileName, const QString& saveDir)
{
	HeadRequest* headReq = new HeadRequest(url);

	connect(headReq, &HeadRequest::failed, this,
		[this, url](const QString& error) {
			emit headRequestError(url, error);
		}, Qt::QueuedConnection);

	connect(headReq, &HeadRequest::headersReady, this,
		[this, taskId, url, initialFileName, saveDir]
		(qint64 totalSize, const QByteArray& contentDisposition,
			const QByteArray& contentType, bool supportsRange) {
				QString fileName = parseFileName(contentDisposition, contentType, initialFileName);
				fileName = generateUniqueFileName(saveDir, fileName);
				QString fullSavePath = saveDir + fileName;

				if (totalSize > 0) {
					QString err;
					if (!checkDiskSpace(saveDir, totalSize, err)) {
						emit headRequestError(url, err);
						return;
					}
				}

				emit headRequestSuccess(taskId, url, fileName, fullSavePath,
					totalSize, supportsRange);

				launchDownload(taskId, url, supportsRange, totalSize, fullSavePath);
		}, Qt::QueuedConnection);

	NetworkContextPool::instance()->submit(headReq);
}

void HttpClient::launchDownload(const QString& taskId, const QUrl& url,
	bool supportsRange, qint64 totalSize,
	const QString& fullSavePath)
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
	info.chunkCount = (totalSize > 0 && supportsRange) ? getOptimalChunkCount(totalSize) : 1;
	repo.saveTask(info);

	int chunkCount = info.chunkCount;
	auto* task = createDownloadTask(taskId, fullSavePath, url, totalSize, supportsRange, chunkCount);
	m_activeTasks.insert(taskId, task);
	task->start();
}

DownloadTask* HttpClient::createDownloadTask(const QString& taskId,
	const QString& fullSavePath,
	const QUrl& url,
	qint64 totalSize,
	bool supportsRange,
	int chunkCount)
{
	auto* task = new DownloadTask(taskId, url, fullSavePath, totalSize, supportsRange, chunkCount);

	connect(task, &DownloadTask::ended, this, &HttpClient::handleResult);
	connect(task, &DownloadTask::progress, this, [this](const QString& tid, qint64 received) {
		emit updateDownloadProgress(tid, received);
		});
	connect(task, &DownloadTask::paused, this, [this](const QString& tid) {
		emit downloadPaused(tid);
		});
	connect(task, &DownloadTask::resumed, this, [this](const QString& tid) {
		emit downloadResumed(tid);
		});

	return task;
}

DownloadTask* HttpClient::findTask(const QString& taskId)
{
	auto it = m_activeTasks.find(taskId);
	return (it != m_activeTasks.end()) ? it.value() : nullptr;
}

void HttpClient::restoreDownloadTask(const QString& taskId)
{
	TaskRepository repo;
	TaskRepository::TaskInfo info = repo.loadTask(taskId);
	if (info.taskId.isEmpty()) return;
	if (info.state == static_cast<int>(TaskRepository::TaskState::Completed)) return;

	int chunkCount = info.chunkCount > 0 ? info.chunkCount : 1;
	auto* task = createDownloadTask(taskId, info.savePath, QUrl(info.url),
		info.totalSize, info.supportsRange, chunkCount);
	m_activeTasks.insert(taskId, task);

	if (!info.chunksJson.isEmpty()) {
		task->restoreChunks(info.chunksJson);
	}

	if (info.state == static_cast<int>(TaskRepository::TaskState::Paused)) {
		task->markAsPaused();
	}
	else if (info.state == static_cast<int>(TaskRepository::TaskState::Failed)) {
		task->markAsFailed();
	}
	else if (info.state == static_cast<int>(TaskRepository::TaskState::Downloading)) {
		task->start();
	}
}

void HttpClient::handleResult(const QString& taskId, bool errorOccurred,
	const QString& errorString, const QString& savePath)
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
	DownloadTask* task = findTask(taskId);
	if (task) {
		m_activeTasks.remove(taskId);
		task->deleteLater();
	}
}

void HttpClient::pauseDownload(const QString& taskId)
{
	if (auto* task = findTask(taskId)) task->pause();
	else qWarning() << "暂停下载: 未找到任务 ID:" << taskId;
}

void HttpClient::resumeDownload(const QString& taskId)
{
	if (auto* task = findTask(taskId)) task->resume();
	else qWarning() << "恢复下载: 未找到任务 ID:" << taskId;
}

void HttpClient::cancelDownload(const QString& taskId)
{
	if (auto* task = findTask(taskId)) task->cancel();
	else qWarning() << "取消下载: 未找到任务 ID:" << taskId;
}

void HttpClient::retryDownload(const QString& taskId)
{
	if (auto* task = findTask(taskId)) task->retry();
	else qWarning() << "重试下载: 未找到任务 ID:" << taskId;
}

QString HttpClient::parseFileName(const QByteArray& contentDisposition,
	const QByteArray& contentType,
	const QString& initialFileName)
{
	QString fileName = initialFileName;

	if (!contentDisposition.isEmpty()) {
		QRegularExpression rx(R"(filename[^;\n=\r]*=((['])[^']*\2|[^;\n\r]*))");
		QRegularExpressionMatch match = rx.match(QString::fromUtf8(contentDisposition));
		if (match.hasMatch()) {
			QString name = match.captured(1).trimmed();
			if (name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"'))) {
				name = name.mid(1, name.length() - 2);
			}
			if (!name.isEmpty()) fileName = name;
		}
	}

	if (fileName.isEmpty()) fileName = "download";

	QFileInfo fileInfo(fileName);
	if (fileInfo.completeSuffix().isEmpty() && !contentType.isEmpty()) {
		QMimeDatabase mimeDb;
		QMimeType mimeType = mimeDb.mimeTypeForName(QString::fromUtf8(contentType));
		if (mimeType.isValid()) {
			QString suffix = mimeType.preferredSuffix();
			if (!suffix.isEmpty()) fileName += "." + suffix;
		}
	}

	return fileName;
}

QString HttpClient::generateUniqueFileName(const QString& saveDir, const QString& fileName)
{
	QFileInfo fileInfo(saveDir + fileName);
	if (!fileInfo.exists()) return fileName;

	QString baseName = fileInfo.baseName();
	QString suffix = fileInfo.completeSuffix();
	int count = 1;
	QString newFileName;

	do {
		if (suffix.isEmpty()) {
			newFileName = QString("%1(%2)").arg(baseName).arg(count);
		}
		else {
			newFileName = QString("%1(%2).%3").arg(baseName).arg(count).arg(suffix);
		}
		count++;
	} while (QFileInfo(saveDir + newFileName).exists());

	return newFileName;
}

bool HttpClient::checkDiskSpace(const QString& saveDir, qint64 requiredBytes, QString& errorString)
{
	QStorageInfo storageInfo(saveDir);
	if (!storageInfo.isValid()) {
		errorString = tr("无法获取磁盘信息: %1").arg(saveDir);
		return false;
	}
	qint64 availableBytes = storageInfo.bytesAvailable();
	if (availableBytes < requiredBytes) {
		errorString = QString("[ %1 ] 磁盘空间不足。需要 %2 ，可用 %3。")
			.arg(saveDir)
			.arg(formatBytes(requiredBytes))
			.arg(formatBytes(availableBytes));
		return false;
	}
	return true;
}