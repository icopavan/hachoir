#ifndef SENSINGNODE_H
#define SENSINGNODE_H

#include <QObject>
#include <QMutex>
#include <QVariant>
#include <QMap>
#include <QTcpSocket>
#include <QReadWriteLock>
#include <QSharedPointer>

#include "spectrumsample.h"

class SensingNode : public QObject
{
	Q_OBJECT

private:
	QTcpSocket *clientSocket;
	int clientID;

	QReadWriteLock swapMutex;
	QMutex backBufferMutex;

	QSharedPointer< QVector<SpectrumSample> > frontBuffer, backBuffer, inputBuffer;

	/* hold the maximum and the minimum received power */
	char pwr_min;
	char pwr_max;

	QByteArray readExactlyNBytes(QTcpSocket *socket, qint64 n);
public:
	explicit SensingNode(QTcpSocket *socket, int clientID, QObject *parent = 0);

	/* returns the number of samples */
	Q_INVOKABLE int startReading();
	Q_INVOKABLE QMap<QString, QVariant> getSample(int i);
	Q_INVOKABLE void stopReading();

signals:
	void dataChanged();
	void powerRangeChanged(qreal high, qreal low);
	void frequencyRangeChanged(qreal low, qreal high);
	void timeChanged(qreal timeNs);
	void requestDestroy(int clientID);

private slots:
	void clientDisconnected();
	void dataReady();
};

#endif // SENSINGNODE_H