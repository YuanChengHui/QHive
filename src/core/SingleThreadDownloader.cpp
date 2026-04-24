#include "SingleThreadDownloader.h"
#include <TaskRepository.h>

#include <QNetworkRequest>
#include <QDebug>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QThread>

SingleThreadDownloader::SingleThreadDownloader(const QString& fullSavePath,
	const QUrl& url,
	qint64 totalSize,
	const QString& taskId,
	bool supportsRange,
	QNetworkAccessManager* networkManager,
	QObject* parent)
	: QObject(parent)
	, m_fullSavePath(fullSavePath)
	, m_url(url)
	, m_totalSize(totalSize)
	, m_taskId(taskId)
	, m_supportsRange(supportsRange)
	, m_networkManager(networkManager)
	, m_bytesReceived(0)
	, m_isDownloadEnded()
	, m_file(nullptr)
	, m_reply(nullptr)
{
	if (m_totalSize > 0 && m_supportsRange) {
		m_isResumable = true;
	}
	m_retryTimer.setSingleShot(true);
	connect(&m_retryTimer, &QTimer::timeout, this, &SingleThreadDownloader::retryRequest);
}

SingleThreadDownloader::~SingleThreadDownloader()
{
	if (!m_isDownloadEnded && !m_isPaused) {
		m_errorOccurred = true;
		m_requestAborted = true;
		m_isDownloadEnded = true;
		if (m_reply) {
			m_reply->disconnect(this);
			m_reply->abort();
			m_reply->deleteLater();
			m_reply = nullptr;
		}
	}
	if (m_reply) {
		delete m_reply;
		m_reply = nullptr;
	}
	cleanupFile();

	if (m_errorOccurred || !m_isDownloadEnded) {
		if (QFile::exists(m_fullSavePath)) {
			QFile::remove(m_fullSavePath);
		}
	}
}

void SingleThreadDownloader::cleanupFile()
{
	if (m_file) {
		if (m_file->isOpen()) m_file->close();
		delete m_file;
		m_file = nullptr;
	}
}

void SingleThreadDownloader::restorePausedState(qint64 downloadedBytes)
{
	m_isPaused = true;
	m_requestAborted = true;
	m_errorString = tr("下载已暂停！");
	m_bytesReceived = downloadedBytes;
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Paused));
}

void SingleThreadDownloader::restoreFailedState(qint64 downloadedBytes)
{
	m_errorOccurred = true;
	m_isDownloadEnded = true;
	m_errorString = tr("下载失败！");
	if (m_isResumable) {
		m_bytesReceived = downloadedBytes;
	}
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Failed));
}

void SingleThreadDownloader::restoreDownloadingState(qint64 downloadedBytes)
{
	if (m_isResumable) {
		m_bytesReceived = downloadedBytes;
	}
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Downloading));
	startNetworkRequest(m_bytesReceived);
}

void SingleThreadDownloader::start()
{
	m_file = new QFile(m_fullSavePath);
	if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		m_isDownloadEnded = true;
		m_errorOccurred = true;
		m_errorString = m_file->errorString();
		TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Failed));
		emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
		return;
	}
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Downloading));
	startNetworkRequest(m_bytesReceived);
	m_dbSaveTimer.start();
	m_lastProgressTime.start();
}

void SingleThreadDownloader::pause()
{
	if (!m_isResumable) return;
	if (m_isPaused || m_isDownloadEnded) return;

	m_retryTimer.stop();
	m_dbSaveTimer.invalidate();
	m_lastProgressTime.invalidate();

	// 读取网络缓冲区剩余数据
	if (m_reply && m_reply->bytesAvailable() > 0 && m_file && m_file->isOpen()) {
		QByteArray buffer(64 * 1024, Qt::Uninitialized);
		qint64 totalRead = 0;
		while (m_reply->bytesAvailable() > 0) {
			qint64 len = m_reply->read(buffer.data(), buffer.size());
			if (len <= 0) break;
			if (m_file->write(buffer.constData(), len) != len) {
				// 写入失败处理
				break;
			}
			totalRead += len;
		}
		if (totalRead > 0) {
			m_bytesReceived += totalRead;
		}
	}

	if (m_file && m_file->isOpen()) {
		m_file->flush();
		m_bytesReceived = m_file->size();
	}
	emit downloadProgressUpdate(m_taskId, m_bytesReceived);
	TaskRepository().updateProgress(m_taskId, m_bytesReceived);
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Paused));
	m_isDownloadEnded = false;
	m_isPaused = true;
	m_isCanceled = false;
	m_errorOccurred = false;
	m_requestAborted = true;
	m_retryScheduled = false;

	if (m_reply) {
		m_reply->abort();
	}
}

