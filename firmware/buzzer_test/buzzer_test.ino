#define PIN_BUZZER 27

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
}

void loop() {
  for (int i = 0; i < 3; ++i) {
    tone(PIN_BUZZER, 2800);
    delay(300);
    noTone(PIN_BUZZER);
    delay(300);
  }
  delay(2000);
}
