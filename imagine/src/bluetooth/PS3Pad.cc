/* -*- mode: c++; tab-width: 4; indent-tabs-mode: true -*-
	
	This file is part of Imagine.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Imagine.  If not, see <http://www.gnu.org/licenses/> */

#define thisModuleName "ps3pad"
#include "PS3Pad.hh"
#include <base/Base.hh>
#include <input/ps3/pad_codes.h>
#include <util/bits.h>
#include <util/collection/DLList.hh>

const uchar PS3Pad::btClass[3] = { 0x04, 0x25, 0x00 };

extern StaticDLList<BluetoothInputDevice*, Input::MAX_BLUETOOTH_DEVS_PER_TYPE * 2> btInputDevList;
StaticDLList<PS3Pad*, Input::MAX_BLUETOOTH_DEVS_PER_TYPE> PS3Pad::devList;

static const Input::PackedInputAccess ps3padDataAccess[] =
{
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_SELECT, Input::Ps3::SELECT },
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_L3, Input::Ps3::L3 },
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_R3, Input::Ps3::R3 },
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_START, Input::Ps3::START },
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_UP, Input::Ps3::UP },
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_RIGHT, Input::Ps3::RIGHT },
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_DOWN, Input::Ps3::DOWN },
	{ CELL_PAD_BTN_OFFSET_DIGITAL1, CELL_PAD_CTRL_LEFT, Input::Ps3::LEFT },

	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_L2, Input::Ps3::L2 },
	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_R2, Input::Ps3::R2 },
	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_L1, Input::Ps3::L1 },
	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_R1, Input::Ps3::R1 },
	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_TRIANGLE, Input::Ps3::TRIANGLE },
	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CIRCLE, Input::Ps3::CIRCLE },
	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_CROSS, Input::Ps3::CROSS },
	{ CELL_PAD_BTN_OFFSET_DIGITAL2, CELL_PAD_CTRL_SQUARE, Input::Ps3::SQUARE },
};

uint PS3Pad::findFreeDevId()
{
	uint id[5] = { 0 };
	forEachInDLList(&devList, e)
	{
		id[e->player] = 1;
	}
	forEachInArray(id, e)
	{
		if(*e == 0)
			return e_i;
	}
	logMsg("too many devices");
	return 0;
}

CallResult PS3Pad::open(BluetoothAdapter &adapter)
{
	logMsg("opening DualShock/Sixaxis");
#if defined CONFIG_BLUEZ && defined CONFIG_ANDROIDBT
	adapter.constructSocket(ctlSock.obj);
	adapter.constructSocket(intSock.obj);
#endif
	ctlSock.onDataDelegate().bind<PS3Pad, &PS3Pad::dataHandler>(this);
	ctlSock.onStatusDelegate().bind<PS3Pad, &PS3Pad::statusHandler>(this);
	intSock.onDataDelegate().bind<PS3Pad, &PS3Pad::dataHandler>(this);
	intSock.onStatusDelegate().bind<PS3Pad, &PS3Pad::statusHandler>(this);
	if(ctlSock.openL2cap(addr, 17) != OK)
	{
		logErr("error opening control socket");
		return IO_ERROR;
	}
	return OK;
}

void PS3Pad::close()
{
	intSock.close();
	ctlSock.close();
}

void PS3Pad::removeFromSystem()
{
	close();
	devList.remove(this);
	if(btInputDevList.remove(this))
	{
		Input::removeDevice((Input::Device){player, Input::Event::MAP_PS3PAD, Input::Device::TYPE_BIT_GAMEPAD, "DualShock/Sixaxis"});
		Input::onInputDevChange((Input::DeviceChange){ player, Input::Event::MAP_PS3PAD, Input::DeviceChange::REMOVED });
	}
}

void PS3Pad::setOperational(void)
{

	const uchar msg[] = {
		0x53 /*HIDP_TRANS_SET_REPORT | HIDP_DATA_RTYPE_FEATURE*/,
		0xf4, 0x42, 0x03, 0x00, 0x00
	};

	ctlSock.write(msg, sizeof(msg));

}

