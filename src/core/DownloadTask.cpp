#include "DownloadTask.h"
#include "DownloadRequest.h"
#include "NetworkContextPool.h"
#include "FileWriterPool.h"
#include "GlobalConcurrency.h"
#include "TaskRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <QtConcurrent>
#include <QDebug>

DownloadTask::DownloadTask(const QString& taskId, const QUrl& url, const QString& savePath,
	qint64 totalSize, bool supportsRange, int chunkCount,
	QObject* parent)
	: QObject(parent)
	, m_taskId(taskId)
	, m_url(url)
	, m_savePath(savePath)
	, m_totalSize(totalSize)
	, m_supportsRange(supportsRange)
	, m_chunkCount(chunkCount)
{
	// 临时目录
	QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (appDataDir.isEmpty()) {
		appDataDir = QCoreApplication::applicationDirPath();
	}
	m_tempDir = appDataDir + "/chunks/" + m_taskId;
	if (!QDir().mkpath(m_tempDir)) {
		QFileInfo saveInfo(m_savePath);
		m_tempDir = saveInfo.absolutePath() + "/" + saveInfo.baseName() + "_tmp";
		QDir().mkpath(m_tempDir);
	}

	m_progressTimer = new QTimer(this);	// 进度更新定时器
	m_progressTimer->setInterval(300);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		emit progress(m_taskId, totalDownloaded());
		});

	m_dbSaveTimer = new QTimer(this);	// 数据库保存定时器
	m_dbSaveTimer->setInterval(3000);
	connect(m_dbSaveTimer, &QTimer::timeout, this, [this]() {
		updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Downloading));
		});
}

DownloadTask::~DownloadTask()
{
	if (m_progressTimer) m_progressTimer->stop();
	if (m_dbSaveTimer) m_dbSaveTimer->stop();

	for (auto req : m_activeRequests) {
		if (req) {
			req->disconnect(this);
			req->abort();
		}
	}
	m_activeRequests.clear();
}

void DownloadTask::start()
{
	initChunks();

	if (m_pendingChunks.isEmpty() && m_successfulChunks.isEmpty() && m_failedChunks.isEmpty()) {
		for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
			m_pendingChunks.enqueue(it.key());
		}
	}

	updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Downloading));
	m_progressTimer->start();
	m_dbSaveTimer->start();

	while (!m_pendingChunks.isEmpty() && m_activeRequests.size() < m_chunkCount) {
		int chunkId = m_pendingChunks.dequeue();
		dispatchChunk(chunkId);
	}
}

void DownloadTask::initChunks()
{
	if (!m_chunks.isEmpty()) return;

	if (!m_supportsRange || m_totalSize <= 0) {
		ChunkInfo info;
		info.id = 0;
		info.startByte = 0;
		info.endByte = (m_totalSize > 0) ? (m_totalSize - 1) : -1;
		info.tempFilePath = m_tempDir + "/chunk_0.part";
		m_chunks.insert(0, info);
		return;
	}

	qint64 chunkSize = m_totalSize / m_chunkCount;
	if (chunkSize < 1) chunkSize = 1;

	qint64 offset = 0;
	for (int i = 0; i < m_chunkCount; ++i) {
		ChunkInfo info;
		info.id = i;
		info.startByte = offset;
		if (i == m_chunkCount - 1) {
			info.endByte = m_totalSize - 1;
		}
		else {
			info.endByte = offset + chunkSize - 1;
		}
		info.tempFilePath = m_tempDir + QString("/chunk_%1.part").arg(i);
		m_chunks.insert(i, info);
		offset += chunkSize;
	}
}

void DownloadTask::dispatchChunk(int chunkId)
{
	if (m_isPaused || m_isFailed || m_isEnded) return;

	auto it = m_chunks.find(chunkId);
	if (it == m_chunks.end()) return;

	ChunkInfo& chunk = it.value();

	qint64 alreadyDownloaded = QFileInfo(chunk.tempFilePath).size();
	if (alreadyDownloaded < 0) alreadyDownloaded = 0;
	m_chunkDownloaded[chunkId] = alreadyDownloaded;

	qint64 start = chunk.startByte + alreadyDownloaded;

	m_chunkBaseDownloaded[chunkId] = alreadyDownloaded;

	if (!NetworkContextPool::instance()->isInitialized()) {
		chunk.errorString = tr("网络上下文池未初始化");
		m_failedChunks.enqueue(chunkId);
		chunk.isEnded = true;
		m_isFailed = true;
		checkCompletion();
		return;
	}

	g_downloadSemaphore.acquire();

	DownloadRequest* request = new DownloadRequest(
		m_url, start, chunk.endByte, chunk.tempFilePath, chunkId, m_taskId);

	connect(request, &DownloadRequest::dataReceived, this, &DownloadTask::onChunkData);
	connect(request, &DownloadRequest::progress, this, &DownloadTask::onChunkProgress);
	connect(request, &DownloadRequest::finished, this, &DownloadTask::onChunkFinished);

	m_activeRequests.insert(chunkId, request);
	NetworkContextPool::instance()->submit(request);
}

