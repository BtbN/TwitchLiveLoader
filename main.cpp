#include <QCoreApplication>

#include <iostream>

#include "twitchhandler.h"


int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	QStringList args = app.arguments();
	args.removeFirst();

	for(const QString &arg: args)
	{
		qDebug() << arg;

		QStringList parts = arg.split(',', QString::SkipEmptyParts);

		QNetworkProxy proxy = QNetworkProxy();

		if(parts.length() == 5)
		{
			QNetworkProxy::ProxyType type;

			if(parts[2].toLower() == "http")
			{
				type = QNetworkProxy::HttpProxy;
			}
			else if(parts[2].toLower() == "socks")
			{
				type = QNetworkProxy::Socks5Proxy;
			}
			else
			{
				std::cout << "Unknown proxy type" << std::endl;
				return -1;
			}

			proxy = QNetworkProxy(type, parts[3], parts[4].toUShort());
		}
		else if(parts.length() != 2)
		{
			std::cout << "Stream format is either 'channelname,destination' or 'channelname,destination,proxytype,proxyip,proxyport'" << std::endl;
			return -1;
		}

		TwitchHandler *hndlr = new TwitchHandler(parts[0], parts[1], proxy);

		QObject::connect(hndlr, &QObject::destroyed, &app, &QCoreApplication::quit);
	}

	if(args.length() == 0)
	{
		std::cout << "No arguments given!" << std::endl;
		return -1;
	}

	return app.exec();
}
