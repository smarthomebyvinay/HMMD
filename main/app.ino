// Example device behavior: blink the board LED and report each transition.
// Do not define setup() or loop(); main.ino owns those and calls these hooks.

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

static bool ledIsOn = false;
static unsigned long lastLedChange = 0;
static const unsigned long LED_INTERVAL_MS = 5000;

void app_setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  ledIsOn = true;
  lastLedChange = millis();
  Serial.println("LED ON");
}

void app_loop() {
  if (millis() - lastLedChange >= LED_INTERVAL_MS) {
    lastLedChange = millis();
    ledIsOn = !ledIsOn;
    digitalWrite(LED_BUILTIN, ledIsOn ? HIGH : LOW);
    Serial.println(ledIsOn ? "LED ON" : "LED OFF");
  }
}
