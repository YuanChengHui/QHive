#pragma once

#include "DownloadWorker.h"

#include <QObject>
#include <QUrl>
#include <QMap>
#include <QQueue>
#include <QThread>
#include <QList>
#include <QTimer>

class MultiThreadDownloader : public QObject
{
	Q_OBJECT

public:
	explicit MultiThreadDownloader(const QString& fullSavePath, const QUrl& url,
		qint64 totalSize, const QString& taskId, int threadCount = 8, QObject* parent = nullptr);
	~MultiThreadDownloader() override;

	void start();
	void cancel();
	void retry();

private:
	void calculateRanges();
	void startWorkers();
	void markPendingAsFailed();
	void checkAllCompleted();
	void mergeChunks();
	void stopAllThreads();
	void cleanupTempFiles();
	void resetState();
	void updateTotalProgress();	// 定期汇总进度

signals:
	void downloadProgressUpdate(const QString& taskId, qint64 received);
	void downloadEnded(const QString& taskId, bool errorOccurred, const QString& errorString, const QString& savePath);

private slots:
	void onWorkerProgress(int chunkId, qint64 bytesReceived);
	void onWorkerFinished(int chunkId, bool success, const QString& error);
	void assignNextChunk();

private:
	struct ChunkInfo {
		int id = 0;
		qint64 startByte = 0;
		qint64 endByte = 0;
		QString tempFilePath;

		QString errorString;
		bool failed = false;
		bool completed = false;
		int retryCount = 0;        // 重试次数
	};

	QUrl m_url;
	QString m_taskId;
	QString m_fullSavePath;
	qint64 m_totalSize;
	int m_threadCount;

	QMap<int, ChunkInfo> m_chunks;	// 存储每个 chunk 的信息，key 是 chunk ID
	QQueue<int> m_pendingChunks;	// 存储待分配的 chunk ID
	QMap<int, qint64> m_chunkDownloaded;	// 进度跟踪（每个 chunk 已下载字节数）


	QList<QThread*> m_threads;	// 管理 worker 线程
	QList<DownloadWorker*> m_workers;	// 管理 worker 对象
	QMap<DownloadWorker*, bool> m_workerIdle;	// 跟踪每个 worker 是否空闲

	int m_completedChunks = 0;// 成功的 chunk 数量
	int m_failedChunks = 0;// 失败的 chunk 数量（包括 worker 失败和取消导致的失败）

	// 定时器合并总进度
	QTimer* m_progressTimer = nullptr;
	static constexpr int PROGRESS_MERGE_INTERVAL_MS = 300;  // 300ms 汇总一次

	bool m_cancelled = false;
	bool m_initialized = false;
	bool m_finished = false;
};