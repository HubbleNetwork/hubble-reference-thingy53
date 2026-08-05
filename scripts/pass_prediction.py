#!/usr/bin/env python3
#
# Copyright (c) 2026 Hubble Network, Inc.
#
# SPDX-License-Identifier: Apache-2.0

"""Host-side port of hubble_sat_ephemeris.c pass prediction."""

from __future__ import annotations

import math
from dataclasses import dataclass

EARTH_RADIUS = 6378136.999954619
EARTH_MU = 398600441800000.0
EARTH_ROTATION_RATE = 7.292115855377074e-05
TEME_REF_DATETIME_2027 = 1798761600
TEME_ANGLE_2027 = 1.7526971469712507
ELEVATION_ANGLE_TOLERANCE = 30

# Matches NORMAL reliability in hubble_sat.c (8 retries * 20 s).
DEFAULT_TRANSMISSION_PERIOD_S = 160


@dataclass(frozen=True)
class OrbitalParams:
    t0: int
    n0: float
    ndot: float
    raan0: float
    raandot: float
    aop0: float
    aopdot: float
    inclination: float
    eccentricity: float
    satellite_id: int = 0


@dataclass
class PassInfo:
    lon: float
    start: int
    culmination: int
    duration: int
    ascending: bool


@dataclass(frozen=True)
class _Crossing:
    t: int
    lon: float


def _deg2rad(deg: float) -> float:
    return deg * (math.pi / 180.0)


def _rad2deg(rad: float) -> float:
    return rad * (180.0 / math.pi)


def _signed_fmod(x: float, y: float) -> float:
    if y == 0.0:
        return float("nan")
    ret = math.fmod(x, y)
    if ret != 0.0 and ((y < 0 and ret > 0) or (y > 0 and ret < 0)):
        ret += y
    return ret


def _zero_to_2pi(angle: float) -> float:
    if angle < 0.0:
        return angle + (2.0 * math.pi)
    return math.fmod(angle, 2.0 * math.pi)


def _minus_180_to_180(angle: float) -> float:
    return _signed_fmod(angle + 180.0, 360.0) - 180.0


def _zero_to_360(angle: float) -> float:
    return _signed_fmod(angle, 360.0)


def _anomaly_from_theta_mean(e: float, theta: float) -> float:
    if e == 0.0:
        return theta
    e_anom = 2 * math.atan(math.sqrt((1 - e) / (1 + e)) * math.tan(theta / 2))
    me = e_anom - e * math.sin(e_anom)
    return _zero_to_2pi(me)


def _anode_time_get(info: OrbitalParams, count: int) -> int:
    if info.ndot == 0.0:
        dt = count / info.n0
    else:
        dt = (math.sqrt(info.n0 * info.n0 + 2 * info.ndot * count) - info.n0) / info.ndot
    return int(info.t0 + round(dt))


def _orbit_count_get(info: OrbitalParams, t: int) -> int:
    dt = t - info.t0
    return int((info.n0 * dt) + (0.5 * info.ndot) * dt * dt)


def _longitude_get(ra: float, t: int) -> float:
    dt = t - TEME_REF_DATETIME_2027
    lon_rad = ra - TEME_ANGLE_2027 - EARTH_ROTATION_RATE * dt
    return _minus_180_to_180(_rad2deg(lon_rad))


