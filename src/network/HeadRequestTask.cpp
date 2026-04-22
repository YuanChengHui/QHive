#include "HeadRequestTask.h"

#include <QRegularExpression>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMimeDatabase>
#include <QStorageInfo>
#include <QByteArray>
#include <QMimeType>
#include <QFileInfo>
#include <QVariant>
#include <QDebug>

HeadRequestTask::HeadRequestTask(const QUrl& url,
	const QString& fileName,
	const QString& saveDir,
	QNetworkAccessManager* networkManager,
	QObject* parent)
	: QObject(parent)
	, m_url(url)
	, m_fileName(fileName)
	, m_saveDir(saveDir)
	, m_networkManager(networkManager)
{
	m_retryTimer.setSingleShot(true);
	connect(&m_retryTimer, &QTimer::timeout, this, &HeadRequestTask::retryRequest);
}

HeadRequestTask::~HeadRequestTask()
{
	m_retryTimer.stop();
	cleanupReply();
}

void HeadRequestTask::cleanupReply()
{
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->deleteLater();
		m_reply = nullptr;
	}
}

void HeadRequestTask::emitFinalResult()
{
	emit headRequestCompleted(m_fileName, m_fullSavePath, m_errorString, m_totalSize, m_hasError, m_supportsRange);
}

void HeadRequestTask::startRequest()
{
	cleanupReply();

	QNetworkRequest request(m_url);
	request.setTransferTimeout(10000);
	request.setRawHeader("Accept", "*/*");
	request.setRawHeader("Connection", "keep-alive");
	request.setRawHeader("Accept-Encoding", "identity");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

	m_reply = m_networkManager->head(request);
	if (!m_reply) {
		// 无法创建请求对象，尝试重试
		if (m_retryCount < MAX_RETRIES) {
			m_retryCount++;
			int delay = 1000 * (1 << (m_retryCount - 1)); // 1s, 2s, 4s
			qDebug() << "[HeadRequestTask] 创建请求失败，将在" << delay << "ms后重试(第" << m_retryCount << "次)";
			m_retryTimer.start(delay);
			return;
		}
		// 重试耗尽
		m_hasError = true;
		m_errorString = tr("无法发起 HEAD 请求");
		emitFinalResult();
		return;
	}

	qDebug() << "[HeadRequestTask] 正在发送 HEAD 请求到:" << m_url.toString()
		<< "(已尝试次数:" << m_retryCount + 1 << ")";

	connect(m_reply, &QNetworkReply::finished, this, [this]() { onReplyFinished(m_reply); });
	connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError code) {
		qCritical() << "[HeadRequestTask] HEAD请求网络错误:"
			<< "\nURL:" << m_url.toString()
			<< "\nCode:" << code
			<< "\nError:" << m_reply->errorString();
		});
}

void HeadRequestTask::retryRequest()
{
	// 重新发起请求
	startRequest();
}

void HeadRequestTask::onReplyFinished(QNetworkReply* reply)
{
	if (!reply) {
		m_hasError = true;
		m_errorString = tr("网络响应为空");
		emitFinalResult();
		return;
	}

	// 成功
	if (reply->error() == QNetworkReply::NoError) {
		m_retryCount = 0; // 重置计数
		parseFileInfo(reply);
		emitFinalResult();
		return;
	}

	// 失败，判断是否需要重试
	if (m_retryCount < MAX_RETRIES) {
		m_retryCount++;
		int delay = 1000 * (1 << (m_retryCount - 1)); // 指数退避
		qDebug() << "[HeadRequestTask] HEAD 请求失败，将在" << delay / 1000 << "s后重试(第" << m_retryCount << "次)";
		m_retryTimer.start(delay);
	}
	else {
		// 重试耗尽
		m_hasError = true;
		m_errorString = reply->errorString();
		qCritical() << "[HeadRequestTask] HEAD 请求最终失败，已重试" << MAX_RETRIES << "次";
		emitFinalResult();
	}
}

