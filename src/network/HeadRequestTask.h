#pragma once

#include <QObject>
#include <QUrl>
#include <QString>	
#include <QNetworkAccessManager>
#include <QTimer>

class HeadRequestTask : public QObject
{
	Q_OBJECT

public:
	explicit HeadRequestTask(const QUrl& url,
		const QString& fileName,
		const QString& saveDir,
		QNetworkAccessManager* networkManager,
		QObject* parent = nullptr);
	~HeadRequestTask() override;

signals:
	void headRequestCompleted(const QString& fileName,
		const QString& fullSavePath,
		const QString& errorString,
		qint64 totalSize,
		bool hasError,
		bool supportsRange);

public slots:
	void startRequest();

private slots:
	void onReplyFinished(QNetworkReply* reply);
	void retryRequest();

private:
	void parseFileInfo(QNetworkReply* reply);
	bool checkDiskSpace(qint64 requiredBytes);
	QString generateUniqueFileName(const QString& saveDir, const QString& fileName);
	void cleanupReply();
	void emitFinalResult();

	QUrl m_url;
	QString m_fileName;
	QString m_saveDir;
	QString m_fullSavePath;
	QString m_fileExtension;
	QString m_errorString;
	bool m_hasError = false;
	qint64 m_totalSize = -1;
	bool m_supportsRange = false;
	QNetworkReply* m_reply = nullptr;
	QNetworkAccessManager* m_networkManager;

	// 重试相关
	int m_retryCount = 0;
	static constexpr int MAX_RETRIES = 3;
	QTimer m_retryTimer;
};