#include "QHive.h"

#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QHive window;
	window.show();
	return app.exec();
}
