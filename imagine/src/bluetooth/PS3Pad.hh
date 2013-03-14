/* -*- mode: c++; tab-width: 4; indent-tabs-mode: true -*- */

#pragma once

#include <bluetooth/sys.hh>
#include <input/Input.hh>
#include <util/collection/DLList.hh>

struct PS3Pad : public BluetoothInputDevice
{
public:
	PS3Pad(BluetoothAddr addr): addr(addr) { }

	CallResult open(BluetoothAdapter &adapter) override;
	void close();
	void removeFromSystem() override;

	uint statusHandler(BluetoothSocket &sock, uint status);
	bool dataHandler(const uchar *packet, size_t size);

	void setLEDs(uint player);

	static const uchar btClass[3];

	static bool isSupportedClass(const uchar devClass[3])
	{
		return mem_equal(devClass, btClass, 3);
	}

	static StaticDLList<PS3Pad*, Input::MAX_BLUETOOTH_DEVS_PER_TYPE> devList;

private:
	BluetoothSocketSys ctlSock, intSock;
	Input::Device *device = nullptr;
	uchar prevBtnData[3] = {0};
	uint player = 0;

	BluetoothAddr addr;

	static uint findFreeDevId();
	void processBtnReport(const uchar *btnData, uint player);
	void setOperational(void);
	static uchar playerLEDs(int player);
};