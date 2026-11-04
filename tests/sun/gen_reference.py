"""Writes tests/sun/reference.inc: expected sunrise and sunset for test_sun.cpp.

Two independent sources, neither shares code with src/SunTimes.cpp:
  1. PyEphem (pip install ephem): an astronomy library (VSOP87-based sun
     position, refraction model off, horizon -0:34 so the upper edge of the
     sun touches the horizon, which is the usual sunrise definition).
     These values go into the table.
  2. NOAA's solar calculator equations (Meeus, "Astronomical Algorithms"),
     written out here in double precision with a fixed 90.833 degree zenith.
     Used only as a cross-check: the script stops if the two differ by
     more than TOLERANCE minutes, and prints the largest difference.

Times are minutes since local midnight, modulo 1440 (a sunset just after
midnight is 0..), rounded to the nearest minute.

Usage: python3 gen_reference.py year.inc > reference.inc   (needs: pip install ephem)
"""
import datetime as dt
import math
import sys

import ephem

TOLERANCE = 2  # minutes between the two sources

# name, latitude, longitude, local UTC offset in minutes, dates (y, m, d)
PLACES = [
    ("Berlin", 52.52, 13.405, [
        (2026, 3, 20, 60), (2026, 3, 29, 120), (2026, 6, 21, 120),
        (2026, 9, 23, 120), (2026, 10, 25, 60), (2026, 12, 21, 60),
        # leap day, the day after it, and the far end of the useful range
        (2028, 2, 29, 60), (2028, 3, 1, 60), (2040, 7, 1, 120)]),
    ("Equator (Quito)", -0.18, -78.47, [
        (2026, 3, 20, -300), (2026, 6, 21, -300),
        (2026, 9, 23, -300), (2026, 12, 21, -300)]),
    ("Equator (Greenwich meridian)", 0.0, 0.0, [
        (2026, 3, 20, 0), (2026, 12, 21, 0)]),
    ("Sydney", -33.87, 151.21, [
        (2026, 3, 20, 660), (2026, 6, 21, 600),
        (2026, 9, 23, 600), (2026, 12, 21, 660)]),
    ("Reykjavik", 64.15, -21.94, [
        (2026, 3, 20, 0), (2026, 6, 21, 0), (2026, 9, 23, 0),
        (2026, 12, 21, 0)]),
    ("Tromso", 69.65, 18.96, [
        (2026, 3, 20, 60), (2026, 5, 10, 120), (2026, 6, 21, 120),
        (2026, 9, 23, 120), (2026, 11, 10, 60), (2026, 12, 21, 60),
        (2027, 1, 30, 60)]),
    ("McMurdo (Antarctica)", -77.85, 166.67, [
        (2026, 3, 20, 780), (2026, 6, 21, 720), (2026, 12, 21, 780)]),
    # Far east of the time zone: sunrise lands near local midnight.
    ("Kiritimati (+14)", 1.87, -157.4, [(2026, 6, 21, 840)]),
    ("Honolulu", 21.31, -157.86, [(2026, 12, 21, -600), (2026, 6, 21, -600)]),
    ("Delhi (+5:30)", 28.61, 77.21, [(2026, 6, 21, 330), (2026, 12, 21, 330)]),
    # A clock that is far from the sun: UTC at Sydney, so sunrise is the
    # evening before (negative before wrapping to 0..1439).
    ("Sydney on a UTC clock", -33.87, 151.21, [(2026, 12, 21, 0)]),
    ("Cape Town", -33.92, 18.42, [(2026, 6, 21, 120), (2026, 12, 21, 120)]),
]

# Kind of every day of 2026 at Tromso: N = sun rises and sets, D = polar
# day, X = polar night. Checks where the polar periods start and end.
YEAR_PLACE = ("Tromso", 69.65, 18.96, 2026, 60)


def jd(y, m, d, minutes_utc):
    """Julian day of a UTC moment (double)."""
    if m <= 2:
        y -= 1
        m += 12
    a = y // 100
    b = 2 - a + a // 4
    day = int(365.25 * (y + 4716)) + int(30.6001 * (m + 1)) + d + b - 1524.5
    return day + minutes_utc / 1440.0


