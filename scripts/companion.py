#!/usr/bin/env python3
#
# Copyright (c) 2026 Hubble Network, Inc.
#
# SPDX-License-Identifier: Apache-2.0

"""Provision UTC and real satellite pass windows onto a Thingy:53 reference device.

Fetches orbital parameters from the Hubble satellite ephemeris API, predicts
overhead passes for a point location or rectangular region (SDK algorithms),
and uploads those windows over the SmartPin BLE characteristic.

Environment:
    HUBBLE_API_TOKEN            - Required Bearer token for api.hubble.com
    HUBBLE_TARGET_SATELLITE_IDS - Optional comma-separated satellite IDs
"""

from __future__ import annotations

import argparse
import asyncio
import os
import signal
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import requests
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData

# Allow `python scripts/companion.py` without installing the package.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from pass_prediction import (  # noqa: E402
    DEFAULT_TRANSMISSION_PERIOD_S,
    DeviceRegion,
    OrbitalParams,
    predict_passes,
    predict_region_passes,
)

HUBBLE_EPHEMERIS_URL = "https://api.hubble.com/api/satellite/ephemeris"

SMARTPIN_SERVICE_UUID = "ef2dc9a1-40be-44b6-9dda-8a00fcd61dc0"
SMARTPIN_CHARACTERISTIC_UUID = "ef2dc9a1-40be-44b6-9dda-8a00fcd61dc1"
SPBLE_PKT_CMD = 0x02
SPBLE_CMD_SETUTC = 0x02
SPBLE_CMD_SETEPHEMERIS = 0x07
SPBLE_CMD_RESETEPHEMERIS = 0x08

# Firmware stores at most 300 satellite pass windows.
MAX_EPHEMERIS_ENTRIES = 300


def fetch_orbital_params(api_token: str) -> list[OrbitalParams]:
    target_ids_str = os.environ.get("HUBBLE_TARGET_SATELLITE_IDS", "").strip()
    target_ids = (
        {int(x) for x in target_ids_str.split(",") if x.strip()} if target_ids_str else set()
    )

    resp = requests.get(
        HUBBLE_EPHEMERIS_URL,
        headers={"Authorization": f"Bearer {api_token}"},
        timeout=30,
    )
    resp.raise_for_status()
    data = resp.json()

    results: list[OrbitalParams] = []
    for sat in data.get("satellites", []):
        sat_id = sat.get("satellite_id")
        if target_ids and sat_id not in target_ids:
            continue
        orb = sat.get("orbital_params")
        if not orb:
            print(f"Skipping satellite {sat_id}: no orbital_params")
            continue
        results.append(
            OrbitalParams(
                t0=int(orb["t0"]),
                n0=float(orb["n0"]),
                ndot=float(orb["ndot"]),
                raan0=float(orb["raan0"]),
                raandot=float(orb["raandot"]),
                aop0=float(orb["aop0"]),
                aopdot=float(orb["aopdot"]),
                inclination=float(orb["inclination"]),
                eccentricity=float(orb["eccentricity"]),
                satellite_id=int(sat_id),
            )
        )

    if not results:
        raise RuntimeError("No orbital parameters returned by ephemeris API")
    print(f"Fetched orbital params for {len(results)} satellite(s)")
    return results


def resolve_location(lat: float | None, lon: float | None) -> tuple[float, float]:
    if lat is not None and lon is not None:
        return lat, lon

    # IP geolocation fallbacks (no extra dependency). Prefer --location for accuracy.
    errors: list[str] = []
    providers = (
        ("https://ipapi.co/json/", ("latitude", "longitude")),
        ("http://ip-api.com/json/", ("lat", "lon")),
    )
    for url, (lat_key, lon_key) in providers:
        try:
            resp = requests.get(url, timeout=15)
            resp.raise_for_status()
            data = resp.json()
            if data.get("error") or data.get("status") == "fail":
                errors.append(f"{url}: {data.get('reason') or data.get('message') or data}")
                continue
            if data.get(lat_key) is None or data.get(lon_key) is None:
                errors.append(f"{url}: missing coordinates")
                continue
            return float(data[lat_key]), float(data[lon_key])
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{url}: {exc}")

    raise RuntimeError(
        "IP geolocation failed; pass --location LAT LON. Details: " + "; ".join(errors)
    )


