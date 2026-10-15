#include "NetworkContextPool.h"
#include "NetworkRequest.h"

#include <QCoreApplication>
#include <QDebug>

NetworkContextPool* NetworkContextPool::instance()
{
	static NetworkContextPool* inst = new NetworkContextPool();
	return inst;
}

NetworkContextPool::NetworkContextPool(QObject* parent)
	: QObject(parent)
	, m_nextIndex(0)
	, m_initialized(false)
{
}

NetworkContextPool::~NetworkContextPool()
{
	shutdown();
}

void NetworkContextPool::init(int contextCount)
{
	if (m_initialized) return;
	if (contextCount < 1) contextCount = 1;

	for (int i = 0; i < contextCount; ++i) {
		NetworkContext* ctx = new NetworkContext(i);
		ctx->start();
		m_contexts.append(ctx);
	}
	m_initialized = true;
}

void NetworkContextPool::shutdown()
{
	if (!m_initialized) return;

	for (NetworkContext* ctx : m_contexts) {
		ctx->stop();
		ctx->deleteLater();
	}
	m_contexts.clear();
	m_initialized = false;

	// 处理一下事件，让 deleteLater 生效
	QCoreApplication::processEvents();
}

void NetworkContextPool::submit(NetworkRequest* request)
{
	if (!request) return;

	if (!m_initialized) {
		// 池未初始化，通知调用方释放资源
		QMetaObject::invokeMethod(request, "released", Qt::DirectConnection);
		request->deleteLater();
		return;
	}

	// 轮询选择 Context
	int index = m_nextIndex.fetchAndAddRelaxed(1) % m_contexts.size();
	NetworkContext* target = m_contexts.at(index);

	if (request->thread() != target->thread()) {
		request->moveToThread(target->thread());
	}

	target->enqueue(request);
}

int NetworkContextPool::contextCount() const
{
	return m_contexts.size();
}