void DownloadTask::onChunkData(int chunkId, const QByteArray& data)
{
	auto it = m_chunks.find(chunkId);
	if (it == m_chunks.end()) return;
	FileWriterPool::instance()->write(it.value().tempFilePath, data);
}

void DownloadTask::onChunkProgress(int chunkId, qint64 received)
{
	qint64 base = m_chunkBaseDownloaded.value(chunkId, 0);
	m_chunkDownloaded[chunkId] = base + received;
}

void DownloadTask::onChunkFinished(int chunkId, bool success, const QString& error)
{
	auto reqIt = m_activeRequests.find(chunkId);
	if (reqIt != m_activeRequests.end()) {
		m_activeRequests.erase(reqIt);
	}

	g_downloadSemaphore.release();

	m_chunkBaseDownloaded.remove(chunkId);

	auto it = m_chunks.find(chunkId);
	if (it == m_chunks.end()) return;
	ChunkInfo& chunk = it.value();

	if (success) {
		chunk.downloaded = m_chunkDownloaded.value(chunkId, 0);
		chunk.isSuccessful = true;
		chunk.isEnded = true;
		chunk.errorString.clear();
		m_successfulChunks.enqueue(chunkId);
	}
	else {
		if (error == tr("已取消")) {
			chunk.errorString = error;
			m_failedChunks.enqueue(chunkId);
			chunk.isEnded = true;
		}
		else {
			if (!m_isFailed && chunk.retryCount < 3) {
				chunk.retryCount++;
				int delay = 1000 * (1 << (chunk.retryCount - 1));
				QTimer::singleShot(delay, this, [this, chunkId]() { retryChunk(chunkId); });
				return;
			}
			else {
				chunk.errorString = error;
				m_failedChunks.enqueue(chunkId);
				chunk.isEnded = true;
				m_isFailed = true;
			}
		}
	}

	checkCompletion();

	if (!m_isPaused && !m_isFailed && !m_isEnded && !m_pendingChunks.isEmpty()) {
		int nextId = m_pendingChunks.dequeue();
		dispatchChunk(nextId);
	}
}

void DownloadTask::retryChunk(int chunkId)
{
	if (m_isPaused || m_isFailed || m_isEnded) return;

	auto it = m_chunks.find(chunkId);
	if (it == m_chunks.end()) return;

	ChunkInfo& chunk = it.value();
	chunk.isEnded = false;
	chunk.isSuccessful = false;

	if (!m_activeRequests.contains(chunkId)) {
		dispatchChunk(chunkId);
	}
	else {
		m_pendingChunks.enqueue(chunkId);
	}
}

void DownloadTask::checkCompletion()
{
	if (m_isFailed) {
		if (m_activeRequests.isEmpty() && m_pendingChunks.isEmpty()) {
			m_isEnded = true;
			m_progressTimer->stop();
			m_dbSaveTimer->stop();
			updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Failed));
			cleanupTempFiles();
			emit ended(m_taskId, true, tr("下载失败"), m_savePath);
		}
		return;
	}

	if (m_successfulChunks.size() == m_chunks.size()) {
		if (m_isMerging) return;
		m_isMerging = true;
		m_progressTimer->stop();
		m_dbSaveTimer->stop();
		mergeChunksAsync();
	}
}

void DownloadTask::mergeChunksAsync()
{
	for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
		FileWriterPool::instance()->closeFileBlocking(it.value().tempFilePath);
	}

	QtConcurrent::run([this]() {
		QFile target(m_savePath);
		bool success = true;
		QString error;

		if (!target.open(QIODevice::WriteOnly)) {
			success = false;
			error = tr("无法创建目标文件: %1").arg(target.errorString());
		}
		else {
			for (int i = 0; i < m_chunks.size(); ++i) {
				auto it = m_chunks.find(i);
				if (it == m_chunks.end()) continue;

				QFile part(it.value().tempFilePath);
				if (!part.open(QIODevice::ReadOnly)) {
					success = false;
					error = tr("无法打开临时文件: %1").arg(it.value().tempFilePath);
					break;
				}
				while (!part.atEnd()) {
					QByteArray data = part.read(64 * 1024);
					if (target.write(data) != data.size()) {
						success = false;
						error = tr("写入目标文件失败");
						break;
					}
				}
				part.close();
				if (!success) break;
			}
			target.close();
		}

		QMetaObject::invokeMethod(this, "onMergeFinished", Qt::QueuedConnection,
			Q_ARG(bool, success), Q_ARG(QString, error));
		});
}

void DownloadTask::onMergeFinished(bool success, const QString& error)
{
	m_isMerging = false;
	m_isEnded = true;

	if (success) {
		updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Completed));
		cleanupTempFiles();
		emit ended(m_taskId, false, QString(), m_savePath);
	}
	else {
		if (QFile::exists(m_savePath)) {
			QFile::remove(m_savePath);
		}
		updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Failed));
		cleanupTempFiles();
		emit ended(m_taskId, true, error, m_savePath);
	}
}

