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
	QObject* parent)
	: QObject(parent),
	m_url(url),
	m_fileName(fileName),
	m_saveDir(saveDir),
	m_networkManager(new QNetworkAccessManager(this))
{
}

HeadRequestTask::~HeadRequestTask()
{
	if (m_reply) {
		m_reply->deleteLater();
		m_reply = nullptr;
	}
	if (m_networkManager) {
		m_networkManager->deleteLater();
		m_networkManager = nullptr;
	}
}

static QString formatBytes(qint64 bytes) {
	if (bytes < 1024) return QString("%1 B").arg(bytes);
	if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
	if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 2);
	return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

void HeadRequestTask::startRequest()
{
	QNetworkRequest request(m_url);
	request.setTransferTimeout(10000);
	request.setRawHeader("Accept-Encoding", "gzip, deflate, br");
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
	m_reply = m_networkManager->head(request);
	if (!m_reply) {
		m_hasError = true;
		m_errorString = tr("无法发起 HEAD 请求");
		emit headRequestCompleted(m_fileName, m_fullSavePath, m_errorString, m_totalSize, m_hasError, m_supportsRange);
		return;
	}

	qDebug() << "[HeadRequestTask] 正在发送 HEAD 请求到:" << m_url.toString();

	connect(m_reply, &QNetworkReply::finished, this, [this]() { onReplyFinished(m_reply); });
	connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError code) {
		m_hasError = true;
		m_errorString = m_reply->errorString();
		qCritical() << "[HeadRequestTask] HEAD请求网络错误发生:"
			<< "\nURL:" << m_url.toString()
			<< "\nCode:" << code
			<< "\nError:" << m_errorString;
		});
}

void HeadRequestTask::onReplyFinished(QNetworkReply* reply)
{
	if (!reply) {
		m_hasError = true;
		m_errorString = tr("网络响应为空");
		emit headRequestCompleted(m_fileName, m_fullSavePath, m_errorString, m_totalSize, m_hasError, m_supportsRange);
		return;
	}

	if (reply->error() == QNetworkReply::NoError) {
		parseFileInfo(reply);
	}
	emit headRequestCompleted(m_fileName, m_fullSavePath, m_errorString, m_totalSize, m_hasError, m_supportsRange);
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
				qCritical() << QString("磁盘空间检查失败:\n %1").arg(m_errorString);
				return;
			}
		}
	}

	// 1. 优先从 Content-Disposition 获取文件名
	QByteArray contentDisposition = reply->rawHeader("Content-Disposition");
	if (!contentDisposition.isEmpty()) {
		QRegularExpression rx(R"(filename[^;\n=\r]*=((['])[^']*\2|[^;\n\r]*))");
		QRegularExpressionMatch match = rx.match(contentDisposition);
		if (match.hasMatch()) {
			QString	fileName = match.captured(1).trimmed();
			if (fileName.startsWith(QLatin1Char('"')) && fileName.endsWith(QLatin1Char('"'))) {
				m_fileName = fileName.mid(1, fileName.length() - 2);
			}
		}
	}// 2. 如果方法一失败，从 URL 推断,即 m_fileName	

	// --- 从文件名推断扩展名 ---
	QFileInfo fileInfo(m_fileName);
	m_fileExtension = fileInfo.completeSuffix();
	// 如果没有扩展名，尝试从 MIME 类型推断
	if (m_fileExtension.isEmpty()) {
		QVariant contentType = reply->header(QNetworkRequest::ContentTypeHeader);
		if (contentType.isValid()) {
			{
				QMimeDatabase mimeDb;
				QMimeType mimeType = mimeDb.mimeTypeForName(contentType.toString());
				if (mimeType.isValid()) {
					m_fileExtension = mimeType.preferredSuffix();
					m_fileName += "." + m_fileExtension;
				}
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

	qDebug() << QString("[ %1 ] HEAD 请求成功。支持断点续传: %2").arg(m_fileName).arg(m_supportsRange)
		<< QString("文件大小: %1").arg(formatBytes(m_totalSize));

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