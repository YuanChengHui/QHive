#include "DownloadRequest.h"

#include <QNetworkRequest>
#include <QDebug>

DownloadRequest::DownloadRequest(const QUrl& url, qint64 startByte, qint64 endByte,
	const QString& tempFilePath, int chunkId, const QString& taskId,
	QObject* parent)
	: NetworkRequest(parent)
	, m_url(url)
	, m_startByte(startByte)
	, m_endByte(endByte)
	, m_tempFilePath(tempFilePath)
	, m_chunkId(chunkId)
	, m_taskId(taskId)
{
}

DownloadRequest::~DownloadRequest()
{
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->deleteLater();
		m_reply = nullptr;
	}
}

void DownloadRequest::start(QNetworkAccessManager* nam)
{
	// 如果在提交之前就被取消，直接结束
	if (m_aborted) {
		emit finished(m_chunkId, false, tr("已取消"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	if (!nam) {
		emit finished(m_chunkId, false, tr("NetworkAccessManager 为空"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}
	m_nam = nam;

	QNetworkRequest request(m_url);
	if (m_startByte >= 0 && m_endByte >= m_startByte) {
		QByteArray range = "bytes=" + QByteArray::number(m_startByte) + "-" + QByteArray::number(m_endByte);
		request.setRawHeader("Range", range);
	}
	request.setRawHeader("Connection", "keep-alive");
	request.setTransferTimeout(15000);
	request.setRawHeader("Accept", "*/*");
	request.setRawHeader("Accept-Encoding", "identity");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

	m_reply = m_nam->get(request);
	if (!m_reply) {
		emit finished(m_chunkId, false, tr("无法发起网络请求"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	connect(m_reply, &QNetworkReply::readyRead, this, &DownloadRequest::onReadyRead);
	connect(m_reply, &QNetworkReply::finished, this, &DownloadRequest::onFinished);
	connect(m_reply, &QNetworkReply::errorOccurred, this, &DownloadRequest::onErrorOccurred);
}

void DownloadRequest::abort()
{
	m_aborted = true;
	if (m_reply) {
		m_reply->abort();
	}
}

void DownloadRequest::onReadyRead()
{
	if (!m_reply || m_aborted) return;

	QByteArray data = m_reply->readAll();
	if (data.isEmpty()) return;

	m_receivedBytes += data.size();
	emit dataReceived(m_chunkId, data);
	emit progress(m_chunkId, m_receivedBytes);
}

void DownloadRequest::onFinished()
{
	if (!m_reply) {
		emit finished(m_chunkId, false, tr("网络请求异常结束"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	if (m_aborted) {
		emit finished(m_chunkId, false, tr("已取消"));
		m_reply->deleteLater();
		m_reply = nullptr;
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	if (m_reply->error() != QNetworkReply::NoError) {
		emit finished(m_chunkId, false, m_reply->errorString());
	}
	else {
		if (m_reply->bytesAvailable() > 0) {
			QByteArray data = m_reply->readAll();
			if (!data.isEmpty()) {
				m_receivedBytes += data.size();
				emit dataReceived(m_chunkId, data);
				emit progress(m_chunkId, m_receivedBytes);
			}
		}
		emit finished(m_chunkId, true, QString());
	}

	m_reply->deleteLater();
	m_reply = nullptr;
	if (!m_released) { m_released = true; emit released(); }
}

void DownloadRequest::onErrorOccurred(QNetworkReply::NetworkError code)
{
	Q_UNUSED(code);
	// 错误在 onFinished 中统一处理
}