#include "HeadRequest.h"

#include <QNetworkRequest>
#include <QDebug>

HeadRequest::HeadRequest(const QUrl& url, QObject* parent)
	: NetworkRequest(parent)
	, m_url(url)
{
}

HeadRequest::~HeadRequest()
{
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->deleteLater();
		m_reply = nullptr;
	}
}

void HeadRequest::start(QNetworkAccessManager* nam)
{
	// 如果在提交之前就被取消，直接结束
	if (m_aborted) {
		emit failed(tr("已取消"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	if (!nam) {
		emit failed(tr("NetworkAccessManager 为空"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}
	m_nam = nam;

	QNetworkRequest request(m_url);
	request.setTransferTimeout(10000);
	request.setRawHeader("Accept", "*/*");
	request.setRawHeader("Connection", "keep-alive");
	request.setRawHeader("Accept-Encoding", "identity");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

	m_reply = m_nam->head(request);
	if (!m_reply) {
		emit failed(tr("无法发起 HEAD 请求"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	connect(m_reply, &QNetworkReply::finished, this, &HeadRequest::onFinished);
}

void HeadRequest::abort()
{
	m_aborted = true;
	if (m_reply) {
		m_reply->abort();
	}
}

void HeadRequest::onFinished()
{
	// 检查网络响应是否为空
	if (!m_reply) {
		emit failed(tr("网络响应为空"));
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	// 检查是否被取消
	if (m_aborted) {
		emit failed(tr("已取消"));
		m_reply->deleteLater();
		m_reply = nullptr;
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	// 检查网络请求是否成功
	if (m_reply->error() != QNetworkReply::NoError) {
		emit failed(m_reply->errorString());
		m_reply->deleteLater();
		m_reply = nullptr;
		if (!m_released) { m_released = true; emit released(); }
		return;
	}

	// 只负责把原始响应头信息发出去
	QVariant lengthVariant = m_reply->header(QNetworkRequest::ContentLengthHeader);
	qint64 totalSize = lengthVariant.isValid() ? lengthVariant.toLongLong() : -1;

	QByteArray contentDisposition = m_reply->rawHeader("Content-Disposition");
	QByteArray contentType = m_reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();

	QByteArray acceptRanges = m_reply->rawHeader("Accept-Ranges");
	bool supportsRange = (acceptRanges.trimmed().toLower() == "bytes");

	emit headersReady(totalSize, contentDisposition, contentType, supportsRange);

	m_reply->deleteLater();
	m_reply = nullptr;
	if (!m_released) { m_released = true; emit released(); }
}