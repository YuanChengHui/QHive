#include "FileWriterPool.h"
#include <QDebug>

FileWriterPool* FileWriterPool::instance()
{
	static FileWriterPool* inst = new FileWriterPool();
	return inst;
}

FileWriterPool::FileWriterPool(QObject* parent)
	: QObject(parent)
	, m_initialized(false)
{
}

FileWriterPool::~FileWriterPool()
{
	shutdown();
}

void FileWriterPool::init(int writerCount)
{
	if (m_initialized) return;
	if (writerCount < 1) writerCount = 1;

	for (int i = 0; i < writerCount; ++i) {
		QThread* thread = new QThread();
		FileWriter* writer = new FileWriter(i);
		writer->moveToThread(thread);

		connect(thread, &QThread::finished, writer, &QObject::deleteLater);

		thread->start();
		m_threads.append(thread);
		m_writers.append(writer);
	}
	m_initialized = true;
}

void FileWriterPool::shutdown()
{
	if (!m_initialized) return;

	for (int i = 0; i < m_writers.size(); ++i) {
		FileWriter* writer = m_writers.at(i);
		QThread* thread = m_threads.at(i);

		// 请求关闭所有文件
		QMetaObject::invokeMethod(writer, "closeAll", Qt::BlockingQueuedConnection);

		thread->quit();
		thread->wait(3000);
		delete thread;
	}
	m_writers.clear();
	m_threads.clear();
	m_initialized = false;
}

void FileWriterPool::write(const QString& path, const QByteArray& data)
{
	if (!m_initialized) return;

	FileWriter* writer = getWriterForPath(path);
	if (!writer) return;

	// 异步调用写操作
	QMetaObject::invokeMethod(writer, "writeChunk", Qt::QueuedConnection,
		Q_ARG(QString, path),
		Q_ARG(QByteArray, data));
}

void FileWriterPool::closeFile(const QString& path)
{
	if (!m_initialized) return;

	FileWriter* writer = getWriterForPath(path);
	if (!writer) return;

	QMetaObject::invokeMethod(writer, "closeFile", Qt::QueuedConnection,
		Q_ARG(QString, path));
}

void FileWriterPool::closeFileBlocking(const QString& path)
{
	if (!m_initialized) return;

	FileWriter* writer = getWriterForPath(path);
	if (!writer) return;

	// 阻塞等待关闭完成
	QMetaObject::invokeMethod(writer, "closeFile", Qt::BlockingQueuedConnection,
		Q_ARG(QString, path));
}

FileWriter* FileWriterPool::getWriterForPath(const QString& path)
{
	if (m_writers.isEmpty()) return nullptr;
	// 使用路径的哈希值选择固定的 writer，确保同一文件总是由同一个 writer 处理
	uint hash = qHash(path);
	int index = hash % m_writers.size();
	return m_writers.at(index);
}