def noaa(lat, lon, y, m, d, off):
    """NOAA solar calculator. Returns None, 'day', 'night' or (rise, set)."""
    def at(local_minute):
        jdv = jd(y, m, d, local_minute - off)
        t = (jdv - 2451545.0) / 36525.0
        l0 = (280.46646 + t * (36000.76983 + t * 0.0003032)) % 360
        mm = 357.52911 + t * (35999.05029 - 0.0001537 * t)
        e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t)
        mr = math.radians(mm)
        c = (math.sin(mr) * (1.914602 - t * (0.004817 + 0.000014 * t))
             + math.sin(2 * mr) * (0.019993 - 0.000101 * t)
             + math.sin(3 * mr) * 0.000289)
        true_long = l0 + c
        omega = 125.04 - 1934.136 * t
        app = true_long - 0.00569 - 0.00478 * math.sin(math.radians(omega))
        sec = 21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))
        eps0 = 23 + (26 + sec / 60) / 60
        eps = eps0 + 0.00256 * math.cos(math.radians(omega))
        decl = math.degrees(math.asin(
            math.sin(math.radians(eps)) * math.sin(math.radians(app))))
        y_ = math.tan(math.radians(eps / 2)) ** 2
        l0r = math.radians(l0)
        eqt = 4 * math.degrees(
            y_ * math.sin(2 * l0r) - 2 * e * math.sin(mr)
            + 4 * e * y_ * math.sin(mr) * math.cos(2 * l0r)
            - 0.5 * y_ * y_ * math.sin(4 * l0r)
            - 1.25 * e * e * math.sin(2 * mr))
        return decl, eqt

    def ha(decl):
        la, de = math.radians(lat), math.radians(decl)
        x = (math.cos(math.radians(90.833)) / (math.cos(la) * math.cos(de))
             - math.tan(la) * math.tan(de))
        return x

    decl, eqt = at(720)
    x = ha(decl)
    if x > 1:
        return "night"
    if x < -1:
        return "day"
    res = []
    for sign in (-1, 1):
        t = 720.0
        for _ in range(4):
            decl, eqt = at(t)
            x = max(-1.0, min(1.0, ha(decl)))
            h = math.degrees(math.acos(x))
            t = 720 - 4 * lon - eqt + off + sign * 4 * h
        res.append(t)
    return res


def ephem_times(lat, lon, y, m, d, off):
    obs = ephem.Observer()
    obs.lat, obs.lon = str(lat), str(lon)
    obs.elevation = 0
    obs.pressure = 0
    obs.horizon = "-0:34"
    start = dt.datetime(y, m, d) - dt.timedelta(minutes=off)
    obs.date = start.strftime("%Y/%m/%d %H:%M:%S")
    sun = ephem.Sun()
    out = []
    for fn in (obs.next_rising, obs.next_setting):
        try:
            t = fn(sun, use_center=False).datetime()
        except ephem.AlwaysUpError:
            return "day"
        except ephem.NeverUpError:
            return "night"
        local = t + dt.timedelta(minutes=off)
        out.append(local.hour * 60 + local.minute + local.second / 60.0)
    return out


def main():
    rows = []
    worst = 0.0
    for name, lat, lon, dates in PLACES:
        for y, m, d, off in dates:
            e = ephem_times(lat, lon, y, m, d, off)
            n = noaa(lat, lon, y, m, d, off)
            if isinstance(e, str):
                if n != e:
                    sys.exit("polar mismatch %s %d-%02d-%02d: %s vs %s"
                             % (name, y, m, d, e, n))
                rows.append((name, lat, lon, y, m, d, off, e, 0, 0))
                continue
            if isinstance(n, str):
                sys.exit("polar mismatch %s %d-%02d-%02d: %s vs %s"
                         % (name, y, m, d, e, n))
            for a, b in zip(e, n):
                diff = abs((a - b + 720) % 1440 - 720)
                worst = max(worst, diff)
                if diff > TOLERANCE:
                    sys.exit("sources differ by %.2f min: %s %d-%02d-%02d"
                             % (diff, name, y, m, d))
            rows.append((name, lat, lon, y, m, d, off, "normal",
                         int(round(e[0])) % 1440, int(round(e[1])) % 1440))
    name, lat, lon, year, off = YEAR_PLACE
    letters = {"normal": "N", "day": "D", "night": "X"}
    kinds_year = ""
    d0 = dt.date(year, 1, 1)
    for i in range(365):
        d = d0 + dt.timedelta(days=i)
        e = ephem_times(lat, lon, d.year, d.month, d.day, off)
        kinds_year += letters["normal"] if not isinstance(e, str) else letters[e]
    print("// Generated by gen_reference.py (PyEphem, cross-checked against the")
    print("// NOAA solar calculator equations; largest difference %.2f min)." % worst)
    print("// kind: 0 = sun rises and sets, 1 = polar day, 2 = polar night.")
    print("// name, lat, lon, year, month, day, UTC offset (min), kind, rise, set")
    kinds = {"normal": 0, "day": 1, "night": 2}
    for r in rows:
        print('{"%s", %.2ff, %.3ff, %d, %d, %d, %d, %d, %d, %d},'
              % (r[0], r[1], r[2], r[3], r[4], r[5], r[6], kinds[r[7]], r[8], r[9]))
    with open(sys.argv[1], "w") as out:
        out.write("// Generated by gen_reference.py: %s %d, N = sun rises and sets,\n"
                  "// D = polar day, X = polar night, one letter per day from 1 January.\n"
                  % (name, year))
        out.write('"%s"\n' % kinds_year)


main()
