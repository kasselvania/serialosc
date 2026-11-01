#!/usr/bin/env python3
"""Lapis USB observations from sysfs only; never opens /dev or sends USB requests.

No external commands, serial libraries, driver binds, writes, or service changes.
An observation is not evidence of sufficient voltage/current or device readiness.
"""
import argparse
import json
import platform
from pathlib import Path

USB_FIELDS = ('idVendor', 'idProduct', 'manufacturer', 'product', 'serial',
              'busnum', 'devnum', 'speed', 'bMaxPower', 'bConfigurationValue',
              'authorized')
POWER_FIELDS = ('control', 'runtime_status')
PORT_FIELDS = ('power_enabled', 'overcurrent', 'over_current_count', 'disable')


def read_attribute(path):
    """Read a bounded sysfs attribute. Caller supplies sysfs paths, not devnodes."""
    try:
        with path.open('r', encoding='utf-8', errors='replace') as stream:
            return stream.read(4096).strip()
    except (OSError, ValueError) as error:
        return {'unavailable': type(error).__name__}


def attributes(directory, fields):
    return {name: read_attribute(directory / name) for name in fields}


def observe(sys_root=Path('/sys')):
    sys_root = Path(sys_root)
    usb_root = sys_root / 'bus' / 'usb' / 'devices'
    devices = []
    ports = []
    for device in sorted(usb_root.glob('*')):
        if (device / 'idVendor').is_file():
            item = {'sysfs_name': device.name, **attributes(device, USB_FIELDS)}
            item['runtime_power'] = attributes(device / 'power', POWER_FIELDS)
            item['interfaces'] = []
            for interface in sorted(usb_root.glob(device.name + ':*')):
                driver = interface / 'driver'
                item['interfaces'].append({
                    'sysfs_name': interface.name,
                    'driver': driver.resolve().name if driver.is_symlink() else None,
                })
            devices.append(item)
        for port in sorted(device.glob('*-port[0-9]*')):
            if (port / 'power_enabled').exists() or (port / 'over_current_count').exists():
                ports.append({'sysfs_name': port.name,
                              'interface': device.name,
                              **attributes(port, PORT_FIELDS)})
    return {
        'schema_version': 1,
        'mode': 'sysfs-read-only',
        'machine': platform.machine(),
        'devices': devices,
        'hub_ports': ports,
        'limitations': [
            'Does not open serial ports, raw USB nodes, or FunctionFS endpoints.',
            'Does not trace other host processes or rule out other process activity.',
            'bMaxPower is declared device demand, not measured draw or port capacity.',
            'Power enabled and no overcurrent do not rule out voltage transients.',
            'An enumerated USB bridge does not prove a ready Arc controller.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    print(json.dumps(observe(), indent=2))


if __name__ == '__main__':
    main()