static QString formatBytes(qint64 bytes) {
	if (bytes < 1024) return QString("%1 B").arg(bytes);
	if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
	if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 2);
	return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

void HeadRequestTask::parseFileInfo(QNetworkReply* reply)
{
	// --- 获取文件总大小 ---
	QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
	if (length.isValid()) {
		m_totalSize = length.toLongLong();
		if (m_totalSize > 0) {
			if (checkDiskSpace(m_totalSize)) {
				qDebug() << QString("[ %1 ] 磁盘空间检查通过，剩余空间足够下载文件。").arg(m_saveDir);
			}
			else {
				return;
			}
		}
	}

	// 1. 优先从 Content-Disposition 获取文件名
	QByteArray contentDisposition = reply->rawHeader("Content-Disposition");
	if (!contentDisposition.isEmpty()) {
		QRegularExpression rx(R"(filename[^;\n=\r]*=((['])[^']*\2|[^;\n\r]*))");
		QRegularExpressionMatch match = rx.match(QString::fromUtf8(contentDisposition));
		if (match.hasMatch()) {
			QString fileName = match.captured(1).trimmed();
			if (fileName.startsWith(QLatin1Char('"')) && fileName.endsWith(QLatin1Char('"'))) {
				m_fileName = fileName.mid(1, fileName.length() - 2);
			}
			else {
				m_fileName = fileName;
			}
		}
	}
	// 2. 如果方法一失败，从 URL 推断，即使用传入的 m_fileName（已在构造函数中设置）

	// --- 从文件名推断扩展名 ---
	QFileInfo fileInfo(m_fileName);
	m_fileExtension = fileInfo.completeSuffix();
	// 如果没有扩展名，尝试从 MIME 类型推断
	if (m_fileExtension.isEmpty()) {
		QVariant contentType = reply->header(QNetworkRequest::ContentTypeHeader);
		if (contentType.isValid()) {
			QMimeDatabase mimeDb;
			QMimeType mimeType = mimeDb.mimeTypeForName(contentType.toString());
			if (mimeType.isValid()) {
				m_fileExtension = mimeType.preferredSuffix();
				m_fileName += "." + m_fileExtension;
			}
		}
	}
	// 生成唯一文件名
	m_fileName = generateUniqueFileName(m_saveDir, m_fileName);
	m_fullSavePath = m_saveDir + m_fileName;

	// --- 检测服务器是否支持断点续传 ---
	QByteArray acceptRanges = reply->rawHeader("Accept-Ranges");
	if (acceptRanges.trimmed().toLower() == "bytes") {
		m_supportsRange = true;
	}

	qDebug() << QString("[ %1 ] HEAD 请求成功。支持断点续传: %2").arg(m_fileName).arg(m_supportsRange ? "是" : "否")
		<< QString("文件大小: %1").arg(formatBytes(m_totalSize));

	// 成功，无错误
	m_hasError = false;
	m_errorString.clear();
}

bool HeadRequestTask::checkDiskSpace(qint64 requiredBytes)
{
	QStorageInfo storageInfo(m_saveDir);
	if (!storageInfo.isValid()) {
		m_hasError = true;
		m_errorString = tr("无法获取磁盘信息: %1").arg(m_saveDir);
		return false;
	}
	qint64 availableBytes = storageInfo.bytesAvailable();
	if (availableBytes < requiredBytes) {
		m_hasError = true;
		m_errorString = QString("[ %1 ] 磁盘空间不足。需要 %2 ，可用 %3。")
			.arg(m_saveDir)
			.arg(formatBytes(requiredBytes))
			.arg(formatBytes(availableBytes));
		return false;
	}
	return true;
}

QString HeadRequestTask::generateUniqueFileName(const QString& saveDir, const QString& fileName)
{
	QFileInfo fileInfo(saveDir + fileName);
	if (!fileInfo.exists()) {
		return fileName;
	}

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