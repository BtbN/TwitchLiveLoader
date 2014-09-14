#pragma once

#include <QtDebug>

#include <QObject>
#include <QTimer>
#include <QHash>

#include <utility>
#include <memory>

template<typename T>
class TimedSet
{
	public:
	TimedSet(QObject *parent, int timeoutMs = 60000)
		:parent(parent)
		,timeout(timeoutMs)
	{
	}

	void setTimeout(int timeoutMs)
	{
		timeout = timeoutMs;
	}

	int getTimeout()
	{
		return timeout;
	}

	bool contains(const T &value)
	{
		return set.contains(value);
	}

	void insert(const T &value)
	{
		insert(value, timeout);
	}

	void insert(const T &value, int timeoutMs)
	{
		remove(value);

		std::shared_ptr<QTimer> timer = std::make_shared<QTimer>();

		timer->setSingleShot(true);
		timer->setInterval(timeoutMs);

		QObject::connect(timer.get(), &QTimer::timeout, parent, [this, value]()
		{
			remove(value);
			qDebug() << "Timed out value" << value;
		});

		timer->start();

		set[value] = std::move(timer);
	}

	bool remove(const T &value)
	{
		return set.remove(value) > 0;
	}

	private:
	QObject *parent;
	int timeout;
	QHash<T, std::shared_ptr<QTimer>> set;
};
