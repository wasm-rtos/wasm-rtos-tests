// The common NeoPixel API on wasm-rtos: a GRB strip at 800 kHz and an RGBW
// strip at 400 kHz go out through the pulse hardware; brightness scales the
// stored colors; the color helpers match the classic library's results. A
// pin without pulse hardware sends nothing.
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel strip(3, 18, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ring(2, 19, NEO_RGBW + NEO_KHZ400);
Adafruit_NeoPixel plain(1, 20);

void setup() {
  Serial.begin(115200);
  strip.begin();
  ring.begin();
  plain.begin();

  strip.setPixelColor(0, 255, 0, 0);
  strip.setPixelColor(1, Adafruit_NeoPixel::Color(0, 255, 0));
  strip.setPixelColor(2, 0x0000FF);
  strip.setPixelColor(3, 0xFFFFFF);
  strip.show();

  strip.setBrightness(51);
  strip.show();
  strip.fill(strip.Color(10, 20, 30), 1);
  strip.show();

  ring.setPixelColor(0, 1, 2, 3, 4);
  ring.setPixelColor(1, Adafruit_NeoPixel::Color(0, 0, 0, 255));
  ring.show();

  plain.setPixelColor(0, 0xFFFFFF);
  plain.show();

  Serial.println(strip.getPixelColor(0), HEX);
  Serial.println(ring.getPixelColor(1), HEX);
  Serial.println(strip.numPixels());
  Serial.println(strip.getBrightness());
  Serial.println(strip.getPin());
  Serial.println(strip.canShow());
  Serial.println(Adafruit_NeoPixel::ColorHSV(0), HEX);
  Serial.println(Adafruit_NeoPixel::ColorHSV(21845), HEX);
  Serial.println(Adafruit_NeoPixel::ColorHSV(43690, 255, 128), HEX);
  Serial.println(Adafruit_NeoPixel::ColorHSV(10000, 0, 255), HEX);
  Serial.println(Adafruit_NeoPixel::gamma8(128));
  Serial.println(Adafruit_NeoPixel::gamma32(0x80FF4000), HEX);
  Serial.println(Adafruit_NeoPixel::sine8(64));
  Serial.println(Adafruit_NeoPixel::str2order("GRBW") == NEO_GRBW);
  exit(0);
}

void loop() {
}
