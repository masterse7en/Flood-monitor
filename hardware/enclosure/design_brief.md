# Flood Monitor Enclosure: Design Brief

Paste this into the new chat as the starting context.

## What the device does

Solar-powered flood level monitor, mounted above a stream or drain in Naduvannur, Kozhikode, Kerala (about 11.5 N). An ESP32-S3 measures the distance down to the water with a waterproof ultrasonic sensor, takes photos with an onboard camera, and posts readings and photos to AWS over WiFi. It must survive the Kerala monsoon: heavy rain, high humidity, strong sun and heat.

## Components inside the case

Dimensions are typical values. Measure the real parts before final CAD.

| Part | Typical size (mm) | Notes |
|---|---|---|
| ESP32-S3 N16R8 dual USB devkit with OV3660 camera | 63 x 28 x 15 | Camera ribbon on the board. Two USB-C ports on one end, needed for flashing |
| JSN-SR04T control board | 41 x 28 x 10 | Connects to the probe by a 2.5 m cable |
| JSN-SR04T probe | 18 dia x 25 deep (shaft), 22 dia flange | Waterproof. Must point straight down at the water |
| 2x 18650 Li-ion cells in a dual holder (parallel) | 77 x 41 x 21 | Largest and heaviest part |
| TP4056 charge module with protection | 26 x 17 x 5 | Solar input, battery output |
| Boost converter MT3608 (battery to 5 V for the board's 5V pin) or 3.3 V LDO | 37 x 17 x 6 | Board needs 5 V or a regulated 3.3 V, not raw 4.2 V |
| Inline fuse (2 A) and a power switch | small | Switch should be reachable without opening the main seal if possible |

## Outside the case

| Part | Typical size (mm) | Notes |
|---|---|---|
| 6 V 5 W solar panel (10 W optional for more margin) | about 220 x 170 x 17 (5 W) | Mounted on top, also acts as the rain roof |

## Power circuit

```
Solar panel 6 V --> TP4056 IN+/IN-
TP4056 B+/B-    --> 2x 18650 in parallel
TP4056 OUT+/-   --> fuse --> switch --> MT3608 (set to 5.0 V) --> ESP32-S3 5V and GND
ESP32-S3 3V3    --> JSN-SR04T VCC (TRIG GPIO 1, ECHO GPIO 2)
```

Average load is about 160 to 240 mAh a day with deep sleep. Two 18650 cells give about 10 days of runtime with no sun.

## Mounting and orientation

1. Mounted on a pole or wall bracket over the water, stainless steel hardware.
2. Ultrasonic probe faces straight down, with a clear cone below it (about 15 degrees each side). Nothing in the beam: no bracket arms, no walls.
3. The probe must sit at least 25 cm above the highest expected flood level. That is the sensor's blind zone.
4. Camera faces down and out toward the water surface or a painted depth gauge. Needs a clear window.
5. Solar panel on top, tilted about 10 to 15 degrees facing south. The tilt also sheds rain.

## Enclosure requirements

1. Weatherproofing: IP65 or better. Lid gasket (silicone or TPU), screws with O-rings or a clamp lid.
2. Material: ASA or PETG if 3D printed. Not PLA, which softens in the sun. Light colour to reduce heat.
3. Heat: the battery compartment must stay shaded. Li-ion should not charge above 45 C. The panel can overhang the case as a sunshade, with an air gap between the panel and the lid.
4. Humidity: a breather vent (Gore-type ePTFE patch) to stop condensation and pressure pumping. Space for a silica gel sachet.
5. Camera window: clear acrylic or glass, sealed, recessed under a small hood so rain does not sit on it. Slight downward angle so drops run off.
6. Probe mount: 18 mm hole in the bottom face with a grommet or a PG-style gland, or a printed clamp collar. The bottom face must stay level.
7. Cable entries: PG7 cable glands for the solar cable and any external probe cable, entering from the bottom only. Drip loops outside.
8. Layout: battery at the bottom (weight, shade), electronics above it on standoffs, camera board near the window. Separate battery bay, if possible.
9. WiFi: plastic walls only near the ESP32 antenna. No metal plates or foil near the top of the board.
10. Service access: open the lid to reach USB-C for flashing and the battery holder without unmounting the case.
11. Optional: a small light pipe for a status LED, and a sealed external switch.

## Deliverables I want

1. Overall dimensions and an exploded layout of the parts inside.
2. Enclosure body, lid, panel mount and pole/wall bracket.
3. Positions for the camera window, probe hole, glands, vent and switch.
4. CAD or 3D printable files (OpenSCAD, STEP or STL) once the layout is agreed.
