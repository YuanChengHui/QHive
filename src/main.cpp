#include "QHive.h"
#include "DatabaseManager.h"

#include <QtWidgets/QApplication>
#include <QMessageBox>

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

    // 初始化数据库
    if (!DatabaseManager::instance()->init()) {
		QMessageBox::critical(nullptr, "错误", "初始化数据库失败！请检查日志获取更多信息。");
        return -1;
    }
	QHive window;
	window.show();
	return app.exec();
}