#include <QCoreApplication>

#include "twitchhandler.h"

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	QNetworkProxy httpProx(QNetworkProxy::HttpProxy, "192.168.115.1", 8118);

	new TwitchHandler("cirno_tv", "outdir_direct");
	new TwitchHandler("misskaddykins", "outdir_proxy", httpProx);

	return app.exec();
}
