/*

Arduino code for controlling a servo via calls to an HTTP
servo with the CC3000 wifi chip.

*/

#include <Servo.h>

// Create servo object
Servo myservo;

// Servo position
int pos = 0;

// Connections
const int SERVO_PIN = 9;
const int BUTTON_PIN = 8;

void setup()
{
  myservo.attach(SERVO_PIN);  // attaches the servo on pin 9 to the servo object
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop()
{
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (pos == 0) {
      pos = 180;
    } else {
      pos = 0;
    }
    myservo.write(pos);
    delay(200);
  }
  delay(15);
}
