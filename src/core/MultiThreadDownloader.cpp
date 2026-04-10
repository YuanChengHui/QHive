#include "MultiThreadDownloader.h"
#include <QDebug>
#include <QDir>
#include <QFile>

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
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {	updateTotalProgress(); });
}

MultiThreadDownloader::~MultiThreadDownloader()
{
	if (m_progressTimer) {
		m_progressTimer->stop();
	}
}

void MultiThreadDownloader::start()
{
	if (m_initialized) return;
	calculateRanges();
	startWorkers();
	m_initialized = true;
	m_progressTimer->start();
}

void MultiThreadDownloader::cancel()
{
	if (m_cancelled) return;
	m_progressTimer->stop();
	for (auto worker : m_workers) {
		if (worker) worker->cancel();
	}
	markPendingAsFailed();
	updateTotalProgress(); // 发出一次最终进度更新
	m_cancelled = true;
}

void MultiThreadDownloader::retry()
{
	resetState();
	start();
}

void MultiThreadDownloader::resetState()
{
	m_chunks.clear();
	m_pendingChunks.clear();
	m_completedChunks = 0;
	m_failedChunks = 0;
	m_cancelled = false;
	m_initialized = false;
	m_finished = false;
	m_chunkDownloaded.clear();

	if (m_progressTimer) {
		m_progressTimer->stop();
	}
}

void MultiThreadDownloader::calculateRanges()
{
	// 动态计算分块大小
	qint64 chunkSize;
	if (m_totalSize < 50 * 1024 * 1024) {           // 10 MB ~ 50 MB (此时线程数 2)
		chunkSize = 2 * 1024 * 1024;                // 2 MB
	}
	else if (m_totalSize < 200 * 1024 * 1024) {   // 50 MB ~ 200 MB (线程数 4)
		chunkSize = 4 * 1024 * 1024;                // 4 MB
	}
	else if (m_totalSize < 1024 * 1024 * 1024) {  // 200 MB ~ 1 GB (线程数 8)
		chunkSize = 8 * 1024 * 1024;                // 8 MB
	}
	else {                                         // >= 1 GB (线程数 16)
		chunkSize = 16 * 1024 * 1024;               // 16 MB
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
		QThread* thread = new QThread(this);
		DownloadWorker* worker = new DownloadWorker(i);
		worker->moveToThread(thread);
		connect(thread, &QThread::started, worker, &DownloadWorker::startWorking);
		connect(worker, &DownloadWorker::needChunk, this, &MultiThreadDownloader::assignNextChunk);
		connect(worker, &DownloadWorker::chunkProgress, this, &MultiThreadDownloader::onWorkerProgress);
		connect(worker, &DownloadWorker::chunkFinished, this, &MultiThreadDownloader::onWorkerFinished);

		thread->start();
		m_threads.append(thread);
		m_workers.append(worker);
		m_workerIdle[worker] = true;
	}
}

void MultiThreadDownloader::assignNextChunk()
{
	DownloadWorker* worker = qobject_cast<DownloadWorker*>(sender());
	if (!worker || m_cancelled) return;
	// 确保 worker 仍在管理列表中
	if (!m_workers.contains(worker)) return;

	if (m_pendingChunks.isEmpty()) {
		m_workerIdle[worker] = true;
		return;
	}
	int chunkId = m_pendingChunks.dequeue();
	auto it = m_chunks.find(chunkId);
	if (it == m_chunks.end()) 		return;

	const ChunkInfo& chunk = it.value();
	m_workerIdle[worker] = false;

	QMetaObject::invokeMethod(worker, "startDownload", Qt::QueuedConnection,
		Q_ARG(QUrl, m_url),
		Q_ARG(qint64, chunk.startByte),
		Q_ARG(qint64, chunk.endByte),
		Q_ARG(QString, chunk.tempFilePath),
		Q_ARG(int, chunk.id));
}

