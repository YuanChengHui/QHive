#include "SingleThreadDownloader.h"

#include <QNetworkRequest>
#include <QDebug>

SingleThreadDownloader::SingleThreadDownloader(const QString& fullSavePath,
	const QUrl& url,
	qint64 totalSize,
	const QString& taskId,
	QObject* parent)
	: QObject(parent)
	, m_fullSavePath(fullSavePath)
	, m_url(url)
	, m_totalSize(totalSize)
	, m_taskId(taskId)
	, m_file(nullptr)
	, m_reply(nullptr)
	, m_errorOccurred(false)
	, m_bytesReceived(0)
	, m_requestAborted(false)
	, m_networkManager(new QNetworkAccessManager(this))
{
}

SingleThreadDownloader::~SingleThreadDownloader()
{
	if (m_reply) {
		delete m_reply;
		m_reply = nullptr;
	}
	if (m_file) {
		m_file->close();
		delete m_file;
		m_file = nullptr;
	}
}

void SingleThreadDownloader::start()
{
	m_file = new QFile(m_fullSavePath);
	if (!m_file->open(QIODevice::WriteOnly)) {
		m_errorOccurred = true;
		m_errorString = m_file->errorString();
		emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
		return;
	}
	startNetworkRequest();
}

void SingleThreadDownloader::cancel()
{
	m_requestAborted = true;
	m_errorOccurred = true;
	m_errorString = tr("下载已取消！");
	if (m_reply) {
		m_reply->abort();
	}
}

void SingleThreadDownloader::retry()
{
	resetState();
	start();
}

void SingleThreadDownloader::resetState()
{
	if (m_reply) {
		delete m_reply;
		m_reply = nullptr;
	}
	if (m_file) {
		m_file->close();
		delete m_file;
		m_file = nullptr;
	}
	if (QFile::exists(m_fullSavePath)) {
		QFile::remove(m_fullSavePath);
	}
	m_requestAborted = false;
	m_errorOccurred = false;
	m_errorString.clear();
	m_bytesReceived = 0;
}

void SingleThreadDownloader::startNetworkRequest()
{
	QNetworkRequest request(m_url);
	request.setTransferTimeout(30000);
	request.setRawHeader("connection", "keep-alive");
	request.setRawHeader("Accept-Encoding", "gzip, deflate, br");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent",
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

	m_reply = m_networkManager->get(request);
	if (!m_reply) {
		emit downloadEnded(m_taskId, true, tr("无法发起网络请求"), m_fullSavePath);
		return;
	}
	m_bytesReceived = 0;

	connect(m_reply, &QNetworkReply::readyRead, this, &SingleThreadDownloader::onReadyRead);
	connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64) {
		m_bytesReceived = bytesReceived;
		emit downloadProgressUpdate(m_taskId, m_bytesReceived);
		});
	connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError code) {
		if (!m_requestAborted) {
			m_errorOccurred = true;
			m_errorString = m_reply->errorString();
			qCritical() << "网络错误:" << code;
		}
		});
	connect(m_reply, &QNetworkReply::finished, this, &SingleThreadDownloader::onFinished);
}

void SingleThreadDownloader::onReadyRead()
{
	if (m_requestAborted || m_errorOccurred) {
		return;
	}

	if (m_file && m_file->isOpen()) {
		QByteArray data = m_reply->readAll();
		if (!data.isEmpty()) {
			m_file->write(data);
		}
	}
	else {
		m_errorOccurred = true;
		m_errorString = m_file ? m_file->errorString() : tr("文件不存在！");
		m_requestAborted = true;
		if (m_reply) {
			disconnect(m_reply, &QNetworkReply::readyRead, this, &SingleThreadDownloader::onReadyRead);
			m_reply->abort();
		}
	}
}

void SingleThreadDownloader::onFinished()
{
	if (!m_reply) {
		m_errorOccurred = true;
		m_errorString = tr("网络请求异常结束！");
		emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
		return;
	}

	if (m_reply->error() == QNetworkReply::NoError && !m_errorOccurred) {
		if (m_file && m_file->isOpen()) {
			QByteArray data = m_reply->readAll();
			if (!data.isEmpty()) {
				m_file->write(data);
			}
			m_file->flush();
		}
		else {
			m_errorOccurred = true;
			m_errorString = m_file ? m_file->errorString() : tr("文件不存在！");
			m_requestAborted = true;
			if (m_reply) {
				m_reply->abort();
			}
		}
	}
	emit downloadEnded(m_taskId, m_errorOccurred, m_errorString, m_fullSavePath);
}