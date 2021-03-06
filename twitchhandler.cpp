#include <QtDebug>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <QCoreApplication>
#include <QStringList>
#include <QFileInfo>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QUrl>
#include <QDir>

#include "twitchhandler.h"


TwitchHandler::TwitchHandler(const QString &stream, const QString &dest, const QNetworkProxy &proxy, QObject *parent)
	:QObject(parent)
	,stream(stream)
	,dest(dest)
	,haveToken(false)
	,haveM3u(false)
	,recentlyLoaded(this)
{
	nam = new QNetworkAccessManager(this);

	nam->setProxy(proxy);

	auto del = [](QNetworkReply *rpl)
	{
		rpl->deleteLater();
	};

	connect(nam, &QNetworkAccessManager::finished, del);

	reauth();

	recentlyLoaded.setTimeout(120000);

	QTimer *reauthTimer = new QTimer(this);
	reauthTimer->setInterval(5 * 60 * 1000);
	reauthTimer->setSingleShot(false);
	reauthTimer->start();

	m3uLoadTimer = new QTimer(this);
	m3uLoadTimer->setInterval(1000);
	m3uLoadTimer->setSingleShot(false);

	retryTimer = new QTimer(this);
	retryTimer->setInterval(10000);
	retryTimer->setSingleShot(true);

	connect(reauthTimer, &QTimer::timeout, this, &TwitchHandler::reauth);
	connect(retryTimer, &QTimer::timeout, this, &TwitchHandler::reauth);
	connect(m3uLoadTimer, &QTimer::timeout, this, &TwitchHandler::updateM3u);
}

void TwitchHandler::reauth()
{
	QNetworkRequest req;

	QString url = QString("http://api.twitch.tv/api/channels/%1/access_token").arg(stream);
	req.setUrl(url);

	QNetworkReply *reply = nam->get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply]()
	{
		tokenReply(reply);
	});

	qDebug() << "Reauthing:" << url;
}

void TwitchHandler::tokenReply(QNetworkReply *reply)
{
	if(reply->error() != QNetworkReply::NoError)
	{
		qDebug() << "token get error!";
		retryTimer->start();
		return;
	}

	int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if(code >= 300 && code <= 310)
	{
		qDebug() << "Token redirected to" << reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

		QNetworkRequest req(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString());
		QNetworkReply *reply = nam->get(req);
		connect(reply, &QNetworkReply::finished, this, [this, reply]()
		{
			tokenReply(reply);
		});
		return;
	}

	if(code != 200)
	{
		qDebug() << "Get token failed: Error" << code;
		retryTimer->start();
		return;
	}

	QByteArray data = reply->readAll();

	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(data, &err);

	if(err.error != QJsonParseError::NoError)
	{
		qDebug() << "Json parsing failed:" << err.errorString();
		retryTimer->start();
		return;
	}

	QString newToken = doc.object().value("token").toString();
	QString newSig = doc.object().value("sig").toString();

	if(!newToken.isEmpty() && !newSig.isEmpty())
	{
		token = newToken;
		sig = newSig;
		haveToken = true;
	}
	else
	{
		qDebug() << "New info is empty!";
		return;
	}

	QNetworkRequest req;

	QUrl url = QString("http://usher.twitch.tv/api/channel/hls/%1.m3u8").arg(stream);
	QUrlQuery qry;
	qry.addQueryItem("player", "twitchweb");
	qry.addQueryItem("type", "any");
	qry.addQueryItem("sig", sig);
	qry.addQueryItem("token", token);
	qry.addQueryItem("allow_source", "true");
	url.setQuery(qry);

	req.setUrl(url);

	reply = nam->get(req);

	connect(reply, &QNetworkReply::finished, this, [this, reply]()
	{
		usherReply(reply);
	});

	m3uLoadTimer->stop();

	qDebug() << "Requested usher:" << url.toString();
}

void TwitchHandler::usherReply(QNetworkReply *reply)
{
	if(reply->error() != QNetworkReply::NoError)
	{
		qDebug() << "usher get error!";
		retryTimer->start();
		return;
	}

	int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if(code >= 300 && code <= 310)
	{
		qDebug() << "Usher redirected to" << reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

		QNetworkRequest req(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString());
		QNetworkReply *reply = nam->get(req);
		connect(reply, &QNetworkReply::finished, this, [this, reply]()
		{
			usherReply(reply);
		});
		return;
	}

	if(code != 200)
	{
		qDebug() << "Get usher failed: Error" << code;
		retryTimer->start();
		return;
	}

	QString m3u8 =  QString::fromUtf8(reply->readAll());

	if(m3u8.trimmed() == "[]")
	{
		qDebug() << "m3u8 is empty, channel likely not live, retrying in 10 seconds";
		retryTimer->start();
		return;
	}

	QStringList lines = m3u8.split('\n');

	for(const QString &line: lines)
	{
		QString tline = line.trimmed();

		if(tline.startsWith("http"))
		{
			newPlaylist(tline);
			return;
		}
	}

	qDebug() << "no channel found in m3u8, retrying in 10 seconds";
	retryTimer->start();
}

