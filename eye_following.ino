/*
Creepy "Eye-Following" Portrait
By: Kevin Killingsworth
October 12, 2022

This is the code for a creepy portrait I've wanted to make for a while.
It uses a low-res (8x8) thermal camera to see humans in the room,
and a servo which causes the eyes to follow heat through the room.

Thanks to SparkFun and Nick Poole for the Grid-EYE Sensor code!
*/

#include <SparkFun_GridEYE_Arduino_Library.h>
#include <Wire.h>
#include <Servo.h>

#define SERVO_GPIO 26
#define SERVO_CENTER 90

#define MAX_NEGATIVE_RECALIBRATE_THRESHOLD 100
#define MIN_TOTAL_EYE_FOLLOW_THRESHOLD 200
#define MIN_COLUMN_EYE_FOLLOW_THRESHOLD 50
#define MAX_POSITION_STEPS 2
#define TO_ACTIVE_DELAY_MS 500
#define TO_INACTIVE_DELAY_MS 5000

#define MODE_INACTIVE 0
#define MODE_GOING_ACTIVE 1
#define MODE_GOING_INACTIVE 2
#define MODE_ACTIVE 3

int totalDiff;
// Camera Columns        0    -    1    -    2    -    3    |    4    -    5    -    6    -    7
int eyePositions[16] = { 150, 141, 132, 123, 114, 105,  96, 90,  84,  75,  66,  57,  48,  37,  28 };
int columnDiffs[8];
int calibrationTable[64];

int mode = MODE_INACTIVE;
unsigned long lastActiveTransitionMs = 0;
int lastEyePositionIndex = -1;

GridEYE grideye;
Servo servo;

void setup() {
  Wire1.begin();
  grideye.begin(0x69, Wire1);

  servo.attach(SERVO_GPIO);

  Serial.begin(115200);

  calibrate();
}

void updateColumns() {
  int index = 0;

  // Reset all columns to 0
  memset(columnDiffs, 0, sizeof(int) * 8);
  totalDiff = 0;

  for(uint8_t column = 0; column < 8; column++) {
    for(uint8_t row = 0; row < 8; row++) {
      int pixelDiff = grideye.getPixelTemperatureRaw(index) - calibrationTable[index];
      columnDiffs[column] += pixelDiff;
      totalDiff += pixelDiff;
      index++;
    }
  }
}

int calculateHottestColumnIndex() {
  int hottestColumnIndex = -1;
  int hottestValue = 0;
  for (uint8_t i = 0; i < 8; i++) {
    int value = columnDiffs[i];
    if (value > MIN_COLUMN_EYE_FOLLOW_THRESHOLD && value > hottestValue) {
      hottestColumnIndex = i;
      hottestValue = value;
    }
  }
  return hottestColumnIndex;
}

int calculateEyePosition() {
  if (totalDiff < MIN_TOTAL_EYE_FOLLOW_THRESHOLD) {
    Serial.print(" P=NT");
    return -1;
  }

  // Figure out the hottest column, and the second hottest
  int firstColumn = -1;
  int secondColumn = -1;
  int firstValue = 0;
  int secondValue = 0;

  for (uint8_t i = 0; i < 8; i++) {
    int value = columnDiffs[i];
    if (value > firstValue) {
      // Relegate the current first value to the second.
      secondColumn = firstColumn;
      secondValue = firstValue;
      // Assign the new value
      firstColumn = i;
      firstValue = value;
    } else if (value > secondValue) {
      // It's not not enough to replace the first value, but it's good enough for the second.
      secondColumn = i;
      secondValue = value;
    }
  }

  if (firstColumn == -1 || firstValue < MIN_COLUMN_EYE_FOLLOW_THRESHOLD) {
    // We didn't find a column hot enough to look at
    Serial.print(" P=NC");
    return -1;
  }

  if (secondColumn == -1) {
    // There was only one column hot enough, so return that value x2 for the eye position.
    return firstColumn * 2;
  }

  // If we've reached here, we know we have a valid first and second column, so find the midpoint and return it.
  int maxPosition = max(firstColumn, secondColumn) * 2;
  int minPosition = min(firstColumn, secondColumn) * 2;
  int position = ((maxPosition - minPosition) / 2) + minPosition;

  Serial.print(" P=");
  Serial.print(position);
  return position;
}

