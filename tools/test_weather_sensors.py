"""Exercise production weather entity metadata/value handling without a HA server."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'homeassistant/custom_components/etcore/sensor.py'


def load_sensor():
    tree = ast.parse(SOURCE.read_text())
    nodes = [ast.ImportFrom(module='__future__', names=[ast.alias(name='annotations')], level=0)]
    for node in tree.body:
        if isinstance(node, (ast.Assign, ast.AnnAssign)) or isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in ['_metric', 'EtCoreSensor']:
            nodes.append(node)
    class Classes:
        def __getattr__(self, name): return name.lower()
    class Entity:
        hass = None
    ns = {'SensorDeviceClass': Classes(), 'SensorStateClass': Classes(), 'SensorEntity': Entity, 'DOMAIN': 'etcore'}
    exec(compile(ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[])), str(SOURCE), 'exec'), ns)
    return ns['EtCoreSensor'], ns['_metric']


class WeatherSensors(unittest.TestCase):
    def setUp(self):
        self.Sensor, self.metric = load_sensor()
        self.hub = SimpleNamespace(devices={'bresser': {'online': True}})

    def entity(self, metric):
        return self.Sensor(self.hub, 'bresser', self.metric(metric), {'name':'Bresser Weather'}, None)

    def test_weather_units_and_original_identity(self):
        for metric, unit in [('temperature','°C'),('humidity','%'),('wind_gust','m/s'),('wind_speed','m/s'),('wind_direction','°'),('rainfall','mm')]:
            sensor = self.entity(metric)
            self.assertEqual(sensor._attr_native_unit_of_measurement, unit)
            sensor.set_value(0)
            self.assertEqual(sensor._attr_native_value, 0)
            self.assertEqual(sensor._attr_state_class, 'measurement')
        self.assertEqual(self.entity('temperature')._attr_unique_id, 'etcore_bresser_temp')

    def test_battery_status_does_not_become_battery_percentage(self):
        sensor = self.entity('battery_ok')
        for value, expected in [(True,'OK'),(False,'LOW')]:
            sensor.set_value(value)
            self.assertEqual(sensor._attr_native_value, expected)
        self.assertIsNone(sensor._attr_device_class)
        self.assertIsNone(sensor._attr_native_unit_of_measurement)
        self.assertEqual(self.entity('battery')._attr_native_unit_of_measurement, '%')

    def test_station_id_and_availability(self):
        sensor = self.entity('sensor_id');sensor.set_value('97484741')
        self.assertEqual(sensor._attr_native_value, '97484741')
        self.assertTrue(sensor.available)
        self.hub.devices['bresser']['online'] = False
        self.assertFalse(sensor.available)


if __name__ == '__main__':unittest.main()
