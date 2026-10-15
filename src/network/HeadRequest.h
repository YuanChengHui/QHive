#pragma once

#include "NetworkRequest.h"

#include <QNetworkReply>
#include <QUrl>
#include <QString>
#include <QByteArray>

class HeadRequest : public NetworkRequest
{
	Q_OBJECT
public:
	explicit HeadRequest(const QUrl& url, QObject* parent = nullptr);
	~HeadRequest() override;

	void start(QNetworkAccessManager* nam) override;
	void abort() override;

signals:
	// 成功拿到响应头后发出（原始信息，由主线程解析）
	void headersReady(qint64 totalSize,
		const QByteArray& contentDisposition,
		const QByteArray& contentType,
		bool supportsRange);

	// 失败/取消
	void failed(const QString& error);

private slots:
	void onFinished();

private:
	QUrl m_url;
	QNetworkAccessManager* m_nam = nullptr;
	QNetworkReply* m_reply = nullptr;
	bool m_aborted = false;
	bool m_released = false;
};