def _tll_crossings_get(
    orbit: OrbitalParams, tll: float, orbit_count: int
) -> tuple[_Crossing, _Crossing] | None:
    latrad = _deg2rad(tll)
    inclination = _deg2rad(orbit.inclination)

    if inclination < 0 or inclination > math.pi:
        return None
    if abs(math.sin(inclination)) <= abs(math.sin(latrad)):
        return None

    anode_time = _anode_time_get(orbit, orbit_count)
    dt_anode = anode_time - orbit.t0
    raan = orbit.raan0 + (orbit.raandot * dt_anode)
    aop = orbit.aop0 + (orbit.aopdot * dt_anode)
    orbit_period = 1.0 / (orbit.n0 + (orbit.ndot * dt_anode))

    if latrad >= 0:
        ra1 = raan + math.asin(math.tan(latrad) / math.tan(inclination))
        ra2 = raan + math.pi - math.asin(math.tan(latrad) / math.tan(inclination))
        lam1 = math.asin(math.sin(latrad) / math.sin(inclination))
        lam2 = math.pi - lam1
    else:
        ra2 = raan + math.asin(math.tan(latrad) / math.tan(inclination))
        ra1 = raan + math.pi - math.asin(math.tan(latrad) / math.tan(inclination))
        lam1 = math.pi - math.asin(math.sin(latrad) / math.sin(inclination))
        lam2 = (3 * math.pi) - lam1

    if lam1 < 0 or lam1 >= (2 * math.pi) or lam2 < 0 or lam2 >= (2 * math.pi):
        return None
    if lam1 >= lam2:
        return None

    me0 = _anomaly_from_theta_mean(orbit.eccentricity, -aop)
    me1 = _anomaly_from_theta_mean(orbit.eccentricity, lam1 - aop)
    me2 = _anomaly_from_theta_mean(orbit.eccentricity, lam2 - aop)

    t0 = anode_time + int(
        round(_signed_fmod(orbit_period * (me1 - me0) / (2 * math.pi), orbit_period))
    )
    t1 = anode_time + int(
        round(_signed_fmod(orbit_period * (me2 - me0) / (2 * math.pi), orbit_period))
    )
    return _Crossing(t0, _longitude_get(ra1, t0)), _Crossing(t1, _longitude_get(ra2, t1))


def _sat_altitude_get(orbit: OrbitalParams, t: int) -> float:
    dt = t - orbit.t0
    n = orbit.n0 + (orbit.ndot * dt)
    return (math.sqrt(EARTH_MU) / (2 * math.pi * n)) ** (2.0 / 3.0) - EARTH_RADIUS


def _lon_tolerance_get(lat: float, sat_altitude: float) -> float:
    a = _deg2rad(ELEVATION_ANGLE_TOLERANCE + 90)
    c = math.asin(EARTH_RADIUS * math.sin(a) / (EARTH_RADIUS + sat_altitude))
    b = EARTH_RADIUS * math.cos(
        math.pi - math.asin((EARTH_RADIUS + sat_altitude) * (math.sin(c) / EARTH_RADIUS))
    ) + ((EARTH_RADIUS + sat_altitude) * math.cos(c))
    b_ang = math.asin(b * math.sin(c) / EARTH_RADIUS)
    return _rad2deg(math.asin((EARTH_RADIUS * math.sin(b_ang)) / (EARTH_RADIUS * math.cos(_deg2rad(lat)))))


def _next_pass_step(
    orbit: OrbitalParams,
    ascending: bool,
    delta_lon: float,
    lon_tol: float,
    lat: float,
    lon: float,
    crossings: list[_Crossing],
    t: int,
) -> tuple[int, float, bool] | None:
    """One search step from hubble_sat_ephemeris.c::_next_pass_get.

    Returns (culmination, lon, ascending) on success of the step (culmination may
    still be 0 if nothing matched yet), or None on geometry failure.
    """
    index = 0 if ascending else 1
    dt = _deg2rad(delta_lon) / EARTH_ROTATION_RATE
    orbit_count = _orbit_count_get(orbit, crossings[index].t + int(round(dt)))
    updated = _tll_crossings_get(orbit, lat, orbit_count)
    if updated is None:
        return None
    crossings[0], crossings[1] = updated

    culmination = 0
    pass_lon = 0.0
    pass_asc = False
    while culmination == 0 and (
        360.0 - _zero_to_360(lon - lon_tol - crossings[index].lon) < 180.0
    ):
        if (
            abs(_minus_180_to_180(crossings[index].lon - lon)) <= lon_tol
            and crossings[index].t > t
        ):
            culmination = crossings[index].t
            pass_lon = crossings[index].lon
            pass_asc = (lat > 0) if ascending else (lat <= 0)
        else:
            orbit_count += 1
            updated = _tll_crossings_get(orbit, lat, orbit_count)
            if updated is None:
                return None
            crossings[0], crossings[1] = updated

    return culmination, pass_lon, pass_asc


