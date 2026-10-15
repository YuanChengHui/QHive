#pragma once

#include "NetworkContext.h"

#include <QObject>
#include <QList>
#include <QAtomicInt>

class NetworkRequest;

class NetworkContextPool : public QObject
{
	Q_OBJECT
public:
	static NetworkContextPool* instance();

	void init(int contextCount = 4);	// 初始化网络上下文池
	void shutdown();	// 关闭所有上下文
	void submit(NetworkRequest* request);	// 提交网络请求
	int contextCount() const;	// 获取上下文数量
	bool isInitialized() const { return m_initialized; }	// 是否已初始化

private:
	explicit NetworkContextPool(QObject* parent = nullptr);
	~NetworkContextPool() override;

	QList<NetworkContext*> m_contexts;	// 网络上下文列表
	QAtomicInt m_nextIndex;	// 下一个要使用的上下文索引
	bool m_initialized;	// 是否已初始化
};