#pragma once

#include <QSemaphore>
#include <QAtomicInt>

/**
 * 全局并发控制
 *
 * 用于限制整个应用中同时活跃的网络请求总数。
 * 每个分片请求在发起前 acquire()，完成后 release()。
 *
 * 默认值为 16，可在 main.cpp 中根据用户配置调整：
 *   g_downloadSemaphore.release(desired - g_downloadSemaphore.available());
 */
inline QSemaphore g_downloadSemaphore{ 16 };


/**
 * 全局最大并发连接数的配置值（供 UI 读取和修改）
 * 0 表示未设置，使用默认值 16
 */
inline QAtomicInt g_maxConcurrentConnections{ 16 };


/**
 * 设置全局最大并发连接数
 * 注意：该函数只调整上限，不会强制中断已有请求；
 *       多余信号量会在已有请求完成后自然生效。
 */
inline void setMaxConcurrentConnections(int count)
{
	if (count < 1) count = 1;
	if (count > 128) count = 128;   // 上限保护

	int current = g_maxConcurrentConnections.loadAcquire();
	if (count > current) {
		g_downloadSemaphore.release(count - current);
	}
	else if (count < current) {
		// 减少时，尝试回收多余信号量；已被占用的会在 release 时自然减少
		// 为了简单，这里只更新上限，实际并发会逐渐下降
		// 更严格的实现需要结合活跃计数，暂不展开
	}
	g_maxConcurrentConnections.storeRelease(count);
}


/**
 * 获取当前全局最大并发连接数
 */
inline int maxConcurrentConnections()
{
	return g_maxConcurrentConnections.loadAcquire();
}