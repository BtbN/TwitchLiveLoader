#pragma once

#include <QObject>

class TwitchHandler : public QObject
{
	Q_OBJECT

	public:
	TwitchHandler(const QString &stream, const QString &dest, QObject *parent = 0);
};
