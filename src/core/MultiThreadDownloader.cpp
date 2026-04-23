#include "MultiThreadDownloader.h"
#include "TaskRepository.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <numeric>

MultiThreadDownloader::MultiThreadDownloader(const QString& fullSavePath, const QUrl& url,
	qint64 totalSize, const QString& taskId, int threadCount, QObject* parent)
	: QObject(parent)
	, m_fullSavePath(fullSavePath)
	, m_url(url)
	, m_totalSize(totalSize)
	, m_taskId(taskId)
	, m_threadCount(threadCount)
{
	m_progressTimer = new QTimer(this);
	m_progressTimer->setInterval(PROGRESS_MERGE_INTERVAL_MS);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() { updateTotalProgress(); });

	m_dbSaveTimer = new QTimer(this);
	m_dbSaveTimer->setInterval(DB_SAVE_INTERVAL_MS);
	connect(m_dbSaveTimer, &QTimer::timeout, this, [this]() { updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Downloading)); });
}

MultiThreadDownloader::~MultiThreadDownloader()
{
	if (m_progressTimer) m_progressTimer->stop();
	if (m_dbSaveTimer) m_dbSaveTimer->stop();
	stopAllThreads();
}

void MultiThreadDownloader::restorePausedState()
{
	TaskRepository repo;
	TaskRepository::TaskInfo info = repo.loadTask(m_taskId);
	if (!info.chunksJson.isEmpty()) {
		restoreChunksFromJson(info.chunksJson);
	}
	m_isPaused = true;
}

void MultiThreadDownloader::restoreFailedState()
{
	TaskRepository repo;
	TaskRepository::TaskInfo info = repo.loadTask(m_taskId);
	if (!info.chunksJson.isEmpty()) {
		restoreChunksFromJson(info.chunksJson);
	}
	m_isDownloadEnded = true;
	m_isFailed = true;
}

void MultiThreadDownloader::restoreDownloadingState()
{
	TaskRepository repo;
	TaskRepository::TaskInfo info = repo.loadTask(m_taskId);
	if (!info.chunksJson.isEmpty()) {
		restoreChunksFromJson(info.chunksJson);
	}
	startWorkers();
}

void MultiThreadDownloader::restoreChunksFromJson(const QString& json)
{
	if (json.isEmpty()) return;

	QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
	if (!doc.isArray()) return;

	QJsonArray array = doc.array();
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

		m_chunks.insert(info.id, info);

		qint64 downloaded = obj["downloaded"].toVariant().toLongLong();
		m_chunkDownloaded[info.id] = downloaded;

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

void MultiThreadDownloader::pause()
{
	if (m_isDownloadEnded || m_isPaused) return;

	for (auto worker : m_workers) {
		if (m_workerStatus.value(worker, false)) {
			QMetaObject::invokeMethod(worker, "pause", Qt::QueuedConnection);
		}
	}
}

void MultiThreadDownloader::resume()
{
	if (m_isDownloadEnded || !m_isPaused) return;

	m_isPaused = false;
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Downloading));
	emit resumed(m_taskId);
	startWorkers();
}

void MultiThreadDownloader::cancel()
{
	if (m_isDownloadEnded) return;

	m_isFailed = true;
	if (m_isPaused) {
		m_isDownloadEnded = true;
		for (int chunkId : m_pendingChunks) {
			m_chunks[chunkId].errorString = tr("下载已取消");
		}
		updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Failed));
		emit downloadEnded(m_taskId, true, tr("下载已取消"), m_fullSavePath);
		return;
	}
	for (auto worker : m_workers) {
		if (m_workerStatus.value(worker, false)) {
			QMetaObject::invokeMethod(worker, "cancel", Qt::QueuedConnection);
		}
	}
}

void MultiThreadDownloader::retry() {
	if (!m_isDownloadEnded || !m_isFailed) return;

	m_isDownloadEnded = false;
	m_isFailed = false;
	m_isPaused = false;

	// 将失败分片重新入队，重置重试次数
	while (!m_failedChunks.isEmpty()) {
		int id = m_failedChunks.dequeue();
		m_chunks[id].retryCount = 0;   // 重置
		m_chunks[id].isEnded = false;
		m_chunks[id].isSuccessful = false;
		m_pendingChunks.enqueue(id);
	}

	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Downloading));
	startWorkers();
}

void MultiThreadDownloader::start()
{
	calculateRanges();
	startWorkers();
}

void MultiThreadDownloader::calculateRanges()
{
	qint64 chunkSize;
	if (m_totalSize < 50 * 1024 * 1024) {
		chunkSize = 2 * 1024 * 1024;
	}
	else if (m_totalSize < 200 * 1024 * 1024) {
		chunkSize = 4 * 1024 * 1024;
	}
	else if (m_totalSize < 1024 * 1024 * 1024) {
		chunkSize = 8 * 1024 * 1024;
	}
	else {
		chunkSize = 16 * 1024 * 1024;
	}

	qint64 remaining = m_totalSize;
	qint64 offset = 0;
	int chunkId = 0;

	while (remaining > 0) {
		qint64 thisSize = qMin(chunkSize, remaining);
		ChunkInfo info;
		info.id = chunkId;
		info.startByte = offset;
		info.endByte = offset + thisSize - 1;
		info.tempFilePath = QString("%1.part%2").arg(m_fullSavePath).arg(chunkId);
		m_chunks.insert(chunkId, info);
		m_pendingChunks.enqueue(chunkId);
		offset += thisSize;
		remaining -= thisSize;
		++chunkId;
	}
}

