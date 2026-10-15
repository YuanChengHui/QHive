#pragma once

#include <QString>
#include <QMetaType>

/**
 * 任务状态枚举
 * 与 TaskRepository::TaskState 保持数值一致，方便直接转换
 */
enum class TaskState {
	Waiting = 0,
	Downloading = 1,
	Paused = 2,
	Completed = 3,
	Failed = 4
};

/**
 * 单个分片的信息
 * 用于分片调度、状态持久化、恢复
 */
struct ChunkInfo {
	int     id = 0;          // 分片 ID，从 0 开始
	qint64  startByte = 0;          // 分片起始字节（闭区间）
	qint64  endByte = 0;          // 分片结束字节（闭区间）
	QString tempFilePath;               // 临时文件路径
	QString errorString;                // 失败原因
	bool    isEnded = false;      // 是否结束（成功或最终失败）
	bool    isSuccessful = false;      // 是否成功
	int     retryCount = 0;          // 已重试次数
	qint64  downloaded = 0;         // 已下载字节数（用于断点续传）
};

// 让 ChunkInfo 可以跨线程通过信号槽传递
Q_DECLARE_METATYPE(ChunkInfo)