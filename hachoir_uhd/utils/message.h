#ifndef MESSAGE_H
#define MESSAGE_H

#include <boost/dynamic_bitset.hpp>
#include <initializer_list>
#include <stdint.h>
#include <ostream>
#include <memory>

class Modulation;

class Message
{
	boost::dynamic_bitset<> data;

	std::shared_ptr<Modulation> _modulation;

	std::string toStringBinary() const;
	std::string toStringHex() const;

public:
	enum MessagePrintStyle {
		BINARY = 0,
		HEX = 1
	};

	Message();
	Message(std::initializer_list<uint8_t> bytes);

	void addBit(bool b);
	void addByte(uint8_t byte);
	void addBytes(std::initializer_list<uint8_t> bytes);
	void addBytes(const uint8_t *bytes, size_t len);
	size_t size() const;
	void clear();

	std::shared_ptr<Modulation> modulation() const;
	void setModulation(std::shared_ptr<Modulation> mod);

	std::string toString(MessagePrintStyle style) const;
	void print(std::ostream &stream, MessagePrintStyle style) const;

	Message &operator<< (bool bit);
	Message &operator<< (uint8_t byte);
	Message &operator<< (std::initializer_list<uint8_t> bytes);
	bool operator[](size_t i) const;
};

#endif // MESSAGE_H