void DownloadTask::pause()
{
	if (m_isPaused || m_isEnded) return;
	m_isPaused = true;

	auto requests = m_activeRequests;
	for (auto req : requests) {
		if (req) req->abort();
	}
	for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
		if (!it.value().isEnded && !m_pendingChunks.contains(it.key())) {
			m_pendingChunks.enqueue(it.key());
		}
	}
	m_progressTimer->stop();
	m_dbSaveTimer->stop();
	updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Paused));
	emit paused(m_taskId);
}

void DownloadTask::resume()
{
	if (!m_isPaused || m_isEnded) return;
	m_isPaused = false;
	updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Downloading));
	m_progressTimer->start();
	m_dbSaveTimer->start();

	while (!m_pendingChunks.isEmpty() && m_activeRequests.size() < m_chunkCount) {
		int chunkId = m_pendingChunks.dequeue();
		dispatchChunk(chunkId);
	}
	emit resumed(m_taskId);
}

void DownloadTask::cancel()
{
	if (m_isEnded) return;
	m_isFailed = true;
	m_isEnded = true;

	auto requests = m_activeRequests;
	for (auto req : requests) {
		if (req) req->abort();
	}
	m_pendingChunks.clear();
	m_progressTimer->stop();
	m_dbSaveTimer->stop();
	updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Failed));
	cleanupTempFiles();
	emit ended(m_taskId, true, tr("下载已取消"), m_savePath);
}

void DownloadTask::retry()
{
	if (!m_isFailed || !m_isEnded) return;
	m_isFailed = false;
	m_isEnded = false;
	m_isMerging = false;

	while (!m_failedChunks.isEmpty()) {
		int chunkId = m_failedChunks.dequeue();
		auto it = m_chunks.find(chunkId);
		if (it != m_chunks.end()) {
			it.value().retryCount = 0;
			it.value().isEnded = false;
			it.value().isSuccessful = false;
			m_pendingChunks.enqueue(chunkId);
		}
	}

	updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Downloading));
	m_progressTimer->start();
	m_dbSaveTimer->start();

	while (!m_pendingChunks.isEmpty() && m_activeRequests.size() < m_chunkCount) {
		int chunkId = m_pendingChunks.dequeue();
		dispatchChunk(chunkId);
	}
}

void DownloadTask::restoreChunks(const QString& json)
{
	if (json.isEmpty()) return;

	QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
	if (!doc.isArray()) return;

	QJsonArray array = doc.array();

	m_chunks.clear();
	m_pendingChunks.clear();
	m_successfulChunks.clear();
	m_failedChunks.clear();
	m_chunkDownloaded.clear();
	m_chunkBaseDownloaded.clear();

	for (const QJsonValue& val : array) {
		QJsonObject obj = val.toObject();
		ChunkInfo info;
		info.id = obj["id"].toInt();
		info.startByte = obj["startByte"].toVariant().toLongLong();
		info.endByte = obj["endByte"].toVariant().toLongLong();
		info.tempFilePath = obj["tempFilePath"].toString();
		info.errorString = obj["errorString"].toString();
		info.isEnded = obj["isEnded"].toBool();
		info.isSuccessful = obj["isSuccessful"].toBool();
		info.retryCount = obj["retryCount"].toInt();
		info.downloaded = obj["downloaded"].toVariant().toLongLong();

		m_chunks.insert(info.id, info);
		m_chunkDownloaded[info.id] = info.downloaded;

		if (info.isEnded) {
			if (info.isSuccessful) {
				m_successfulChunks.enqueue(info.id);
			}
			else {
				m_failedChunks.enqueue(info.id);
			}
		}
		else {
			m_pendingChunks.enqueue(info.id);
		}
	}
}

void DownloadTask::markAsPaused()
{
	m_isPaused = true;
}

void DownloadTask::markAsFailed()
{
	m_isFailed = true;
	m_isEnded = true;
}

void DownloadTask::updateStateToDatabase(int state)
{
	TaskRepository repo;
	repo.updateState(m_taskId, state);
	repo.updateProgress(m_taskId, totalDownloaded());

	QJsonArray chunksArray;
	for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
		QJsonObject obj;
		obj["id"] = it->id;
		obj["startByte"] = it->startByte;
		obj["endByte"] = it->endByte;
		obj["isEnded"] = it->isEnded;
		obj["isSuccessful"] = it->isSuccessful;
		obj["retryCount"] = it->retryCount;
		obj["downloaded"] = m_chunkDownloaded.value(it.key(), 0);
		obj["tempFilePath"] = it->tempFilePath;
		obj["errorString"] = it->errorString;
		chunksArray.append(obj);
	}
	QJsonDocument doc(chunksArray);
	repo.updateChunks(m_taskId, doc.toJson(QJsonDocument::Compact));
}

void DownloadTask::cleanupTempFiles()
{
	if (!m_tempDir.isEmpty() && QDir(m_tempDir).exists()) {
		QDir(m_tempDir).removeRecursively();
	}
}

qint64 DownloadTask::totalDownloaded() const
{
	qint64 total = 0;
	for (auto it = m_chunkDownloaded.constBegin(); it != m_chunkDownloaded.constEnd(); ++it) {
		total += it.value();
	}
	return total;
}