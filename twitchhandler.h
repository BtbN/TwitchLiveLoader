#pragma once

#include <QNetworkProxy>
#include <QObject>
#include <QHash>
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class TwitchHandler : public QObject
{
	Q_OBJECT

	public:
	TwitchHandler(const QString &stream, const QString &dest, const QNetworkProxy &proxy = QNetworkProxy(), QObject *parent = 0);

	public slots:
	void reauth();

	private slots:
	void tokenReply(QNetworkReply *reply);
	void usherReply(QNetworkReply *reply);
	void newPlaylist(const QString &url);
	void updateM3u();
	void m3uReply(QNetworkReply *reply);
	void downloadPart(const QString &name);
	void savePart(const QString &path, QNetworkReply *reply);

	private:
	QString stream;
	QString dest;

	QString token;
	QString sig;
	bool haveToken;

	QString m3uUrl;
	bool haveM3u;

	QNetworkAccessManager *nam;
	QNetworkAccessManager *nam_direct;
	QTimer *m3uLoadTimer;

	QHash<QString, bool> currentParts;
};
