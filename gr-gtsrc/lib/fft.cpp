#include "fft.h"

#include <iostream>

void Fft::doFFt(uint16_t fftSize, FftWindow &win, gr::fft::fft_complex *fft)
{
	int i;

	fft->execute();

	/* process the output */
	gr_complex *out = fft->get_outbuf();
	float logFftSize = log10f(fftSize);
	float logWindowPower = log10f(win.windowPower()/fftSize);
	for (i = 0; i < fftSize; i++) {
		int n = (i + fftSize/2 ) % fftSize;

		float real = out[n].real();
		float imag = out[n].imag();
		float mag = sqrtf(real*real + imag*imag);

		_pwr[i] = 20 * log10f(fabsf(mag))
			- 20 * logFftSize
			- 10 * logWindowPower
			+ 3;
	}
}

void Fft::FftFromRing(uint16_t fftSize, uint64_t centralFrequency, uint64_t sampleRate,
    gr::fft::fft_complex *fft, FftWindow &win, SamplesRingBuffer &ringBuffer,
    uint64_t &fromPos)
{
	gr_complex *dst = fft->get_inbuf();
	gr_complex *samples;

	/* wait until we have enough samples */
	while (!ringBuffer.hasNAvailableFrom(fromPos, fftSize))
		boost::this_thread::sleep_for(boost::chrono::microseconds(100));

	uint64_t restartPos;
	size_t length = fftSize;
	if (fromPos != (uint64_t)-1) {
		if (!ringBuffer.requestRead(fromPos, &length, &samples)) {
			fprintf(stderr, "Fft::FftFromRing: We lost samples!\n");
			length = ringBuffer.requestReadLastN(fftSize, &fromPos, &restartPos, &samples);
		} else
			restartPos += length;
	} else
		length = ringBuffer.requestReadLastN(fftSize, &fromPos, &restartPos, &samples);
	_ringBufferStartPos = fromPos;

	_time_ns = ringBuffer.timeAt(fromPos);

	size_t currentPos = 0;
	do {
		size_t i;

		/* apply the window and copy to the input buffer */
		for (i = 0; i < length; i++) {
			dst[i] = samples[i] * win[i];
		}
		currentPos += length;

		if (length == 0)
			boost::this_thread::yield();

		if (currentPos < fftSize) {
			size_t len = fftSize - currentPos;
			ringBuffer.requestRead(restartPos, &len, &samples);
		}
	} while(currentPos < fftSize);

	/* check that the data has not been overriden while we were reading it! */
	if (!ringBuffer.isPositionValid(fromPos)) {
		fprintf(stderr, "Fft::FftFromRing: Invalid FFT, we potentially lost samples!\n");
		return;
	}

	doFFt(fftSize, win, fft);
}

Fft::Fft(uint16_t fftSize, uint64_t centralFrequency, uint64_t sampleRate) :
	_fft_size(fftSize), _central_frequency(centralFrequency),
	_sample_rate(sampleRate), _ringBufferStartPos(0), _pwr(fftSize)
{

}

Fft::Fft(uint16_t fftSize, uint64_t centralFrequency, uint64_t sampleRate,
	 gr::fft::fft_complex *fft, FftWindow &win, const gr_complex *src, size_t length,
	 uint64_t time_ns) : _fft_size(fftSize),
	_central_frequency(centralFrequency), _sample_rate(sampleRate),
	_time_ns(time_ns), _ringBufferStartPos(0), _pwr(fftSize)
{
	size_t i;

	gr_complex *dst = fft->get_inbuf();

	/* apply the window and copy to input buffer */
	for (i = 0; i < length && i < fftSize; i++) {
		dst[i] = src[i] * win[i];
	}

	/* add padding samples when length < fft_size */
	for (i = i; i < fftSize; i++)
		dst[i] = 0;

	doFFt(fftSize, win, fft);
}

Fft::Fft(uint16_t fftSize, uint64_t centralFrequency, uint64_t sampleRate,
    gr::fft::fft_complex *fft, FftWindow &win, SamplesRingBuffer &ringBuffer)
	: _fft_size(fftSize), _central_frequency(centralFrequency),
	  _sample_rate(sampleRate), _pwr(fftSize)
{
	uint64_t fromPos = (uint64_t)-1;
	FftFromRing(fftSize, centralFrequency, sampleRate, fft, win, ringBuffer, fromPos);
}

Fft::Fft(uint16_t fftSize, uint64_t centralFrequency, uint64_t sampleRate,
    gr::fft::fft_complex *fft, FftWindow &win, SamplesRingBuffer &ringBuffer,
    uint64_t &fromPos)
	: _fft_size(fftSize), _central_frequency(centralFrequency),
	  _sample_rate(sampleRate), _pwr(fftSize)
{
	FftFromRing(fftSize, centralFrequency, sampleRate, fft, win, ringBuffer, fromPos);
}

float Fft::noiseFloor() const
{
	std::vector<float> sortedPwr(_pwr.size());
	size_t keptPercentage = 10;

	std::copy(_pwr.begin(), _pwr.end(), sortedPwr.begin());
	std::sort(sortedPwr.begin(), sortedPwr.end());

	size_t keptCount = fftSize() * keptPercentage / 100;
	float keptSum = 0;
	std::vector<float>::iterator rit;
	for (rit = sortedPwr.begin(); rit != sortedPwr.begin() + keptCount; ++rit)
		keptSum += *rit;

	return keptSum / keptCount;
}

Fft & Fft::operator+=(const Fft &rhs)
{
	/* check that we are adding an fft of the same type */
	if (rhs.fftSize() != fftSize() ||
	    rhs.centralFrequency() != centralFrequency() ||
	    rhs.sampleRate() != sampleRate()) {
		std::cerr << "FftAverage::operator+: Tried to add an fft with different characteristics";
		std::cerr << std::endl;
		return *this;
	}

	for (int i = 0; i < fftSize(); i++)
		_pwr[i] += rhs.operator [](i);

	return *this;
}

Fft & Fft::operator-=(const Fft &rhs)
{
	std::cerr << "operator-=(): start" << std::endl;

	/* check that we are adding an fft of the same type */
	if (rhs.fftSize() != fftSize() ||
	    rhs.centralFrequency() != centralFrequency() ||
	    rhs.sampleRate() != sampleRate()) {
		std::cerr << "FftAverage::operator+: Tried to add an fft with different characteristics";
		std::cerr << std::endl;
		return *this;
	}

	std::cerr << "operator-=(): 2" << std::endl;

	for (int i = 0; i < fftSize(); i++)
		_pwr[i] -= rhs.operator [](i);

	std::cerr << "operator-=(): end" << std::endl;
	return *this;
}

const Fft Fft::operator+(const Fft &other) const
{
	return Fft(*this) += other;
}

const Fft Fft::operator-(const Fft &other) const
{
	return Fft(*this) -= other;
}
