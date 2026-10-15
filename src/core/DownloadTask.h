#pragma once

#include <QObject>
#include <QUrl>
#include <QMap>
#include <QQueue>
#include <QTimer>
#include "DownloadTypes.h"

class DownloadRequest;

class DownloadTask : public QObject
{
	Q_OBJECT
public:
	DownloadTask(const QString& taskId, const QUrl& url, const QString& savePath,
		qint64 totalSize, bool supportsRange, int chunkCount,
		QObject* parent = nullptr);
	~DownloadTask() override;

	void start();
	void pause();
	void resume();
	void cancel();
	void retry();

	// 恢复相关
	void restoreChunks(const QString& json);
	void markAsPaused();
	void markAsFailed();

	QString taskId() const { return m_taskId; }

signals:
	void progress(const QString& taskId, qint64 received);
	void paused(const QString& taskId);
	void resumed(const QString& taskId);
	void ended(const QString& taskId, bool error,
		const QString& errorString, const QString& savePath);

private slots:
	void onChunkData(int chunkId, const QByteArray& data);
	void onChunkProgress(int chunkId, qint64 received);
	void onChunkFinished(int chunkId, bool success, const QString& error);
	void onMergeFinished(bool success, const QString& error);

private:
	void initChunks();	// 初始化分片信息
	void dispatchChunk(int chunkId);	// 分片下载调度
	void retryChunk(int chunkId);	// 分片下载重试
	void checkCompletion();	// 检查下载是否完成
	void mergeChunksAsync();	// 异步合并分片
	void updateStateToDatabase(int state);	// 更新下载任务状态到数据库
	void cleanupTempFiles();	// 清理临时文件
	qint64 totalDownloaded() const;	// 计算总下载字节数

	QString m_taskId;
	QUrl m_url;
	QString m_savePath;
	QString m_tempDir;
	qint64 m_totalSize;
	bool m_supportsRange;
	int m_chunkCount;

	QMap<int, ChunkInfo> m_chunks;	// 分片信息，key 为分片 ID
	QQueue<int> m_pendingChunks;	// 待下载的分片队列
	QQueue<int> m_failedChunks;		// 下载失败的分片队列
	QQueue<int> m_successfulChunks;	// 下载成功的分片队列
	QMap<int, qint64> m_chunkDownloaded;			// 分片已下载的字节数，key 为分片 ID
	QMap<int, qint64> m_chunkBaseDownloaded;		// 分片已下载的基础字节数，key 为分片 ID
	QMap<int, DownloadRequest*> m_activeRequests;	// 正在下载的分片请求，key 为分片 ID

	bool m_isPaused = false;
	bool m_isFailed = false;
	bool m_isEnded = false;
	bool m_isMerging = false;

	QTimer* m_progressTimer = nullptr;	// 用于定时更新进度
	QTimer* m_dbSaveTimer = nullptr;	// 用于定时保存数据库
};