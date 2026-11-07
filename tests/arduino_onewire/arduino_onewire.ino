// Arduino's OneWire API on wasm-rtos, as DS18B20 sketches use it: search
// the bus, start at a family, find alarms, convert every sensor at once on
// parasite power, read scratchpads with their CRCs, poll with single bits.
#include <OneWire.h>

OneWire ds(4);

void printAddress(const uint8_t* rom) {
  for (int i = 0; i < 8; i++) {
    if (rom[i] < 16) Serial.print('0');
    Serial.print(rom[i], HEX);
  }
}

void setup() {
  Serial.begin(115200);
  uint8_t rom[8];
  int count = 0;
  while (ds.search(rom)) {
    printAddress(rom);
    Serial.println(OneWire::crc8(rom, 7) == rom[7] ? " ok" : " bad crc");
    count++;
  }
  Serial.print("devices ");
  Serial.println(count);

  ds.target_search(0x28);
  if (ds.search(rom)) {
    Serial.print("first sensor ");
    printAddress(rom);
    Serial.println();
  }
  ds.reset_search();
  while (ds.search(rom, false)) {
    Serial.print("alarm ");
    printAddress(rom);
    Serial.println();
  }
}

void loop() {
  // Every sensor converts at once; the strong pull-up powers the parasite one.
  if (!ds.reset()) {
    Serial.println("no presence");
    exit(1);
  }
  ds.skip();
  ds.write(0x44, 1);
  delay(750);
  ds.depower();

  uint8_t rom[8];
  ds.reset_search();
  while (ds.search(rom)) {
    if (rom[0] != 0x28) continue;
    uint8_t data[9];
    ds.reset();
    ds.select(rom);
    ds.write(0xBE);
    ds.read_bytes(data, 9);
    printAddress(rom);
    if (OneWire::crc8(data, 8) != data[8]) {
      Serial.println(" crc error");
      continue;
    }
    int16_t raw = (int16_t)((data[1] << 8) | data[0]);
    Serial.print(" ");
    Serial.println(raw / 16.0, 4);
  }

  // Without the pull-up a sensor answers read slots with 1 once it is done.
  ds.reset();
  ds.skip();
  ds.write(0x44);
  int polls = 0;
  while (!ds.read_bit() && polls < 100) polls++;
  Serial.print("converted after ");
  Serial.print(polls);
  Serial.println(" polls");

  static const uint8_t check[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
  Serial.println(OneWire::crc16(check, 9), HEX);
  exit(0);
}