void SingleThreadDownloader::resume()
{
	if (!m_isResumable) return;
	if (!m_isPaused) return;

	m_file = new QFile(m_fullSavePath);
	if (!m_file->open(QIODevice::Append)) {
		m_isDownloadEnded = true;
		m_isPaused = false;
		m_errorOccurred = true;
		m_errorString = m_file->errorString();
		qCritical() << m_taskId << tr(":无法打开文件进行续传:") << m_errorString << tr(" 下载路径：") << m_fullSavePath;
		TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Failed));
		emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
		return;
	}

	m_isPaused = false;
	m_requestAborted = false;
	m_retryCount = 0;

	emit downloadResumed(m_taskId);
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Downloading));

	startNetworkRequest(m_bytesReceived);
	m_dbSaveTimer.restart();
	m_lastProgressTime.restart();
}

void SingleThreadDownloader::cancel()
{
	if (m_isDownloadEnded) return;

	m_errorOccurred = true;
	m_isCanceled = true;
	m_requestAborted = true;
	m_errorString = tr("下载已取消！");
	if (m_isPaused) {
		m_isPaused = false;
		if (QFile::exists(m_fullSavePath)) {
			QFile::remove(m_fullSavePath);
		}
		TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Failed));
		emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
		return;
	}
	m_isPaused = false;

	if (m_reply) {
		m_reply->abort();
	}
}

void SingleThreadDownloader::retry()
{
	if (!m_errorOccurred) return;
	if (m_file) {
		if (m_file->isOpen()) m_file->close();
		delete m_file;
		m_file = nullptr;
	}
	m_file = new QFile(m_fullSavePath);
	m_retryCount = 0;

	bool fileExists = QFile::exists(m_fullSavePath);
	if (m_isCanceled) {
		if (fileExists) {
			QFile::remove(m_fullSavePath);
		}
		m_bytesReceived = 0;
		if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			m_isCanceled = false;
			m_errorString = m_file->errorString();
			qCritical() << m_taskId << tr(":无法打开文件进行重试:") << m_errorString;
			emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
			return;
		}
	}
	else {
		if (fileExists) {
			m_bytesReceived = QFileInfo(m_fullSavePath).size();
		}
		else {
			m_bytesReceived = 0;
		}
		if (!m_file->open(QIODevice::Append)) {
			m_isCanceled = false;
			m_errorString = m_file->errorString();
			qCritical() << m_taskId << tr(":无法打开文件进行重试:") << m_errorString;
			emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
			return;
		}
	}
	m_isDownloadEnded = false;
	m_isCanceled = false;
	m_errorOccurred = false;
	m_requestAborted = false;
	TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Downloading));
	startNetworkRequest(m_bytesReceived);
	m_dbSaveTimer.restart();
	m_lastProgressTime.restart();
}

void SingleThreadDownloader::startNetworkRequest(qint64 startByte)
{
	QNetworkRequest request(m_url);
	request.setTransferTimeout(15000);
	request.setRawHeader("Accept", "*/*");
	request.setRawHeader("Accept-Encoding", "identity");
	request.setRawHeader("Connection", "keep-alive");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
	if (m_isResumable) {
		request.setRawHeader("Range", QString("bytes=%1-").arg(startByte).toUtf8());
	}

	m_reply = m_networkManager->get(request);
	if (!m_reply) {
		if (m_retryCount < MAX_RETRIES) {
			m_retryCount++;
			m_retryScheduled = true;
			int delay = 1000 * (1 << (m_retryCount - 1));
			m_retryTimer.start(delay);
			return;
		}
		m_retryTimer.stop();
		m_dbSaveTimer.invalidate();
		m_lastProgressTime.invalidate();

		if (m_file && m_file->isOpen()) {
			m_file->flush();
			m_bytesReceived = m_file->size();
		}
		m_retryScheduled = false;
		m_isDownloadEnded = true;
		m_errorOccurred = true;
		m_errorString = tr("无法发起网络请求");
		qCritical() << m_taskId << tr(":无法发起网络请求:") << m_errorString;
		TaskRepository().updateProgress(m_taskId, m_bytesReceived);
		TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Failed));
		emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
		return;
	}

	connect(m_reply, &QNetworkReply::readyRead, this, &SingleThreadDownloader::onReadyRead);
	connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError code) {
		if (!m_requestAborted) {
			if (m_retryCount < MAX_RETRIES) {
				m_retryScheduled = true;
				m_retryCount++;
				int delay = 1000 * (1 << (m_retryCount - 1));
				m_retryTimer.start(delay);
				return;
			}
			m_retryScheduled = false;
			m_errorOccurred = true;
			m_errorString = m_reply->errorString();
			qCritical() << "网络错误:" << code;
		}
		});
	connect(m_reply, &QNetworkReply::finished, this, &SingleThreadDownloader::onFinished);
}