void MultiThreadDownloader::startWorkers()
{
	for (int i = 0; i < m_threadCount; ++i) {
		QThread* thread = new QThread;
		DownloadWorker* worker = new DownloadWorker(i);
		worker->moveToThread(thread);
		connect(thread, &QThread::started, worker, &DownloadWorker::requestChunk);
		connect(worker, &DownloadWorker::needChunk, this, &MultiThreadDownloader::assignChunk);
		connect(worker, &DownloadWorker::workPaused, this, &MultiThreadDownloader::onWorkPaused);
		connect(worker, &DownloadWorker::workUpdated, this, &MultiThreadDownloader::onWorkUpdated);
		connect(worker, &DownloadWorker::workEnded, this, &MultiThreadDownloader::onWorkEnded);

		thread->start();
		m_threads.append(thread);
		m_workers.append(worker);
		m_workerStatus[worker] = false; // 初始状态为未工作
		m_progressTimer->start();
		m_dbSaveTimer->start();
	}
}

void MultiThreadDownloader::assignChunk()
{
	DownloadWorker* worker = qobject_cast<DownloadWorker*>(sender());
	if (!m_workers.contains(worker)) return;

	if (!m_isFailed && !m_pendingChunks.isEmpty()) {
		int chunkId = m_pendingChunks.dequeue();
		auto it = m_chunks.find(chunkId);
		if (it == m_chunks.end()) return;
		const ChunkInfo& chunk = it.value();
		qint64 alreadyDownloaded = m_chunkDownloaded.value(chunkId, 0);
		m_activeWorkers++;
		m_workerStatus[worker] = true;

		QMetaObject::invokeMethod(worker, "startDownload", Qt::QueuedConnection,
			Q_ARG(QUrl, m_url),
			Q_ARG(qint64, chunk.startByte),
			Q_ARG(qint64, chunk.endByte),
			Q_ARG(QString, chunk.tempFilePath),
			Q_ARG(int, chunk.id),
			Q_ARG(qint64, alreadyDownloaded));
	}
	else {
		if (m_activeWorkers == 0) {
			if (m_successfulChunks.size() + m_failedChunks.size() == m_chunks.size() || m_isFailed) {
				m_progressTimer->stop();
				m_dbSaveTimer->stop();
				stopAllThreads();
				checkAllChunkStatus();
				m_isDownloadEnded = true;
			}
		}
	}
}

void MultiThreadDownloader::checkAllChunkStatus()
{
	updateTotalProgress();
	if (m_failedChunks.size() > 0) {
		updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Failed));
		emit downloadEnded(m_taskId, true, tr("下载失败"), m_fullSavePath);
		return;
	}
	mergeChunks();
}

void MultiThreadDownloader::mergeChunks()
{
	QFile target(m_fullSavePath);
	QString errorString;
	bool mergeError = false;
	if (!target.open(QIODevice::WriteOnly)) {
		mergeError = true;
		errorString = tr("无法创建目标文件: %1").arg(target.errorString());
	}
	else {
		for (int i = 0; i < m_chunks.size(); ++i) {
			auto it = m_chunks.find(i);
			if (it == m_chunks.end()) continue;
			const ChunkInfo& chunk = it.value();
			QFile part(chunk.tempFilePath);
			if (!part.open(QIODevice::ReadOnly)) {
				mergeError = true;
				qWarning() << "[MultiThread] 无法打开临时文件:" << chunk.tempFilePath;
				break;
			}
			while (!part.atEnd()) {
				QByteArray data = part.read(64 * 1024);
				if (target.write(data) != data.size()) {
					mergeError = true;
					break;
				}
			}
			part.close();
			if (mergeError) break;
		}
		target.close();

		if (mergeError) {
			errorString = tr("合并临时文件失败");
			if (QFile::exists(m_fullSavePath)) {
				QFile::remove(m_fullSavePath);
			}
		}
	}
	updateStateToDatabase(mergeError ? static_cast<int>(TaskRepository::TaskState::Failed) : static_cast<int>(TaskRepository::TaskState::Completed));
	cleanupTempFiles();
	m_chunks.clear();
	m_pendingChunks.clear();
	m_failedChunks.clear();
	m_successfulChunks.clear();
	m_chunkDownloaded.clear();
	emit downloadEnded(m_taskId, mergeError, errorString, m_fullSavePath);
}

int MultiThreadDownloader::getRetryDelay(int retryCount) const
{
	int delay = 1000 * (1 << (retryCount - 1));
	return qMin(delay, 30000);
}

