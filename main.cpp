#include <QCoreApplication>

#include "twitchhandler.h"

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	TwitchHandler hndl("cirno_tv", "outdir");

	return app.exec();
}