def _pass_get(
    orbit: OrbitalParams, t: int, lat: float, lon: float, lon_tol: float
) -> PassInfo | None:
    orbit_count = _orbit_count_get(orbit, t)
    if orbit_count < 0:
        return None

    crossings_t = _tll_crossings_get(orbit, lat, orbit_count)
    if crossings_t is None:
        return None
    crossings = list(crossings_t)

    while crossings[0].t <= t and crossings[1].t <= t:
        orbit_count += 1
        crossings_t = _tll_crossings_get(orbit, lat, orbit_count)
        if crossings_t is None:
            return None
        crossings = list(crossings_t)

    culmination = 0
    pass_lon = 0.0
    ascending = False

    if abs(_minus_180_to_180(crossings[0].lon - lon)) <= lon_tol and crossings[0].t > t:
        culmination = crossings[0].t
        pass_lon = crossings[0].lon
        ascending = lat > 0
    elif abs(_minus_180_to_180(crossings[1].lon - lon)) <= lon_tol and crossings[1].t > t:
        culmination = crossings[1].t
        pass_lon = crossings[1].lon
        ascending = lat <= 0

    search_t = t
    for _ in range(10_000):
        if culmination != 0:
            break

        delta_lon_a = 360.0 - _zero_to_360(lon + lon_tol - crossings[0].lon)
        delta_lon_d = 360.0 - _zero_to_360(lon + lon_tol - crossings[1].lon)

        if delta_lon_a < delta_lon_d:
            step = _next_pass_step(
                orbit, True, delta_lon_a, lon_tol, lat, lon, crossings, search_t
            )
            search_t = crossings[0].t
        else:
            step = _next_pass_step(
                orbit, False, delta_lon_d, lon_tol, lat, lon, crossings, search_t
            )
            search_t = crossings[1].t

        if step is None:
            return None

        culmination, pass_lon, ascending = step
    else:
        return None

    if culmination == 0:
        return None
    return PassInfo(pass_lon, 0, culmination, 0, ascending)


