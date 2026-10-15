#include "NetworkContext.h"
#include "NetworkRequest.h"

#include <QDebug>

NetworkContext::NetworkContext(int id, QObject* parent)
	: QObject(parent)
	, m_id(id)
	, m_thread(nullptr)
	, m_nam(nullptr)
	, m_activeCount(0)
	, m_running(false)
{
}

NetworkContext::~NetworkContext()
{
	stop();
}

void NetworkContext::start()
{
	if (m_running) return;

	m_thread = new QThread();
	this->moveToThread(m_thread);

	connect(m_thread, &QThread::started, this, [this]() {
		m_nam = new QNetworkAccessManager(this);
		m_running = true;
		//emit started();	// 发出启动信号，预留
		processQueue();
		});

	connect(m_thread, &QThread::finished, this, [this]() {
		if (m_nam) {
			m_nam->deleteLater();
			m_nam = nullptr;
		}
		m_running = false;
		//emit stopped();	// 发出停止信号，预留
		});

	m_thread->start();	// 启动线程，线程启动后会创建 QNetworkAccessManager 并处理队列
}

void NetworkContext::stop()
{
	if (!m_thread) return;

	// 丢弃未处理的请求，并通知释放（避免信号量泄漏）
	QList<NetworkRequest*> pending;
	{
		QMutexLocker locker(&m_mutex);
		while (!m_queue.isEmpty()) {
			pending.append(m_queue.dequeue());
		}
	}
	for (NetworkRequest* req : pending) {
		if (req) {
			emit req->released();   // 通知 DownloadTask 释放信号量
			req->deleteLater();
		}
	}

	m_thread->quit();
	m_thread->wait(3000);
	m_thread->deleteLater();
	m_thread = nullptr;
	m_running = false;
}

void NetworkContext::enqueue(NetworkRequest* request)
{
	if (!request) return;

	{
		QMutexLocker locker(&m_mutex);
		m_queue.enqueue(request);
	}

	QMetaObject::invokeMethod(this, "processQueue", Qt::QueuedConnection);
}

int NetworkContext::activeCount() const
{
	return m_activeCount.loadAcquire();
}

void NetworkContext::processQueue()
{
	if (!m_running || !m_nam) return;

	while (true) {
		NetworkRequest* request = nullptr;
		{
			QMutexLocker locker(&m_mutex);
			if (m_queue.isEmpty()) break;
			request = m_queue.dequeue();
		}
		if (!request) break;

		// 统一监听 released 信号，用于活跃计数递减和 deleteLater
		connect(request, &NetworkRequest::released, this,
			[this, request]() {
				m_activeCount.deref();
				request->deleteLater();
			}, Qt::QueuedConnection);

		m_activeCount.ref();
		request->start(m_nam);
	}
}