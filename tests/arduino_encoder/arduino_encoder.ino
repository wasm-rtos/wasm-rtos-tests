// The classic Encoder API on wasm-rtos: the hardware counts the knob while
// the sketch sleeps; write sets the position, readAndReset loses no step.
#include <Encoder.h>

Encoder knob(18, 19);

void setup() {
  Serial.begin(115200);
}

void loop() {
  static int32_t last = -999;
  static int rounds = 0;
  int32_t now = knob.read();
  if (now != last) {
    Serial.print("position ");
    Serial.println(now);
    last = now;
  }
  rounds++;
  if (rounds == 3) {
    knob.write(100);
    Serial.println(knob.read());
  }
  if (rounds == 5) {
    Serial.print("reset ");
    Serial.println(knob.readAndReset());
    Serial.println(knob.read());
    exit(0);
  }
  delay(10);
}