def next_pass_get(
    satellites: list[OrbitalParams],
    t: int,
    lat: float,
    lon: float,
    transmission_period_s: int = DEFAULT_TRANSMISSION_PERIOD_S,
) -> PassInfo | None:
    """Return the soonest next pass across all satellites (SDK hubble_next_pass_get)."""
    if not satellites:
        return None

    best: PassInfo | None = None
    for sat in satellites:
        alt = _sat_altitude_get(sat, t)
        lon_tol = _lon_tolerance_get(lat, alt)
        candidate = _pass_get(sat, t, lat, lon, lon_tol)
        if candidate is None:
            continue
        if best is None or candidate.culmination < best.culmination:
            best = candidate

    if best is None:
        return None

    best.duration = transmission_period_s
    best.start = best.culmination - (transmission_period_s // 2)
    return best


@dataclass(frozen=True)
class DeviceRegion:
    """Rectangular geographic region (SDK hubble_sat_device_region)."""

    lat_mid: float
    lat_range: float
    lon_mid: float
    lon_range: float


def _region_pass_duration(
    sat: OrbitalParams,
    pass_info: PassInfo,
    lat_min: float,
    lat_max: float,
) -> int | None:
    """Compute region transit duration from lat-bound crossings (SDK logic)."""
    orbit_count = _orbit_count_get(sat, pass_info.culmination)
    if orbit_count < 0:
        return None

    if (lat_min * lat_max) < 0:
        crossings_min = _tll_crossings_get(sat, lat_min, orbit_count)
        if crossings_min is None:
            return None
        if pass_info.ascending:
            crossings_max = _tll_crossings_get(sat, lat_max, orbit_count + 1)
            if crossings_max is None:
                return None
            return crossings_max[0].t - crossings_min[1].t
        crossings_max = _tll_crossings_get(sat, lat_max, orbit_count)
        if crossings_max is None:
            return None
        return crossings_min[0].t - crossings_max[1].t

    if lat_min < 0 and lat_max < 0:
        crossings_min = _tll_crossings_get(sat, lat_min, orbit_count)
        crossings_max = _tll_crossings_get(sat, lat_max, orbit_count)
        if crossings_min is None or crossings_max is None:
            return None
        if pass_info.ascending:
            return crossings_max[1].t - crossings_min[1].t
        return crossings_min[0].t - crossings_max[0].t

    if lat_min < 0 or lat_max < 0:
        return None

    crossings_min = _tll_crossings_get(sat, lat_min, orbit_count)
    crossings_max = _tll_crossings_get(sat, lat_max, orbit_count)
    if crossings_min is None or crossings_max is None:
        return None
    if pass_info.ascending:
        return crossings_max[0].t - crossings_min[0].t
    return crossings_min[1].t - crossings_max[1].t


def next_pass_region_get(
    satellites: list[OrbitalParams],
    t: int,
    region: DeviceRegion,
    transmission_period_s: int = DEFAULT_TRANSMISSION_PERIOD_S,
) -> PassInfo | None:
    """Soonest next pass visible from a region.

    Host-side adaptation of SDK ``hubble_next_pass_region_get``. The SDK uses
    ``lon_range / 2`` alone as the ground-track longitude gate, which only
    accepts near-nadir overflights of the rectangle. For a multi-city corridor
    that is far too picky: a ~74° elevation pass can sit a couple degrees of
    longitude off the midline and be rejected.

    Here ``lon_tol`` is the elevation footprint at ``lat_mid`` (same 30° floor
    as ``next_pass_get``) plus half the region ``lon_range``, so any pass that
    is usable from some longitude in the corridor is kept. Duration still uses
    the SDK lat-bound transit across the region, plus ``transmission_period_s``.
    """
    if not satellites:
        return None

    lat_mid = region.lat_mid
    if lat_mid == 0.0:
        lat_mid = 1e-3

    lat_min = lat_mid - (region.lat_range / 2.0)
    lat_max = lat_mid + (region.lat_range / 2.0)
    region_lon_half = region.lon_range / 2.0

    best: PassInfo | None = None
    for sat in satellites:
        alt = _sat_altitude_get(sat, t)
        # Elevation cone at center + geographic corridor half-width.
        lon_tol = _lon_tolerance_get(lat_mid, alt) + region_lon_half
        candidate = _pass_get(sat, t, lat_mid, region.lon_mid, lon_tol)
        if candidate is None:
            continue

        duration = _region_pass_duration(sat, candidate, lat_min, lat_max)
        if duration is None:
            continue
        candidate.duration = duration

        if best is None or candidate.culmination < best.culmination:
            best = candidate

    if best is None:
        return None

    best.duration += transmission_period_s
    best.start = best.culmination - (best.duration // 2)
    return best


def predict_passes(
    satellites: list[OrbitalParams],
    lat: float,
    lon: float,
    start_time: int,
    count: int,
    transmission_period_s: int = DEFAULT_TRANSMISSION_PERIOD_S,
) -> list[PassInfo]:
    """Predict up to ``count`` successive overhead passes from ``start_time``."""
    passes: list[PassInfo] = []
    t = start_time
    for _ in range(count):
        nxt = next_pass_get(satellites, t, lat, lon, transmission_period_s)
        if nxt is None:
            break
        passes.append(nxt)
        # Advance past this pass so the next query finds a later one.
        t = max(nxt.culmination + 1, nxt.start + nxt.duration)
    return passes


def predict_region_passes(
    satellites: list[OrbitalParams],
    region: DeviceRegion,
    start_time: int,
    count: int,
    transmission_period_s: int = DEFAULT_TRANSMISSION_PERIOD_S,
) -> list[PassInfo]:
    """Predict up to ``count`` successive region passes from ``start_time``."""
    passes: list[PassInfo] = []
    t = start_time
    for _ in range(count):
        nxt = next_pass_region_get(satellites, t, region, transmission_period_s)
        if nxt is None:
            break
        passes.append(nxt)
        t = max(nxt.culmination + 1, nxt.start + nxt.duration)
    return passes
