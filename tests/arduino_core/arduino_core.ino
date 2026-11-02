// The Arduino core on wasm-rtos: Serial, String, pins, an interrupt,
// Wire, SPI and time, as a classic sketch uses them.
#include <SPI.h>
#include <Wire.h>

volatile int presses = 0;
void pressed() { presses++; }

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(0, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(0), pressed, FALLING);
  Wire.begin();
  SPI.begin();

  Serial.println(F("hello"));
  String text = "Temp";
  text += 42;
  text.toUpperCase();
  Serial.println(text);
  Serial.println(String(3.14159, 3));
  Serial.println(255, HEX);
  Serial.println(-12);
  Serial.printf("%d-%s\n", 7, "x");
  Serial.println(String("  padded ").substring(2, 8) + "|" + String(text.indexOf("42")));
  Serial.println(map(512, 0, 1023, 0, 255));
  Serial.println(constrain(300, 0, 255));
}

void loop() {
  static int round = 0;
  digitalWrite(LED_BUILTIN, round % 2 == 0 ? HIGH : LOW);

  // A register read: the pointer, then two bytes after a repeated start.
  Wire.beginTransmission(0x48);
  Wire.write(0x00);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x48, 2) == 2) {
    int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
    Serial.print("temperature ");
    Serial.println(raw / 256.0, 2);
  }
  Wire.beginTransmission(0x50);
  Serial.print("no device: ");
  Serial.println(Wire.endTransmission());

  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  uint8_t reply = SPI.transfer(0x5A);
  uint16_t reply16 = SPI.transfer16(0x1234);
  SPI.endTransaction();
  Serial.print("spi 0x");
  Serial.print(reply, HEX);
  Serial.print(" 0x");
  Serial.println(reply16, HEX);

  unsigned long before = millis();
  delay(100);
  Serial.print("slept ");
  Serial.print(millis() - before >= 100 ? "enough" : "too little");
  Serial.print(", presses ");
  Serial.println(presses);

  if (++round == 2) {
    Serial.flush();
    exit(0);
  }
}
