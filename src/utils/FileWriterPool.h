#pragma once

#include "FileWriter.h"

#include <QObject>
#include <QList>
#include <QThread>

class FileWriterPool : public QObject
{
	Q_OBJECT
public:
	static FileWriterPool* instance();

	void init(int writerCount = 2);
	void shutdown();
	void write(const QString& path, const QByteArray& data);
	void closeFile(const QString& path);
	void closeFileBlocking(const QString& path);   // 阻塞式关闭，确保写入落盘

private:
	explicit FileWriterPool(QObject* parent = nullptr);
	~FileWriterPool() override;

	FileWriter* getWriterForPath(const QString& path);	// 根据文件路径获取对应的 FileWriter

	QList<FileWriter*> m_writers;	// 每个 FileWriter 对应一个文件写入器
	QList<QThread*> m_threads;	// 每个 FileWriter 对应一个线程
	bool m_initialized;
};