void setModeInactive() {
  lastActiveTransitionMs = millis();
  mode = MODE_INACTIVE;
}

void setModeGoingActive() {
  lastActiveTransitionMs = millis();
  mode = MODE_GOING_ACTIVE;
}

void setModeGoingInactive() {
  lastActiveTransitionMs = millis();
  mode = MODE_GOING_INACTIVE;
}

void setModeActive() {
  lastActiveTransitionMs = millis();
  mode = MODE_ACTIVE;
}

void updateMode(int eyePositionIndex) {
  unsigned long now = millis();

  switch(mode) {
    case MODE_INACTIVE:
      if (eyePositionIndex >= 0) {
        setModeGoingActive();
        Serial.print(" a!");
      } else {
        Serial.print(" I");
      }
      break;
    case MODE_GOING_ACTIVE:
      if (eyePositionIndex >= 0) {
        if (now > lastActiveTransitionMs + TO_ACTIVE_DELAY_MS) {
          setModeActive();
          Serial.print(" A!");
        } else {
          Serial.print(" a");
        }
      } else {
        setModeInactive();
        Serial.print(" I!");
      }
      break;
    case MODE_GOING_INACTIVE:
      if (eyePositionIndex == -1) {
        if (now > lastActiveTransitionMs + TO_ACTIVE_DELAY_MS) {
          setModeInactive();
          Serial.print(" I!");
        } else {
          Serial.print(" i");
        }
      } else {
        setModeActive();
        Serial.print(" A!");
      }
      break;
    case MODE_ACTIVE:
      if (eyePositionIndex == -1) {
        setModeGoingInactive();
        Serial.print(" i!");
      } else {
        Serial.print(" A");
      }
      break;
    default:
      setModeInactive();
      break;
  }
}

void updateServo(int mode, int eyePositionIndex) {
  int servoPosition;

  switch(mode) {
    case MODE_INACTIVE:
    case MODE_GOING_ACTIVE:
      servoPosition = SERVO_CENTER;
      break;
    case MODE_GOING_INACTIVE:
      servoPosition = eyePositions[lastEyePositionIndex];
      break;
    case MODE_ACTIVE:
      servoPosition = eyePositions[eyePositionIndex];
      break;
    default:
      servoPosition = SERVO_CENTER;
  }

  servo.write(servoPosition);
}

void loop() {
  updateColumns();

  for(unsigned char column = 0; column < 8; column++){
    Serial.print(columnDiffs[column]);
    Serial.print(" ");
  }
  Serial.print(" T=");
  Serial.print(totalDiff);

  int eyePositionIndex = calculateEyePosition();
  if (eyePositionIndex >= 0 && lastEyePositionIndex >= 0 && abs(eyePositionIndex - lastEyePositionIndex) > MAX_POSITION_STEPS) {
    // This eye position is too far from the last one. Disregard this result and continue next time.
    Serial.println();
    return;
  }

  updateMode(eyePositionIndex);
  updateServo(mode, eyePositionIndex);
  lastEyePositionIndex = eyePositionIndex;
  Serial.println();

  if (totalDiff < -MAX_NEGATIVE_RECALIBRATE_THRESHOLD) {
    calibrate();
  }

  delay(100);
}

void calibrate() {
  Serial.println("Calibrating...");
  // loop through all 64 pixels on the device and record the current raw temperature
  for(unsigned char i = 0; i < 64; i++){
    calibrationTable[i] = grideye.getPixelTemperatureRaw(i);
  }  
}
