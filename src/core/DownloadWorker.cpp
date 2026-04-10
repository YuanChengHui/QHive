#include "DownloadWorker.h"
#include <QDebug>

DownloadWorker::DownloadWorker(int id, QObject* parent)
	: QObject(parent)
	, m_id(id)
	, m_manager(new QNetworkAccessManager(this))
	, m_reply(nullptr)
	, m_startByte(0)
	, m_endByte(0)
	, m_downloadedBytes(0)
	, m_file(nullptr)
	, m_chunkId(-1)
	, m_cancelled(false)
	, m_running(false)
{
}

DownloadWorker::~DownloadWorker()
{
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->abort();
		m_reply->deleteLater();
		m_reply = nullptr;
	}
	if (m_file) {
		if (m_file->isOpen()) {
			m_file->close();
		}
		delete m_file;
		m_file = nullptr;
	}
}

void DownloadWorker::startWorking()
{
	// 线程已启动，请求分片
	emit needChunk();
}

void DownloadWorker::startDownload(const QUrl& url, qint64 startByte, qint64 endByte,
	const QString& tempFilePath, int chunkId)
{
	if (m_running) {
		qWarning() << tr("Worker %1 已经在运行").arg(m_id);
		return;
	}

	m_url = url;
	m_startByte = startByte;
	m_endByte = endByte;
	m_tempFilePath = tempFilePath;
	m_chunkId = chunkId;
	m_downloadedBytes = 0;
	m_cancelled = false;
	m_running = true;

	// 打开临时文件（写入模式，存在则覆盖）
	m_file = new QFile(m_tempFilePath);
	if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QString fileErr = m_file->errorString();
		delete m_file;
		m_file = nullptr;
		m_running = false;
		emit chunkFinished(m_chunkId, false, tr("无法创建临时文件: %1").arg(fileErr));
		return;
	}

	QNetworkRequest request(m_url);
	QByteArray range = "bytes=" + QByteArray::number(startByte) + "-" + QByteArray::number(endByte);
	request.setRawHeader("Range", range);
	request.setTransferTimeout(30000);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
	m_reply = m_manager->get(request);
	if (!m_reply) {
		emit chunkFinished(m_chunkId, false, tr("无法发起网络请求"));
		return;
	}
	connect(m_reply, &QNetworkReply::readyRead, this, &DownloadWorker::onReadyRead);
	connect(m_reply, &QNetworkReply::finished, this, &DownloadWorker::onFinished);
	connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError code) {
		qWarning() << "Worker" << m_id << "网络错误:" << code;
		});
	m_lastProgressTime.start();
}

void DownloadWorker::cancel()
{
	if (m_cancelled) return;
	m_cancelled = true;
	m_running = false;

	if (m_reply) {
		m_reply->abort();
	}
}

void DownloadWorker::onReadyRead()
{
	if (m_cancelled || !m_reply || !m_file || !m_file->isOpen()) return;

	QByteArray data = m_reply->readAll();
	if (data.isEmpty()) return;

	qint64 written = m_file->write(data);
	if (written > 0) {
		m_downloadedBytes += written;
		// 时间节流
		if (!m_lastProgressTime.isValid() || m_lastProgressTime.elapsed() >= PROGRESS_INTERVAL_MS) {
			emit chunkProgress(m_chunkId, m_downloadedBytes);
			m_lastProgressTime.restart();
		}
	}
	else {
		qWarning() << "Worker" << m_id << "写入临时文件失败";
	}
}

void DownloadWorker::onFinished()
{
	if (!m_reply) {
		emit chunkFinished(m_chunkId, false, tr("网络请求异常结束"));
		m_running = false;
		return;
	}

	bool success = false;
	QString errorString;

	if (!m_cancelled && m_reply->error() == QNetworkReply::NoError) {
		// 确保最后的数据被写入
		if (m_reply->bytesAvailable() > 0) {
			onReadyRead();
		}
		if (m_file) {
			m_file->flush();
			m_file->close();
		}
		success = true;
	}
	else {
		errorString = m_cancelled ? tr("已取消") : m_reply->errorString();
		if (m_file) {
			m_file->close();
			m_file->remove(); // 删除不完整的临时文件
		}
	}

	// 发送最终进度
	emit chunkProgress(m_chunkId, m_downloadedBytes);

	delete m_file;
	m_file = nullptr;

	if (m_reply) {
		m_reply->deleteLater();
		m_reply = nullptr;
	}

	m_running = false;
	emit chunkFinished(m_chunkId, success, errorString);
}