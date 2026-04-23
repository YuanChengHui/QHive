#include "DownloadWorker.h"
#include <QDebug>

DownloadWorker::DownloadWorker(int id, QObject* parent)
	: QObject(parent)
	, m_id(id)
	, m_manager(nullptr)
	, m_reply(nullptr)
	, m_startByte(0)
	, m_endByte(0)
	, m_downloadedBytes(0)
	, m_tempFile(nullptr)
	, m_chunkId(-1)
	, m_failed(false)
	, m_canceled(false)
	, m_paused(false)
	, m_isWorking(false)
{
}

DownloadWorker::~DownloadWorker()
{
	cleanup();
}

void DownloadWorker::requestChunk()
{
	// 线程已启动，请求分片
	emit needChunk();
}

void DownloadWorker::startDownload(const QUrl& url, qint64 startByte, qint64 endByte,
	const QString& tempFilePath, int chunkId, qint64 downloadedBytes)
{
	if (m_manager == nullptr) {
		m_manager = new QNetworkAccessManager(this);
	}
	m_isWorking = true;
	m_failed = false;

	m_url = url;
	m_chunkId = chunkId;
	m_startByte = startByte;
	m_endByte = endByte;
	m_downloadedBytes = downloadedBytes;          // 外部传入已下载量
	m_tempFilePath = tempFilePath;

	QIODevice::OpenMode mode = QIODevice::WriteOnly;
	if (m_downloadedBytes > 0) {
		mode |= QIODevice::Append;                // 续传模式
	}
	else {
		mode |= QIODevice::Truncate;              // 全新下载
	}

	m_tempFile = new QFile(m_tempFilePath);
	if (!m_tempFile->open(mode)) {
		QString fileErr = m_tempFile->errorString();
		delete m_tempFile;
		m_tempFile = nullptr;
		m_failed = true;
		m_isWorking = false;
		cleanup();
		emit workEnded(m_chunkId, false, tr("无法创建临时文件: %1").arg(fileErr));
		return;
	}

	// 如果是续传，验证文件大小一致性
	if (m_downloadedBytes > 0 && m_tempFile->size() != m_downloadedBytes) {
		qWarning() << "Worker" << m_id << "临时文件大小不一致，重新开始";
		m_tempFile->close();
		m_tempFile->remove();
		delete m_tempFile;
		m_tempFile = nullptr;
		// 递归调用自身，从头开始
		startDownload(url, startByte, endByte, tempFilePath, chunkId, 0);
		return;
	}

	QNetworkRequest request(m_url);
	qint64 requestStart = m_startByte + m_downloadedBytes;
	QByteArray range = "bytes=" + QByteArray::number(requestStart) + "-" + QByteArray::number(m_endByte);
	request.setRawHeader("Range", range);
	request.setRawHeader("Connection", "keep-alive");
	request.setTransferTimeout(15000);
	request.setRawHeader("Accept", "*/*");
	request.setRawHeader("Accept-Encoding", "identity");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

	m_reply = m_manager->get(request);
	if (!m_reply) {
		m_failed = true;
		m_isWorking = false;
		emit workEnded(m_chunkId, false, tr("无法发起网络请求"));
		return;
	}
	connect(m_reply, &QNetworkReply::readyRead, this, &DownloadWorker::onReadyRead);
	connect(m_reply, &QNetworkReply::finished, this, &DownloadWorker::onFinished);
	connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError code) {
		m_failed = true;
		qWarning() << "Worker" << m_id << "网络错误:" << code;
		});
	m_lastProgressTime.start();
}

void DownloadWorker::pause()
{
	if (m_paused) return;
	m_paused = true;
	if (m_reply) {
		m_reply->abort();
	}
}

void DownloadWorker::cancel()
{
	if (m_canceled) return;
	m_canceled = true;
	if (m_reply) {
		m_reply->abort();
	}
}

void DownloadWorker::onReadyRead()
{
	if (m_paused || m_canceled || m_failed) return;

	QByteArray data = m_reply->readAll();
	if (data.isEmpty()) return;

	qint64 written = m_tempFile->write(data);
	if (written > 0) {
		m_downloadedBytes += written;
		if (!m_lastProgressTime.isValid() || m_lastProgressTime.elapsed() >= PROGRESS_INTERVAL_MS) {
			emit workUpdated(m_chunkId, m_downloadedBytes);
			m_lastProgressTime.restart();
		}
	}
	else {
		qWarning() << "Worker" << m_id << "写入临时文件失败";
		// 写入失败，终止当前分片任务
		m_failed = true;
		if (m_reply) {
			m_reply->abort();
		}
	}
}

void DownloadWorker::onFinished()
{
	m_lastProgressTime.invalidate(); // 请求结束后不再更新进度时间
	if (!m_reply) {
		m_isWorking = false;
		cleanup();
		emit workUpdated(m_chunkId, m_downloadedBytes);
		emit workEnded(m_chunkId, false, tr("网络请求异常结束"));
		return;
	}
	if (m_paused) {
		cleanup();
		emit workUpdated(m_chunkId, m_downloadedBytes);
		emit workPaused(m_chunkId, m_isWorking);
		return;
	}
	bool success = false;
	QString errorString;
	m_isWorking = false;

	if (m_canceled) {
		errorString = tr("已取消");
		cleanup();
		emit workUpdated(m_chunkId, m_downloadedBytes);
		emit workEnded(m_chunkId, success, errorString);
		return;
	}

	if (!m_failed) {
		if (m_reply->bytesAvailable() > 0) {
			onReadyRead();
		}
		cleanup();
		success = true;
	}
	else {
		errorString = m_reply->errorString();
		cleanup();
	}
	emit workUpdated(m_chunkId, m_downloadedBytes);
	emit workEnded(m_chunkId, success, errorString);
}

void DownloadWorker::cleanup()
{
	if (m_tempFile) {
		if (m_tempFile->isOpen()) {
			m_tempFile->close();
		}
		delete m_tempFile;
		m_tempFile = nullptr;
	}
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->deleteLater();
		m_reply = nullptr;
	}
}