void TwitchHandler::newPlaylist(const QString &url)
{
	m3uUrl = url;
	haveM3u = true;

	m3uLoadTimer->start();

	updateM3u();
}

void TwitchHandler::updateM3u()
{
	if(!haveM3u)
	{
		qDebug() << "Tried to update m3u without url set!";
		return;
	}

	QNetworkRequest req;
	req.setUrl(m3uUrl);

	QNetworkReply *reply = nam->get(req);

	connect(reply, &QNetworkReply::finished, this, [this, reply]()
	{
		m3uReply(reply);
	});
}

void TwitchHandler::m3uReply(QNetworkReply *reply)
{
	if(reply->error() != QNetworkReply::NoError)
	{
		qDebug() << "m3u get error!";
		m3uLoadTimer->stop();
		retryTimer->start();
		return;
	}

	int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if(code >= 300 && code <= 310)
	{
		qDebug() << "m3u redirected to" << reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

		QNetworkRequest req(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString());
		QNetworkReply *reply = nam->get(req);
		connect(reply, &QNetworkReply::finished, this, [this, reply]()
		{
			m3uReply(reply);
		});
		return;
	}

	if(code != 200)
	{
		qDebug() << "Get m3u failed: Error" << code;
		m3uLoadTimer->stop();
		retryTimer->start();
		return;
	}

	QString pl = QString::fromUtf8(reply->readAll());

	QStringList lines = pl.split('\n');

	for(const QString &line: lines)
	{
		if(line.startsWith('#') || line.isEmpty())
			continue;

		downloadPart(line);
	}
}

#define DIR_LIFETIME (5*60*60)

static QDir getCurrentDestDir(const QDir &topDir)
{
	QDir res = topDir;

	uint time = QDateTime::currentDateTimeUtc().toTime_t();
	time /= DIR_LIFETIME;

	QString timeStr = QString("%1").arg(time);

	res.mkdir(timeStr);
	res.cd(timeStr);

	return res;
}

void TwitchHandler::downloadPart(const QString &name)
{
	QUrl url = m3uUrl;

	QString path = url.path();
	path = path.left(path.lastIndexOf('/') + 1) + name;
	url.setPath(path);
	url.setQuery(QUrlQuery());

	QDir dir;
	dir.mkpath(dest);
	if(!dir.cd(dest))
	{
		qDebug() << "Failed creating destdir" << dest;
		return;
	}

	dir = getCurrentDestDir(dir);

	QFileInfo finfo(dir, QString("%1-%2").arg(QDateTime::currentDateTimeUtc().toTime_t()).arg(name));

	if(currentParts.contains(name) || recentlyLoaded.contains(name))
	{
		return;
	}

	QString targetFile = finfo.absoluteFilePath();

	QNetworkRequest req;
	req.setUrl(url);

	QNetworkReply *reply = nam->get(req);

	connect(reply, &QNetworkReply::finished, this, [this, name, targetFile, reply]
	{
		currentParts.remove(name);
		savePart(name, targetFile, reply);
	});

	currentParts.insert(name);

	qDebug() << "Downloading" << url.toString() << "to" << targetFile;
}

void TwitchHandler::savePart(const QString &name, const QString &path, QNetworkReply *reply)
{
	if(reply->error() != QNetworkReply::NoError)
	{
		qDebug() << "save get error!";
		return;
	}

	int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if(code >= 300 && code <= 310)
	{
		qDebug() << "save redirected to" << reply->attribute(QNetworkRequest::RedirectionTargetAttribute);

		QNetworkRequest req(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString());
		QNetworkReply *reply = nam->get(req);
		connect(reply, &QNetworkReply::finished, this, [this, name, path, reply]()
		{
			savePart(name, path, reply);
		});
		return;
	}

	if(code != 200)
	{
		qDebug() << "Get save failed: Error" << code;
		return;
	}

	QFile file(path);

	if(!file.open(QFile::WriteOnly))
	{
		qDebug() << "Failed opening" << path << "for writing";
		return;
	}

	QByteArray data = reply->readAll();
	qint64 written = file.write(data);
	file.close();

	if(written == data.length())
	{
		qDebug() << "Wrote" << path;
		recentlyLoaded.insert(name);
	}
	else
	{
		qDebug() << "Error writing full data to file!";
		file.remove();
	}
}