void MultiThreadDownloader::onWorkerProgress(int chunkId, qint64 received)
{
	if (m_cancelled) return;
	m_chunkDownloaded[chunkId] = received;
}

void MultiThreadDownloader::onWorkerFinished(int chunkId, bool success, const QString& error)
{
	auto it = m_chunks.find(chunkId);
	if (it == m_chunks.end()) return;

	if (success) {
		it->completed = true;
		it->failed = false;
		++m_completedChunks;
	}
	else {
		it->errorString = error;
		it->retryCount++;
		const int MAX_RETRY = 3;
		if (it->retryCount < MAX_RETRY && !m_cancelled) {
			it->failed = false;
			it->completed = false;
			// 重置该分块的已下载计数，避免重复累加
			m_chunkDownloaded[chunkId] = 0;
			m_pendingChunks.enqueue(chunkId);
		}
		else {
			it->failed = true;
			it->completed = false;
			++m_failedChunks;
		}
	}

	int finishedCount = m_completedChunks + m_failedChunks;
	int totalChunks = m_chunks.size();
	bool shouldCheck = (finishedCount == totalChunks);

	if (shouldCheck) {
		stopAllThreads();
		checkAllCompleted();
		return;
	}

	DownloadWorker* worker = qobject_cast<DownloadWorker*>(sender());
	if (worker && !m_cancelled) {
		if (!m_pendingChunks.isEmpty()) {
			QMetaObject::invokeMethod(worker, &DownloadWorker::startWorking, Qt::QueuedConnection);
		}
		else {
			m_workerIdle[worker] = true;
		}
	}
}

void MultiThreadDownloader::checkAllCompleted()
{
	if (m_finished) return;
	m_finished = true;
	m_progressTimer->stop();	// 任务完成后停止定时器，避免后续事件

	if (m_cancelled) {
		// 取消后不销毁对象，保留以便重试
		emit downloadEnded(m_taskId, true, tr("已取消"), m_fullSavePath);
		return;
	}

	if (m_failedChunks > 0) {
		cleanupTempFiles();
		QString error = tr("%1 个分块下载失败").arg(m_failedChunks);
		emit downloadEnded(m_taskId, true, error, m_fullSavePath);
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
			QFile::remove(m_fullSavePath);  // 删除不完整的目标文件
		}
	}
	cleanupTempFiles();
	emit downloadProgressUpdate(m_taskId, m_totalSize);
	emit downloadEnded(m_taskId, mergeError, errorString, m_fullSavePath);
}

void MultiThreadDownloader::markPendingAsFailed()
{
	while (!m_pendingChunks.isEmpty()) {
		int chunkId = m_pendingChunks.dequeue();
		auto it = m_chunks.find(chunkId);
		if (it != m_chunks.end() && !it->completed && !it->failed) {
			it->failed = true;
			it->completed = false;
			it->errorString = tr("已取消");
			++m_failedChunks;
		}
	}
}

void MultiThreadDownloader::updateTotalProgress()
{
	if (m_cancelled || m_finished) return;

	qint64 totalReceived = 0;
	for (auto it = m_chunkDownloaded.constBegin(); it != m_chunkDownloaded.constEnd(); ++it) {
		totalReceived += it.value();
	}
	emit downloadProgressUpdate(m_taskId, totalReceived);
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
	// 先断开所有 worker 的信号，避免后续事件
	for (auto worker : m_workers) {
		if (worker) {
			disconnect(worker, nullptr, this, nullptr);
			worker->cancel();
		}
	}

	// 请求线程退出并等待
	for (auto thread : m_threads) {
		if (thread && thread->isRunning()) {
			thread->quit();
			thread->wait(3000);
		}
	}

	// 安全删除 worker 和 thread
	qDeleteAll(m_workers);
	m_workers.clear();
	qDeleteAll(m_threads);
	m_threads.clear();
	m_workerIdle.clear();
}