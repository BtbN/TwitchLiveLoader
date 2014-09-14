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
{
	nam = new QNetworkAccessManager(this);
	nam_direct = new QNetworkAccessManager(this);

	nam->setProxy(proxy);

	auto del = [](QNetworkReply *rpl)
	{
		rpl->deleteLater();
	};

	connect(nam, &QNetworkAccessManager::finished, del);
	connect(nam_direct, &QNetworkAccessManager::finished, del);

	reauth();

	QTimer *reauthTimer = new QTimer(this);
	reauthTimer->setInterval(5 * 60 * 1000);
	reauthTimer->setSingleShot(false);
	reauthTimer->start();

	m3uLoadTimer = new QTimer(this);
	m3uLoadTimer->setInterval(1000);
	m3uLoadTimer->setSingleShot(false);

	connect(reauthTimer, &QTimer::timeout, this, &TwitchHandler::reauth);
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
		QTimer::singleShot(2000, this, SLOT(reauth()));
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
		QTimer::singleShot(2000, this, SLOT(reauth()));
		return;
	}

	QByteArray data = reply->readAll();

	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(data, &err);

	if(err.error != QJsonParseError::NoError)
	{
		qDebug() << "Json parsing failed:" << err.errorString();
		QTimer::singleShot(2000, this, SLOT(reauth()));
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

	QUrl url = QString("http://usher.twitch.tv/select/%1.json").arg(stream);
	QUrlQuery qry;
	qry.addQueryItem("nauthsig", sig);
	qry.addQueryItem("nauth", token);
	qry.addQueryItem("allow_source", "true");
	url.setQuery(qry);

	req.setUrl(url);

	reply = nam->get(req);

	connect(reply, &QNetworkReply::finished, this, [this, reply]()
	{
		usherReply(reply);
	});

	qDebug() << "Requested usher:" << url;
}

void TwitchHandler::usherReply(QNetworkReply *reply)
{
	if(reply->error() != QNetworkReply::NoError)
	{
		qDebug() << "usher get error!";
		QTimer::singleShot(2000, this, SLOT(reauth()));
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
		QTimer::singleShot(2000, this, SLOT(reauth()));
		return;
	}

	QString m3u8 =  QString::fromUtf8(reply->readAll());

	if(m3u8.trimmed() == "[]")
	{
		qDebug() << "m3u8 is empty!";
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

	QFileInfo finfo(dir, name);
	if(finfo.exists())
	{
		return;
	}

	if(currentParts.contains(name))
	{
		return;
	}

	QString targetFile = finfo.absoluteFilePath();

	QNetworkRequest req;
	req.setUrl(url);

	QNetworkReply *reply = nam_direct->get(req);

	connect(reply, &QNetworkReply::finished, this, [this, name, targetFile, reply]
	{
		currentParts.remove(name);
		savePart(targetFile, reply);
	});

	currentParts[name] = true;
}

void TwitchHandler::savePart(const QString &path, QNetworkReply *reply)
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
		QNetworkReply *reply = nam_direct->get(req);
		connect(reply, &QNetworkReply::finished, this, [this, path, reply]()
		{
			savePart(path, reply);
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

	file.write(reply->readAll());

	file.close();

	qDebug() << "Wrote" << path;
}