void MultiThreadDownloader::retryChunk(int chunkId)
{
	if (m_isDownloadEnded) {
		m_chunks[chunkId].isEnded = true;
		m_failedChunks.enqueue(chunkId);
		return;
	}
	else if (m_isPaused) {
		m_pendingChunks.enqueue(chunkId);
		return;
	}

	auto it = m_chunks.find(chunkId);
	if (it == m_chunks.end()) return;
	m_pendingChunks.enqueue(chunkId);

	for (auto worker : m_workers) {
		if (!m_workerStatus.value(worker, true)) {
			QMetaObject::invokeMethod(worker, &DownloadWorker::requestChunk, Qt::QueuedConnection);
			break;
		}
	}
}

void MultiThreadDownloader::onWorkEnded(int chunkId, bool success, const QString& error)
{
	DownloadWorker* worker = qobject_cast<DownloadWorker*>(sender());
	if (success) {
		m_successfulChunks.enqueue(chunkId);
		m_chunks[chunkId].isSuccessful = true;
		m_chunks[chunkId].isEnded = true;
		m_chunks[chunkId].errorString.clear();
	}
	else {
		m_chunks[chunkId].errorString = error;
		int retryCount = m_chunks[chunkId].retryCount;
		if (!m_isFailed && retryCount < MAX_RETRY_COUNT) {
			m_chunks[chunkId].retryCount++;
			int delay = getRetryDelay(retryCount);
			QTimer::singleShot(delay, this, [this, chunkId]() { retryChunk(chunkId); });
		}
		else {
			m_failedChunks.enqueue(chunkId);
			m_chunks[chunkId].isEnded = true;
			if (!m_isFailed) {
				m_isFailed = true;
			}
		}
	}
	if (m_isPaused) return;
	m_activeWorkers--;
	m_workerStatus[worker] = false;
	QMetaObject::invokeMethod(worker, &DownloadWorker::requestChunk, Qt::QueuedConnection);
}

void MultiThreadDownloader::onWorkPaused(int chunkId, bool isWorking)
{
	DownloadWorker* worker = qobject_cast<DownloadWorker*>(sender());
	m_activeWorkers--;
	if (worker) {
		m_workerStatus[worker] = false;
	}
	if (isWorking) {
		m_pendingChunks.enqueue(chunkId);
		m_chunks[chunkId].errorString = tr("下载已暂停");
	}

	if (m_activeWorkers == 0) {
		m_progressTimer->stop();
		m_dbSaveTimer->stop();
		stopAllThreads();
		updateTotalProgress();
		updateStateToDatabase(static_cast<int>(TaskRepository::TaskState::Paused));
		m_isPaused = true;
		emit paused(m_taskId);
	}
}

void MultiThreadDownloader::onWorkUpdated(int chunkId, qint64 received)
{
	if (m_isDownloadEnded || m_isPaused) return;
	m_chunkDownloaded[chunkId] = received;
}

void MultiThreadDownloader::updateTotalProgress()
{
	if (m_isDownloadEnded || m_isPaused) return;
	qint64 totalReceived = 0;
	for (auto it = m_chunkDownloaded.constBegin(); it != m_chunkDownloaded.constEnd(); ++it) {
		totalReceived += it.value();
	}
	emit downloadProgressUpdate(m_taskId, totalReceived);
}

void MultiThreadDownloader::updateStateToDatabase(int state)
{
	if (m_isDownloadEnded || m_isPaused) return;
	TaskRepository repo;
	qint64 total = std::accumulate(m_chunkDownloaded.begin(), m_chunkDownloaded.end(), 0LL);
	repo.updateProgress(m_taskId, total);
	repo.updateState(m_taskId, state);

	QJsonArray chunksArray;
	for (auto it = m_chunks.begin(); it != m_chunks.end(); ++it) {
		QJsonObject obj;
		obj["id"] = it->id;
		obj["startByte"] = it->startByte;
		obj["endByte"] = it->endByte;
		obj["isEnded"] = it->isEnded;
		obj["retryCount"] = it->retryCount;
		obj["downloaded"] = m_chunkDownloaded.value(it.key(), 0);
		obj["tempFilePath"] = it->tempFilePath;
		obj["errorString"] = it->errorString;
		obj["isSuccessful"] = it->isSuccessful;
		chunksArray.append(obj);
	}
	QJsonDocument doc(chunksArray);
	repo.updateChunks(m_taskId, doc.toJson(QJsonDocument::Compact));
}

void MultiThreadDownloader::cleanupTempFiles()
{
	for (const auto& chunk : m_chunks) {
		if (QFile::exists(chunk.tempFilePath))
			QFile::remove(chunk.tempFilePath);
	}
}

void MultiThreadDownloader::stopAllThreads()
{
	for (auto worker : m_workers) {
		if (worker) {
			disconnect(worker, nullptr, this, nullptr);
		}
	}

	for (auto thread : m_threads) {
		if (thread && thread->isRunning()) {
			thread->quit();
			thread->wait(3000);
		}
	}

	qDeleteAll(m_workers);
	m_workers.clear();
	qDeleteAll(m_threads);
	m_threads.clear();
	m_workerStatus.clear();
}