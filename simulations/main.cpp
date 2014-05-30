#include <iostream>

#include "cognode.h"
#include "random.h"
#include <unistd.h>
#include <time.h>

uint64_t scenario1_run(uint64_t hoppingPeriod, uint64_t beaconningPeriod)
{
	float band1 = 325e6 + (rand_cmwc() % 2650) * 1e6;
	float band2 = 325e6 + (rand_cmwc() % 2650) * 1e6;

	/*std::cout << "Band1: " << band1 / 1e6;
	std::cout << ", Band2: " << band2 / 1e6 << std::endl;*/

	HoppingPattern hp1(1000e3, Band(300e6, 3e9), 25e6, HoppingPattern::LINEAR, 1e3);
	hp1.addAvailableEntry(Band(band1, band1+25e6), 0.0, 0.4);
	hp1.addAvailableEntry(Band(band2, band2+25e6), 0.5, 0.4);
	CogNode n1(42, 0, 0, beaconningPeriod, hp1);

	HoppingPattern hp2(60e6, Band(300e6, 3e9), 25e6, HoppingPattern::RANDOM, hoppingPeriod);
	CogNode n2(43, 0, 0, 10e3, hp2);

	CogNode *nodes[] = { &n1, &n2 };
	size_t nodesCount = sizeof(nodes) / sizeof(CogNode *);

	while (true) {
		uint64_t next = -1;

		for (size_t i = 0; i < nodesCount; i++) {
			uint64_t nodeNext = nodes[i]->usUntilNextOperation();
			if (nodeNext < next)
				next = nodeNext;
		}

		//std::cout << "next = " << next << std::endl;

		// FIXME? If a node jumps to a frequency, it won't be able to
		// receive a message until the next tick change. That's pretty
		// much what we get in real life though :s
		for (size_t i = 0; i < nodesCount; i++) {
			nodes[i]->addTicks(next);
			if (n2.hasNeighbour(42)) {
				return n2.ticksSinceStart();
			}
		}
	}

	//std::cout << std::endl;
}

int main(int argc, char **argv)
{
	init_rand(time(NULL));

	uint64_t sum = 0;

	for (size_t i = 0; i < 1000; i++) {
		uint64_t res = scenario1_run(10e3, 100e3);
		sum += res;
	}
	std::cout << "Node has been found in " << sum / 1000 / 1000.0 / 1000.0 << "s" << std::endl;

	return 0;
}

