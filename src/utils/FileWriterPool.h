#pragma once

#include <QObject>
#include <QList>
#include <QThread>
#include "FileWriter.h"

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

	FileWriter* getWriterForPath(const QString& path);

	QList<FileWriter*> m_writers;
	QList<QThread*> m_threads;
	bool m_initialized;
};