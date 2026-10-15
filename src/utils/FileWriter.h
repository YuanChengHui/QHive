#pragma once

#include <QObject>
#include <QHash>
#include <QFile>
#include <QString>

class FileWriter : public QObject
{
	Q_OBJECT
public:
	explicit FileWriter(int id, QObject* parent = nullptr);
	~FileWriter() override;

public slots:
	void writeChunk(const QString& path, const QByteArray& data);
	void closeFile(const QString& path);
	void closeAll();

signals:
	void writeFinished(const QString& path, qint64 written);
	void writeFailed(const QString& path, const QString& error);

private:
	QFile* getFile(const QString& path);
	int m_id;
	QHash<QString, QFile*> m_files;
};