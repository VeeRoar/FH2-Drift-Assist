"""Build steering profiles from a local FH6 installation and fit an original GR2."""
import ctypes
import json
import math
import os
import tempfile
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile, ZIP_DEFLATED

from gr2_pose import ClipSampler, prepare_steering_clip, read_skeleton, use_granny
from grip_retarget import convert as retarget_grip
from hand_reference import reference, wheel_parents
from read_mojo import bind_skeleton, read_clip, wheel_phase
from retarget import Donor, SPINDLE


PROFILES = {"normal": ("round_37cm_25deg", "steering_270", 270),
            "yoke": ("yoke_37cm_18deg", "steering_140", 140)}
CACHE_FILE = "wheel-profiles-v1.zip"


def _member(archive, name):
    key = name.casefold()
    found = [i for i in archive.infolist() if i.filename.replace("\\", "/").casefold() == key]
    if len(found) != 1:
        raise ValueError(f"Expected one {name} inside {archive.filename}")
    return found[0]


def _make_profile(archive, rig, decoder, geometry, motion, limit):
    driver = read_clip(archive, f"clip/characters/male/incar/{geometry}.clipd", motion, decoder)
    wheel = read_clip(archive, f"clip/cars/{geometry}/steeringwheel.clipd", motion + "_ik_wheel", decoder)
    if driver["samples"] != wheel["samples"] or driver["duration"] != wheel["duration"]:
        raise ValueError("FH6 character and steering-wheel sample intervals differ")
    skeleton = bind_skeleton(archive, "characters\\male", driver["frames_by_bone"])
    phase = wheel_phase(wheel)
    for actual, expected in zip(phase["degrees_by_frame"][::len(phase["degrees_by_frame"])-1], (limit, -limit)):
        if abs(actual-expected) > 0.25:
            raise ValueError(f"Unexpected wheel sweep for {geometry}")
    data = dict(skeleton=skeleton, wheel_phase=phase, character=driver, wheel=wheel)
    coords = reference(data, 0.185, wheel_parents(archive, wheel["path"], rig))
    if any(s["max_m"] > 0.005 for s in coords["wrist_to_anchor_check"].values()):
        raise ValueError(f"Driver wrists do not follow the FH6 wheel anchors for {geometry}")
    return {"data": data, "reference": coords}


def _yoke_cars(cars):
    result = []
    for path in sorted(cars.glob("*.zip")):
        with ZipFile(path) as archive:
            entry = next((i for i in archive.infolist() if i.filename.casefold() == "ikanchorbones.xml"), None)
            if entry is None:
                continue
            ik = ET.fromstring(archive.read(entry))
        if ik.tag == "IKAnchorBones" and float(ik.get("SteeringWheelMaxDegrees", "0")) == 140:
            result.append(path.stem.casefold())
    return result


def prepare(fh6_media, acl_dll, cache):
    """Replace the local profile cache atomically after both profiles pass verification."""
    media = Path(fh6_media)
    decoder = ctypes.CDLL(str(Path(acl_dll).resolve(strict=True)))
    cars = media / "cars"
    with ZipFile(cars / "NIS_SkylineTurboRS_83.zip") as car:
        rig = car.read(_member(car, "Scene/animations/Mojo/skeleton.skeld"))
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=cache, prefix="wheel-profiles-", suffix=".tmp", delete=False) as f:
        tmp = Path(f.name)
    try:
        with ZipFile(media / "animation.zip") as source, ZipFile(tmp, "w", ZIP_DEFLATED) as dest:
            for key, params in PROFILES.items():
                profile = _make_profile(source, rig, decoder, *params)
                dest.writestr(key + ".json", json.dumps(profile, separators=(",", ":")))
            dest.writestr("yokes.json", json.dumps(_yoke_cars(cars)))
        os.replace(tmp, cache / CACHE_FILE)
    finally:
        tmp.unlink(missing_ok=True)
    return cache / CACHE_FILE


class Converter:
    def __init__(self, cache, granny_dll, driver_bytes):
        use_granny(granny_dll)
        self.rig = read_skeleton(driver_bytes)
        self.cache_file = Path(cache) / CACHE_FILE
        self.profiles = {}
        with ZipFile(self.cache_file) as archive:
            self.yokes = frozenset(json.loads(archive.read("yokes.json")))

    def profile_for_car(self, car):
        return "yoke" if Path(car).stem.casefold() in self.yokes else "normal"

    def convert(self, raw_clip_bytes, profile="normal"):
        if profile not in PROFILES:
            raise ValueError(f"Unknown steering profile {profile}")
        raw_clip_bytes = prepare_steering_clip(raw_clip_bytes)
        with ClipSampler(raw_clip_bytes) as clip:
            if SPINDLE not in clip.tracks:
                raise ValueError("Original steering-wheel track is missing")
            poses = [clip.pose(SPINDLE, i / 16)[0] for i in range(17)]
            sweep = sum(2 * math.acos(min(1, abs(sum(a*b for a, b in zip(p, q)))))
                        for p, q in zip(poses, poses[1:]))
            if math.degrees(sweep) > 250:
                raise ValueError("Car already has a long-travel steering clip; supply its original clip")
        if profile not in self.profiles:
            with ZipFile(self.cache_file) as source:
                value = json.loads(source.read(profile + ".json"))
            self.profiles[profile] = Donor(value["data"], value["reference"])
        try:
            output, _ = retarget_grip(raw_clip_bytes, self.rig, self.profiles[profile])
        except KeyError as exc:
            raise ValueError(f"Unsupported steering rig; missing bone {exc}") from exc
        return output
