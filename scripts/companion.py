#!/usr/bin/env python3
#
# Copyright (c) 2026 Hubble Network, Inc.
#
# SPDX-License-Identifier: Apache-2.0

import asyncio
import os
import signal
import sys
import time

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData


EPHEMERIS_DATA = []


SMARTPIN_SERVICE_UUID = "ef2dc9a1-40be-44b6-9dda-8a00fcd61dc0"
SMARTPIN_CHARACTERISTIC_UUID = "ef2dc9a1-40be-44b6-9dda-8a00fcd61dc1"
SPBLE_PKT_CMD = 0x02
SPBLE_CMD_SETUTC = 0x02
SPBLE_CMD_SETEPHEMERIS = 0x07
SPBLE_CMD_RESETEPHEMERIS = 0x08

def generate_utc_timestamps(start_time, interval, duration, count):
    global EPHEMERIS_DATA

    # current_time = start_time + duration
    current_time = start_time

    for _ in range(count):
        utc_timestamp = int(current_time)  # Convert to milliseconds
        EPHEMERIS_DATA.append([utc_timestamp, duration])
        current_time += interval

async def scan(stop_event: asyncio.Event):
    def match_tilepro_uuid(device: BLEDevice, adv: AdvertisementData):
        for uuid in adv.service_uuids:
            if uuid.startswith("0000fca7"):
                return True

        return False

    def handle_disconnect(_: BleakClient):
        print("Device disconnected, exiting ...")
        for task in asyncio.all_tasks():
            task.cancel()

    device = await BleakScanner.find_device_by_filter(match_tilepro_uuid)
    if device is not None:
        print("Found device")
        async with BleakClient(device) as client:
            service = client.services.get_service(SMARTPIN_SERVICE_UUID)
            utc_char = service.get_characteristic(SMARTPIN_CHARACTERISTIC_UUID)

            # Provision ephemeris first
            total = len(EPHEMERIS_DATA)
            pos = 0

            while (total > 0):
                if stop_event.is_set():
                    print("Stopping scan ...")
                    return

                batch_size = min(total, 20)
                data = bytearray([SPBLE_PKT_CMD, SPBLE_CMD_SETEPHEMERIS])
                data += bytearray(batch_size.to_bytes(4, byteorder="little", signed=False))

                for epoch, value in EPHEMERIS_DATA[pos:pos + batch_size]:
                    data += bytearray(epoch.to_bytes(4, byteorder="little", signed=False))
                    data += bytearray(value.to_bytes(2, byteorder="little", signed=False))
                
                pos += batch_size
                total -= batch_size
                
                await client.write_gatt_char(SMARTPIN_CHARACTERISTIC_UUID, data, response=True)

            # Provision UTC time now
            data = bytearray([SPBLE_PKT_CMD, SPBLE_CMD_SETUTC])
            data +=  bytearray(int(time.time() * 1000).to_bytes(8, byteorder="little", signed=False))

            await client.write_gatt_char(SMARTPIN_CHARACTERISTIC_UUID, data, response=True)

    else:
        print("No device")

def main() -> None:
    stop_event = asyncio.Event()
    signal.signal(signal.SIGINT, lambda _, __: stop_event.set())

    generate_utc_timestamps(int(time.time()) + 20, 100, 60, 3)

    asyncio.run(scan(stop_event))


if __name__ == '__main__':
    main()
