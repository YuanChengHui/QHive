#pragma once

#include <QObject>
#include <QThread>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QMutex>
#include <QAtomicInt>

class NetworkRequest;

class NetworkContext : public QObject
{
	Q_OBJECT
public:
	explicit NetworkContext(int id, QObject* parent = nullptr);
	~NetworkContext() override;

	void start();	// 启动网络上下文线程和网络访问管理器
	void stop();	// 停止网络上下文线程和网络访问管理器
	void enqueue(NetworkRequest* request);	// 将网络请求加入队列
	int activeCount() const;	// 获取当前活跃的请求数量

signals:
	//void started();	// 网络上下文启动信号
	//void stopped();	// 网络上下文停止信号

private slots:
	void processQueue();	// 处理请求队列
private:
	int m_id;	// 网络上下文ID
	QThread* m_thread;	// 网络上下文线程
	QNetworkAccessManager* m_nam;	// 网络访问管理器
	QQueue<NetworkRequest*> m_queue;	// 请求队列
	mutable QMutex m_mutex;	// 互斥锁
	QAtomicInt m_activeCount;	// 当前活跃的请求数量
	bool m_running;	// 是否正在运行
};