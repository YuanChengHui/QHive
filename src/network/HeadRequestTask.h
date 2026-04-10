#pragma once

#include <QObject>
#include <QUrl>
#include <QString>	
#include <QNetworkAccessManager>

class HeadRequestTask : public QObject
{
	Q_OBJECT

public:
	explicit HeadRequestTask(const QUrl& url,
		const QString& fileName,
		const QString& saveDir,
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

private:
	void parseFileInfo(QNetworkReply* reply);
	bool checkDiskSpace(qint64 requiredBytes);
	QString generateUniqueFileName(const QString& saveDir, const QString& fileName);

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
};