def _windows_from_passes(
    passes: list,
    now: int,
    summary: str,
) -> list[list[int]]:
    windows: list[list[int]] = []
    for p in passes:
        # uint16 duration field on device; clamp defensively.
        dur = min(max(int(p.duration), 1), 65535)
        start = int(p.start)
        if start + dur <= now:
            continue
        # If the pass already started, open the window immediately.
        if start < now:
            remaining = start + dur - now
            if remaining <= 0:
                continue
            start = now + 2
            dur = remaining
        windows.append([start, dur])

    if not windows:
        raise RuntimeError("All predicted passes are already in the past")

    first = windows[0][0]
    last = windows[-1][0]
    last_end = last + windows[-1][1]
    print(
        f"Predicted {len(windows)} pass window(s) {summary}, "
        f"span≈{(last_end - first) / 86400:.1f} days"
    )
    for i, (start, dur) in enumerate(windows[:8]):
        when = datetime.fromtimestamp(start, tz=timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        )
        print(f"  [{i}] start={when} duration={dur}s")
    if len(windows) > 8:
        print(f"  ... and {len(windows) - 8} more")
    return windows


def build_pass_windows(
    satellites: list[OrbitalParams],
    lat: float,
    lon: float,
    count: int,
    duration_s: int,
) -> list[list[int]]:
    now = int(time.time())
    passes = predict_passes(
        satellites,
        lat=lat,
        lon=lon,
        start_time=now,
        count=count,
        transmission_period_s=duration_s,
    )
    if not passes:
        raise RuntimeError(
            f"No satellite passes predicted for lat={lat}, lon={lon}. "
            "Check location and that orbital params are current."
        )
    return _windows_from_passes(
        passes,
        now,
        summary=(
            f"for point lat={lat:.4f}, lon={lon:.4f}, "
            f"tx_period={duration_s}s"
        ),
    )


def build_region_pass_windows(
    satellites: list[OrbitalParams],
    region: DeviceRegion,
    count: int,
    duration_s: int,
) -> list[list[int]]:
    now = int(time.time())
    passes = predict_region_passes(
        satellites,
        region=region,
        start_time=now,
        count=count,
        transmission_period_s=duration_s,
    )
    if not passes:
        raise RuntimeError(
            "No satellite passes predicted for region "
            f"lat_mid={region.lat_mid}, lon_mid={region.lon_mid}, "
            f"lat_range={region.lat_range}, lon_range={region.lon_range}. "
            "Check region and that orbital params are current."
        )
    lat_min = region.lat_mid - region.lat_range / 2
    lat_max = region.lat_mid + region.lat_range / 2
    lon_min = region.lon_mid - region.lon_range / 2
    lon_max = region.lon_mid + region.lon_range / 2
    return _windows_from_passes(
        passes,
        now,
        summary=(
            f"for region lat=[{lat_min:.4f},{lat_max:.4f}] "
            f"lon=[{lon_min:.4f},{lon_max:.4f}], "
            f"tx_period={duration_s}s (added to transit)"
        ),
    )


