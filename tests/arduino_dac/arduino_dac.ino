// analogWrite on a pin with an analog output writes the converter, scaled
// to its resolution; digitalWrite gives the pin back; dacWrite as on the ESP32.
void setup() {
  analogWrite(25, 128);
  analogWriteResolution(12);
  analogWrite(25, 4095);
  digitalWrite(25, HIGH);
  dacWrite(25, 64);
  dacDisable(25);
  exit(0);
}

void loop() {}
