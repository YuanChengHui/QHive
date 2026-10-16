#include "FileWriter.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

FileWriter::FileWriter(int id, QObject* parent)
	: QObject(parent)
	, m_id(id)
{
}

FileWriter::~FileWriter()
{
	closeAll();
}

void FileWriter::writeChunk(const QString& path, const QByteArray& data)
{
	QFile* file = getFile(path);
	if (!file) {
		qWarning() << "[FileWriter] 无法打开文件:" << path;
		return;
	}

	qint64 written = file->write(data);
	if (written != data.size()) {
		qWarning() << "[FileWriter] 写入文件失败:" << file->errorString();
		return;
	}
}

void FileWriter::closeFile(const QString& path)
{
	auto it = m_files.find(path);
	if (it != m_files.end()) {
		QFile* file = it.value();
		if (file->isOpen()) {
			file->close();
		}
		delete file;
		m_files.erase(it);
	}
}

void FileWriter::closeAll()
{
	for (auto it = m_files.begin(); it != m_files.end(); ++it) {
		QFile* file = it.value();
		if (file->isOpen()) {
			file->close();
		}
		delete file;
	}
	m_files.clear();
}

QFile* FileWriter::getFile(const QString& path)
{
	auto it = m_files.find(path);
	if (it != m_files.end()) {
		QFile* file = it.value();
		if (file->isOpen()) {
			return file;
		}
		else {
			if (file->open(QIODevice::WriteOnly | QIODevice::Append)) {
				return file;
			}
			else {
				return nullptr;
			}
		}
	}

	QFile* file = new QFile(path);
	QFileInfo info(path);
	QDir dir = info.absoluteDir();
	if (!dir.exists()) {
		dir.mkpath(".");
	}

	if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
		delete file;
		return nullptr;
	}
	m_files.insert(path, file);
	return file;
}