uint PS3Pad::statusHandler(BluetoothSocket &sock, uint status)
{
	if(status == BluetoothSocket::STATUS_OPENED && &sock == (BluetoothSocket*)&ctlSock)
	{
		logMsg("opened DualShock/Sixaxis control socket, opening interrupt socket");
		intSock.openL2cap(addr, 19);
		return 0; // don't add ctlSock to event loop
	}
	else if(status == BluetoothSocket::STATUS_OPENED && &sock == (BluetoothSocket*)&intSock)
	{
		logMsg("DualShock/Sixaxis opened successfully");
		player = findFreeDevId();
		if(!devList.add(this) || !btInputDevList.add(this))
		{
			logErr("No space left in BT input device list");
			removeFromSystem();
			delete this;
			return 0;
		}

		setOperational();
		setLEDs(player);

		Input::addDevice((Input::Device){player, Input::Event::MAP_PS3PAD, Input::Device::TYPE_BIT_GAMEPAD, "DualShock/Sixaxis"});
		device = Input::devList.last();
		Input::onInputDevChange((Input::DeviceChange){ player, Input::Event::MAP_PS3PAD, Input::DeviceChange::ADDED });

		return BluetoothSocket::REPLY_OPENED_USE_READ_EVENTS;
	}
	else if(status == BluetoothSocket::STATUS_ERROR)
	{
		logErr("DualShock/Sixaxis read error, disconnecting");
		removeFromSystem();
		delete this;
	}
	return 1;
}

void PS3Pad::setLEDs(uint player)
{
	/*
	 * the total time the led is active (0xff means forever)
	 * |     duty_length: how long a cycle is in deciseconds (0 means "blink really fast")
	 * |     |     ??? (Some form of phase shift or duty_length multiplier?)
	 * |     |     |     % of duty_length the led is off (0xff means 100%)
	 * |     |     |     |     % of duty_length the led is on (0xff mean 100%)
	 * |     |     |     |     |
	 * 0xff, 0x27, 0x10, 0x00, 0x32,
	 */
	unsigned char ledsReport[] = {
		0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, /* rumble values TBD */
		0x00, 0x00, 0x00, 0x00, 0x1e, /* LED_1 = 0x02, LED_2 = 0x04, ... */
		0xff, 0x27, 0x10, 0x00, 0x32, /* LED_4 */
		0xff, 0x27, 0x10, 0x00, 0x32, /* LED_3 */
		0xff, 0x27, 0x10, 0x00, 0x32, /* LED_2 */
		0xff, 0x27, 0x10, 0x00, 0x32, /* LED_1 */
		0x00, 0x00, 0x00, 0x00, 0x00,
	};

	ledsReport[10] = playerLEDs(player);

	logMsg("setting LEDs for player %d", player);
	intSock.write(ledsReport, sizeof(ledsReport));
}

uchar PS3Pad::playerLEDs(int player)
{
	switch(player)
	{
		default:
		case 0: return BIT(2);
		case 1: return BIT(3);
		case 2: return BIT(4);
		case 3: return BIT(5);
		case 4: return BIT(2) | BIT(5);
		case 5: return BIT(3) | BIT(5);
		case 6: return BIT(4) | BIT(5);
	}
}

bool PS3Pad::dataHandler(const uchar *packet, size_t size)
{
	if (size < 49)
	{
		logWarn("packet size too small");
	}
	else if (packet[0] != 0xa1)
	{
		logWarn("Unknown report header in packet");
	}
	else if (packet[1] != 0x01)
	{
		logWarn("Unknown report ID in packet");
	}
	else
	{
		processBtnReport(&packet[3], player);
	}

	return 1;
}

void PS3Pad::processBtnReport(const uchar *btnData, uint player)
{
	using namespace Input;

	forEachInArray(ps3padDataAccess, e)
	{
		int newState = e->updateState(prevBtnData, btnData);
		if(newState != -1)
		{
			logMsg("%s %s @ ps3pad %d", buttonName(Event::MAP_PS3PAD, e->keyEvent), newState ? "pushed" : "released", player);
			onInputEvent(Event(player, Event::MAP_PS3PAD, e->keyEvent, newState ? PUSHED : RELEASED, 0, device));
		}
	}

	memcpy(prevBtnData, btnData, sizeof(prevBtnData));
	
}
