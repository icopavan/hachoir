#ifndef DEMODULATOR_H
#define DEMODULATOR_H

#include "com_decode.h"
#include "message.h"

class Demodulator
{
public:
	virtual uint8_t likeliness(const burst_sc16_t * const burst) = 0;
	virtual Message demod(const burst_sc16_t * const burst) = 0;
};

#endif // DEMODULATOR_H