async def provision(windows: list[list[int]], stop_event: asyncio.Event) -> None:
    def match_hubble_uuid(device: BLEDevice, adv: AdvertisementData):
        for uuid in adv.service_uuids:
            if uuid.startswith("0000fca7"):
                return True
        return False

    def handle_disconnect(_: BleakClient):
        print("Device disconnected, exiting ...")
        for task in asyncio.all_tasks():
            task.cancel()

    print("Scanning for Hubble device...")
    device = await BleakScanner.find_device_by_filter(match_hubble_uuid, timeout=20.0)
    if device is None:
        print("No device")
        return

    print(f"Found device: {device} name={device.name!r}")
    async with BleakClient(device, timeout=60.0, disconnected_callback=handle_disconnect) as client:
        if SMARTPIN_SERVICE_UUID.lower() not in {
            str(s.uuid).lower() for s in client.services
        }:
            print("Connected device missing SmartPin service; aborting")
            return

        # Clear any previous schedule before uploading a new one.
        # Requires firmware with SPBLE_CMD_RESETEPHEMERIS / sat_pass_clear().
        reset = bytearray([SPBLE_PKT_CMD, SPBLE_CMD_RESETEPHEMERIS])
        await client.write_gatt_char(SMARTPIN_CHARACTERISTIC_UUID, reset, response=True)
        print("Cleared previous ephemeris table")

        total = len(windows)
        pos = 0
        max_batch = 3

        while total > 0:
            if stop_event.is_set():
                print("Stopping ...")
                return

            batch_size = min(total, max_batch)
            data = bytearray([SPBLE_PKT_CMD, SPBLE_CMD_SETEPHEMERIS])
            data += batch_size.to_bytes(4, byteorder="little", signed=False)

            for epoch, value in windows[pos : pos + batch_size]:
                data += int(epoch).to_bytes(4, byteorder="little", signed=False)
                data += int(value).to_bytes(2, byteorder="little", signed=False)

            pos += batch_size
            total -= batch_size

            await client.write_gatt_char(
                SMARTPIN_CHARACTERISTIC_UUID, data, response=True
            )
            if pos == batch_size or pos % 30 == 0 or total == 0:
                print(f"Uploaded ephemeris {pos}/{len(windows)}")

        utc = bytearray([SPBLE_PKT_CMD, SPBLE_CMD_SETUTC])
        utc += int(time.time() * 1000).to_bytes(8, byteorder="little", signed=False)
        await client.write_gatt_char(SMARTPIN_CHARACTERISTIC_UUID, utc, response=True)
        print("UTC synchronized")
        await asyncio.sleep(1.0)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    loc = parser.add_mutually_exclusive_group()
    loc.add_argument(
        "--location",
        nargs=2,
        type=float,
        metavar=("LAT", "LON"),
        help="Point location in degrees (default: IP geolocation)",
    )
    loc.add_argument(
        "--region",
        nargs=4,
        type=float,
        metavar=("LAT_MID", "LON_MID", "LAT_RANGE", "LON_RANGE"),
        help=(
            "Rectangular region: center lat/lon and total lat/lon ranges in "
            "degrees. Pass selection uses the elevation footprint at the "
            "center (same ~30° floor as --location) plus lon_range/2, so "
            "high-elevation corridor passes are kept. Window duration covers "
            "the lat transit across the region plus --duration."
        ),
    )
    loc.add_argument(
        "--bbox",
        nargs=4,
        type=float,
        metavar=("LAT_MIN", "LAT_MAX", "LON_MIN", "LON_MAX"),
        help="Geographic bounding box converted to --region mid/range",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=MAX_EPHEMERIS_ENTRIES,
        help=f"Max pass windows to upload (default/max {MAX_EPHEMERIS_ENTRIES})",
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=DEFAULT_TRANSMISSION_PERIOD_S,
        help=(
            "TX period in seconds centered on culmination "
            f"(default {DEFAULT_TRANSMISSION_PERIOD_S}, SDK NORMAL mode). "
            "For --region/--bbox this is added to the region transit duration."
        ),
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Fetch ephemeris and print predicted passes without BLE upload",
    )
    return parser.parse_args()


def region_from_args(args: argparse.Namespace) -> DeviceRegion | None:
    if args.region is not None:
        lat_mid, lon_mid, lat_range, lon_range = args.region
        if lat_range <= 0 or lon_range <= 0:
            raise ValueError("lat_range and lon_range must be positive")
        return DeviceRegion(lat_mid, lat_range, lon_mid, lon_range)
    if args.bbox is not None:
        lat_min, lat_max, lon_min, lon_max = args.bbox
        if lat_max <= lat_min or lon_max <= lon_min:
            raise ValueError("bbox requires lat_max>lat_min and lon_max>lon_min")
        return DeviceRegion(
            lat_mid=(lat_min + lat_max) / 2.0,
            lat_range=lat_max - lat_min,
            lon_mid=(lon_min + lon_max) / 2.0,
            lon_range=lon_max - lon_min,
        )
    return None


def main() -> None:
    args = parse_args()
    api_token = os.environ.get("HUBBLE_API_TOKEN")
    if not api_token:
        print(
            "HUBBLE_API_TOKEN env var must be set, e.g.\n"
            "  export HUBBLE_API_TOKEN=your_token",
            file=sys.stderr,
        )
        sys.exit(1)

    count = min(max(args.count, 1), MAX_EPHEMERIS_ENTRIES)
    satellites = fetch_orbital_params(api_token)

    try:
        region = region_from_args(args)
    except ValueError as exc:
        print(f"Invalid region/bbox: {exc}", file=sys.stderr)
        sys.exit(2)

    if region is not None:
        print(
            f"Device region: lat_mid={region.lat_mid}, lon_mid={region.lon_mid}, "
            f"lat_range={region.lat_range}, lon_range={region.lon_range}"
        )
        windows = build_region_pass_windows(
            satellites, region=region, count=count, duration_s=args.duration
        )
    else:
        lat_arg, lon_arg = (args.location if args.location else (None, None))
        lat, lon = resolve_location(lat_arg, lon_arg)
        print(f"Device location: lat={lat}, lon={lon}")
        windows = build_pass_windows(
            satellites, lat=lat, lon=lon, count=count, duration_s=args.duration
        )

    if args.dry_run:
        print("Dry run complete (no BLE upload)")
        return

    stop_event = asyncio.Event()
    signal.signal(signal.SIGINT, lambda *_: stop_event.set())
    try:
        asyncio.run(provision(windows, stop_event))
    except asyncio.CancelledError:
        # Device often drops the link right after SETUTC as it leaves
        # connectable mode; treat as success if upload finished.
        pass


if __name__ == "__main__":
    main()
