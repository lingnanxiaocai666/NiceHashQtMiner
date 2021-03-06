#ifndef MINERS_CLAYMOREDUAL_H_
#define MINERS_CLAYMOREDUAL_H_

#include "Miners/ClaymoreBaseMiner.h"


class ClaymoreDual
	: public ClaymoreBaseMiner
{
//	Q_OBJECT
public:
	ClaymoreDual(Enums::AlgorithmType secondaryAlgorithmType);
	QString SecondaryShortName();

	void Start(QString url, QString btcAdress, QString worker) override;

protected:
	QStringList DeviceCommand(int amdCount=-1) override;
	QStringList BenchmarkCreateCommandLine(Algorithm* algorithm, int time) override;

private:
	QStringList GetStartCommand(QString url, QString btcAdress, QString worker);
};

#endif
