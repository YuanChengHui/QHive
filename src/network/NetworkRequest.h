#pragma once

#include <QObject>
#include <QNetworkAccessManager>

class NetworkRequest : public QObject
{
	Q_OBJECT
public:
	explicit NetworkRequest(QObject* parent = nullptr) : QObject(parent) {}
	~NetworkRequest() override = default;

	virtual void start(QNetworkAccessManager* nam) = 0;	// 开始请求
	virtual void abort() = 0;	// 取消请求

signals:
	void released();	// 通知 DownloadTask 释放信号量
};