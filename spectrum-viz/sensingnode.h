#ifndef SENSINGNODE_H
#define SENSINGNODE_H

#include <QObject>
#include <QMutex>
#include <QVariant>
#include <QMap>
#include <QTcpSocket>
#include <QSharedPointer>
#include <QAtomicInt>

#include "powerspectrum.h"
#include "absoluteringbuffer.h"
#include "../common/radioeventtable.h"

class SensingNode : public QObject
{
	Q_OBJECT

private:
	QTcpSocket *clientSocket;
	int clientID;

	QMutex _renderingMutex;
	AbsoluteRingBuffer< PowerSpectrum > _ringbuffer;
	RadioEventTable _ret;

	/* qml's view */
	std::vector< std::shared_ptr<PowerSpectrum> > _entries;
	uint64_t _entries_start, _entries_end;

	QAtomicInt updatesPaused;

	/* hold the maximum and the minimum received power */
	char pwr_min;
	char pwr_max;

	QByteArray readExactlyNBytes(QTcpSocket *socket, qint64 n);
	void readPowerSpectrumMessage(const QByteArray &psMsg);
	void readRETMessage(const QByteArray &retMsg);
public:
	explicit SensingNode(QTcpSocket *socket, int clientID, QObject *parent = 0);

	Q_INVOKABLE void pauseUpdates(bool pause);
	Q_INVOKABLE bool areUpdatesPaused();

	Q_INVOKABLE bool fetchEntries(qreal start, qreal end);
	Q_INVOKABLE QMap<QString, QVariant> getEntriesRange();

	Q_INVOKABLE QObject * selectEntry(qreal pos);

	Q_INVOKABLE void freeEntries(); /* unreference the entries */

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
