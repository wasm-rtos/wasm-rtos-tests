// Analog in and out, tones and a second serial port in a sketch.
void setup() {
  Serial.begin(115200);
  Serial.println(analogRead(A0));       // 10 bits by default
  analogReadResolution(12);
  Serial.println(analogRead(A0));
  Serial.println(analogRead(0));        // pin 0 means A0 too

  analogWrite(LED_BUILTIN, 64);         // a quarter, 8 bits by default
  delay(10);
  analogWriteResolution(12);
  analogWrite(LED_BUILTIN, 4095);       // fully on
  delay(10);
  digitalWrite(LED_BUILTIN, LOW);       // digital use ends the PWM
  delay(10);

  tone(5, 440, 50);                     // stops by itself after 50 ms
  delay(100);

  Serial1.begin(9600, SERIAL_8E1);
  Serial1.print("ping\n");
  Serial1.flush();
  unsigned long start = millis();
  String reply;
  while (millis() - start < 500) {
    while (Serial1.available() > 0) {
      char c = (char)Serial1.read();
      if (c == '\n') {
        Serial.print("reply ");
        Serial.println(reply);
        Serial.flush();
        exit(0);
      }
      reply += c;
    }
    delay(5);
  }
  Serial.println("no reply");
  Serial.flush();
  exit(1);
}

void loop() {}
