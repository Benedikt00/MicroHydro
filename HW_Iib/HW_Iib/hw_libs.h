#pragma once
#include "iostream"

class OptaAbstraction {
public:
	OptaAbstraction() {

	}
private:
	int setupDevice() {

	}
};

class generalIO {
public:
	int address;
};


class DigitalOutput : generalIO {
public:
	bool state;

	int setState(bool set_to) {
		std::cout << "Setting State ADD" << address << " " << state << std::endl;
		state = set_to;
		if (state) {
			return 1;
		}
		return 0;
	}

	int updateIO() {
		//TODO
		return 0;
	};

};


class DigitalOutput : generalIO {
public:
	bool get_state() {
		bool state = 0;
		//Todo
		return state;
	};

};


