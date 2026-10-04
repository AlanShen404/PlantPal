"""Check breadboard electrical nets, including internal five-hole strips.

Run from the PlantPal folder with python tests/check_circuit.py.
This verifies the diagram, not the analog behavior of real components.
"""
import json
import re
from pathlib import Path

diagram = json.loads((Path(__file__).parent.parent / "diagram.json").read_text())
parts = {p["id"]: p for p in diagram["parts"]}
assert len(parts) == len(diagram["parts"]), "Duplicate part IDs"
parent = {}


def canonical(pin):
    if pin.startswith("bb:"):
        hole = pin[3:]
        rail = re.fullmatch(r"(tp|tn|bp|bn)\.(\d+)", hole)
        if rail:
            assert 1 <= int(rail[2]) <= 25
            return "bb:" + rail[1]
        strip = re.fullmatch(r"(\d+)(t|b)\.([a-j])", hole)
        assert strip and 1 <= int(strip[1]) <= 30, pin
        assert strip[3] in ("abcde" if strip[2] == "t" else "fghij"), pin
        return "bb:" + strip[1] + strip[2]
    return pin


def root(pin):
    pin = canonical(pin)
    parent.setdefault(pin, pin)
    while parent[pin] != pin:
        parent[pin] = parent[parent[pin]]
        pin = parent[pin]
    return pin


def join(a, b):
    parent[root(a)] = root(b)


for a, b, color, route in diagram["connections"]:
    if not (a.startswith("$serialMonitor:") or b.startswith("$serialMonitor:")):
        assert color and "$bb" not in route, "Use explicit wires: visual placement alone is not a verified connection"
    for endpoint in (a, b):
        assert endpoint.split(":")[0] in parts or endpoint.startswith("$serialMonitor:")
    join(a, b)


def same(a, b):
    assert root(a) == root(b), f"Disconnected: {a} <-> {b}"


power, ground = root("esp:3V3"), root("esp:GND.1")
assert power != ground, "Supply shorted to ground"
for rail, expected in (("tp.1", power), ("bp.1", power), ("tn.1", ground), ("bn.1", ground)):
    assert root("bb:" + rail) == expected
for sensor in ("soil", "tank", "dht", "ldr"):
    same(sensor + ":VCC", "esp:3V3")
    same(sensor + ":GND", "esp:GND.1")
signals = {
    "soil:SIG": "esp:D34", "tank:SIG": "esp:D35", "dht:SDA": "esp:RX2",
    "ldr:AO": "esp:D32", "ack:1.l": "esp:D27", "fault:1.l": "esp:D14",
    "buzzer:2": "esp:D23",
}
for sensor, gpio in signals.items():
    same(sensor, gpio)
    assert root(gpio) not in (power, ground), f"Signal shorted to supply: {gpio}"
for resistor, gpio, led in (("rr", "esp:D18", "mood:R"), ("rg", "esp:D19", "mood:G"),
                           ("rb", "esp:D21", "mood:B"), ("rp", "esp:D26", "pump:A")):
    same(resistor + ":1", gpio)
    same(resistor + ":2", led)
    assert root(gpio) != root(led), f"Bypassed resistor: {resistor}"
    assert root(gpio) not in (power, ground)
    assert root(led) not in (power, ground)
    assert parts[resistor]["attrs"]["value"] == "220"
for pin in ("mood:COM", "pump:C", "buzzer:1", "ack:2.l", "fault:2.l"):
    same(pin, "esp:GND.1")
same("pullup:1", "esp:3V3")
same("pullup:2", "dht:SDA")
assert parts["pullup"]["attrs"]["value"] == "10000"
all_gpios = list(signals.values()) + ["esp:D18", "esp:D19", "esp:D21", "esp:D26"]
assert len({root(p) for p in all_gpios}) == len(all_gpios), "GPIO signals shorted together"
print("PASS: breadboard rails, sensor supply/signals, 4 LED resistors, DHT pull-up, GPIO isolation")