void SingleThreadDownloader::onReadyRead()
{
	if (m_isDownloadEnded || m_isPaused)return;
	if (!m_file || !m_file->isOpen()) {
		m_errorOccurred = true;
		m_requestAborted = true;
		m_errorString = tr("文件未打开");
		m_reply->abort();
		return;
	}
	QByteArray data = m_reply->readAll();
	if (data.isEmpty()) return;

	qint64 written = m_file->write(data);
	if (written > 0) {
		m_bytesReceived += written;
		if (!m_lastProgressTime.isValid() || m_lastProgressTime.elapsed() >= PROGRESS_INTERVAL_MS) {
			emit downloadProgressUpdate(m_taskId, m_bytesReceived);
			m_lastProgressTime.restart();
		}
		if (!m_dbSaveTimer.isValid() || m_dbSaveTimer.elapsed() >= DB_SAVE_INTERVAL_MS) {
			TaskRepository().updateProgress(m_taskId, m_bytesReceived);
			m_dbSaveTimer.restart();
		}
	}
	else {
		m_errorOccurred = true;
		m_requestAborted = true;
		m_errorString = tr("写入文件失败");
		if (m_reply) {
			m_reply->abort();
		}
	}
}

void SingleThreadDownloader::onFinished()
{
	if (m_isDownloadEnded) return;
	if (m_isPaused) {
		cleanupFile();
		if (m_reply) {
			m_reply->deleteLater();
			m_reply = nullptr;
		}
		emit downloadPaused(m_taskId);
		return;
	}

	if (m_retryScheduled) return;

	m_retryTimer.stop();
	m_dbSaveTimer.invalidate();
	m_lastProgressTime.invalidate();
	m_isDownloadEnded = true;

	if (m_errorOccurred) {
		TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Failed));
	}
	else {
		TaskRepository().updateState(m_taskId, static_cast<int>(TaskRepository::TaskState::Completed));
	}
	// 读取网络缓冲区剩余数据
	if (m_reply && m_reply->bytesAvailable() > 0 && m_file && m_file->isOpen()) {
		QByteArray remainingData = m_reply->readAll();
		if (!remainingData.isEmpty()) {
			m_file->write(remainingData);
			m_bytesReceived += remainingData.size();
		}
	}
	if (m_file && m_file->isOpen()) {
		m_file->flush();
	}
	emit downloadProgressUpdate(m_taskId, m_bytesReceived);
	TaskRepository().updateProgress(m_taskId, m_bytesReceived);
	cleanupFile();
	if (m_isCanceled) {
		if (QFile::exists(m_fullSavePath)) {
			QFile::remove(m_fullSavePath);
		}
	}
	if (m_reply) {
		m_reply->deleteLater();
		m_reply = nullptr;
	}
	emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
}

void SingleThreadDownloader::retryRequest()
{
	if (m_isCanceled || m_isPaused || m_isDownloadEnded) return;
	m_retryScheduled = false;
	if (m_file && m_file->isOpen()) { m_file->close(); }

	if (m_isResumable) {
		if (!m_file->open(QIODevice::Append)) {
			m_errorOccurred = true;
			m_requestAborted = true;
			if (m_file) {
				m_errorString = m_file->errorString();
			}
			else {
				m_errorString = tr("无法创建文件进行重试");
			}
			qCritical() << m_taskId << tr(":无法打开文件进行重试:") << m_errorString;
			if (m_reply) {
				m_reply->abort();
			}
			return;
		}
	}
	else {
		if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			m_errorOccurred = true;
			m_requestAborted = true;
			if (m_file) {
				m_errorString = m_file->errorString();
			}
			else {
				m_errorString = tr("无法创建文件进行重试");
			}
			qCritical() << m_taskId << tr(":无法打开文件进行重试:") << m_errorString;
			if (m_reply) {
				m_reply->abort();
			}
			return;
		}
		m_bytesReceived = 0;
	}
	if (m_reply) {
		m_reply->deleteLater();
		m_reply = nullptr;
	}
	startNetworkRequest(m_bytesReceived);
}