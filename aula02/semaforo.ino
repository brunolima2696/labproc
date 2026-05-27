

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
}

// the loop function runs over and over again forever
void loop() {
  rgbLedWrite(RGB_BUILTIN, 0, 64, 0);
  delay(3000);                  
  rgbLedWrite(RGB_BUILTIN, 64, 0, 0);
  delay(4000);
  rgbLedWrite(RGB_BUILTIN, 64, 64, 0);  
  delay(1000);              
}
