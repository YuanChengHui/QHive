#pragma once

#include "DownloadWorker.h"

#include <QObject>
#include <QUrl>
#include <QMap>
#include <QFile>
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
	void pause();
	void resume();
	void cancel();
	void retry();

	// 恢复状态
	void restorePausedState();
	void restoreFailedState();
	void restoreDownloadingState();

private:
	void calculateRanges();
	void startWorkers();
	void checkAllChunkStatus();
	void mergeChunks();
	void stopAllThreads();
	void cleanupTempFiles();
	void updateTotalProgress();
	void updateStateToDatabase(int state);
	void restoreChunksFromJson(const QString& json);
	int getRetryDelay(int retryCount) const;

signals:
	void paused(const QString& taskId);
	void resumed(const QString& taskId);
	void downloadProgressUpdate(const QString& taskId, qint64 received);
	void downloadEnded(const QString& taskId, bool errorOccurred, const QString& errorString, const QString& savePath);

private slots:
	void assignChunk();// 分配下载任务给空闲的线程
	void retryChunk(int chunkId);
	void onWorkUpdated(int chunkId, qint64 bytesReceived);
	void onWorkPaused(int chunkId, bool isWorking);
	void onWorkEnded(int chunkId, bool success, const QString& error);


private:
	struct ChunkInfo {
		int id = 0;
		qint64 startByte = 0;
		qint64 endByte = 0;
		QString tempFilePath;
		QString errorString;
		bool isEnded = false;
		bool isSuccessful = false;
		int retryCount = 0;
	};

	QUrl m_url;
	QString m_taskId;
	QString m_fullSavePath; // 最终合并后的文件路径
	QString m_taskCacheDir;	// 用于存储分块下载的临时文件目录
	qint64 m_totalSize;
	int m_threadCount;

	bool m_isPaused = false;
	bool m_isFailed = false;
	bool m_isDownloadEnded = false;

	QMap<int, ChunkInfo> m_chunks;
	QQueue<int> m_pendingChunks;
	QQueue<int> m_failedChunks;
	QQueue<int> m_successfulChunks;
	QMap<int, qint64> m_chunkDownloaded;	// chunkId -> 已下载字节数

	QList<QThread*> m_threads;
	QList<DownloadWorker*> m_workers;
	QMap<DownloadWorker*, bool> m_workerStatus;		// true: 正在工作，false: 空闲
	int m_activeWorkers = 0;

	QTimer* m_progressTimer = nullptr;
	QTimer* m_dbSaveTimer = nullptr;
	static constexpr int PROGRESS_MERGE_INTERVAL_MS = 300;
	static constexpr int DB_SAVE_INTERVAL_MS = 3000;
	static constexpr int MAX_RETRY_COUNT = 3;
};
