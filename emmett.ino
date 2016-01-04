/**                                __    __
 *    ____   _____   _____   _____/  |__/  |_
 *  _/ __ \ /     \ /     \_/ __ \   __\   __\
 *  \  ___/|  Y Y  \  Y Y  \  ___/|  |  |  |
 *   \___  >__|_|  /__|_|  /\___  >__|  |__|
 *       \/      \/      \/     \/
 * openEmmett
 * DIY one-wheel, self-balancing, electric skateboard. AKA open-source hoverboard.
 * https://github.com/doublejosh/emmett
 * v0.1.4
 *
 * Arduinos tested: Leonardo, Nano
 * Accelerometer: ADXL345
 */

// Communicate with the sensor.
#include <SPI.h>

// Main adjustments.
const float minPowerAngle    = 2;
const float maxPowerAngle    = 26;
const float powerDelayAngle  = 9;
const float powerDelay       = 20; // @todo Switch to time.
const float powerOffDelay    = 60;
const float maxTiltAngle     = 33;
const float minThrottleVolts = 2;
const float maxThrottleVolts = 2.55;
const float angleAdjust      = 0.6;
const float tiltAdjust       = -4.09;
const float pwmAdjust        = 0;
const unsigned int readDelay = 1;

const boolean debugging      = true;

// Setup alternate pins for Trinket (Adafruit micro-Arduino).
//#if defined (__AVR_ATtiny85__)
//#endif

// Pin configurations.
const unsigned int THROTTLE = 5; // Throttle control pin.
const unsigned int REVERSE  = 4; // Reverse relay pin.
const unsigned int LIMITER  = 1; // Power limiter.
const unsigned int CS       = 3; // Chip Select signal pin.
// SCK -> SCL
// MISO -> SDO
// MOSI -> SDA

// ADXL345 registers.
const char POWER_CTL = 0x2D;
const char DATA_FORMAT = 0x31;
//const char DATAX0 = 0x32; //X-Axis Data 0
//const char DATAX1 = 0x33; //X-Axis Data 1
//const char DATAY0 = 0x34; //Y-Axis Data 0
//const char DATAY1 = 0x35; //Y-Axis Data 1
//const char DATAZ0 = 0x36; //Z-Axis Data 0
//const char DATAZ1 = 0x37; //Z-Axis Data 1
// Some Arduinos supposedly require alternate data addresses.
const char DATAX0 = 0xB2; //X-Axis Data 0
const char DATAX1 = 0xB3; //X-Axis Data 1
const char DATAY0 = 0xB4; //Y-Axis Data 0
const char DATAY1 = 0xB5; //Y-Axis Data 1
const char DATAZ0 = 0xB6; //Z-Axis Data 0
const char DATAZ1 = 0xB7; //Z-Axis Data 1
const float BOARD_VOLTAGE  = 5; // Arduino voltage.
const int SERIAL_RATE = 19200; //115200; //9600;

// Internal globals.
int x, y, z; // Accelerometer axis values.
unsigned char values[10]; // Buffer for accelerometer values.
float angles[5]; // Rolling average.
unsigned int readIndex = 0; // Floating average sensor angle.
int rideStatus = 0; // Current device state.
unsigned int powerDelayCycles = 0; // Power start counter.
unsigned int powerOffCycles = 0; // Power off counter.
float data[2]; // Processed sensor data for application consumption.
unsigned long previousMillis = 0; // Store time.
const unsigned long LONG_RESET = 2147483000; // Careful with variable value.


// Statup.
void setup() {
  // Initiate and configure the SPI communication.
  SPI.begin();
  SPI.setDataMode(SPI_MODE3);

  // Create a serial connection for debugging.
  if (debugging) {
    Serial.begin(SERIAL_RATE);
  }

  // Set the Chip Select for output.
  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH);

  // Setup motor limiter.
  pinMode(LIMITER, INPUT);
  
  // Set the motor speed throttle via PWM.
  pinMode(THROTTLE, OUTPUT);

  // Set the reverse relay for output.
  pinMode(REVERSE, OUTPUT);
  digitalWrite(REVERSE, LOW);

  // Put ADXL345 into +/- 4G range by writing the value 0x01 to the DATA_FORMAT register.
  writeRegister(DATA_FORMAT, 0x01);

  //Put the ADXL345 into Measurement Mode by writing 0x08 to the POWER_CTL register.
  writeRegister(POWER_CTL, 0x08);
}

// Continue.
void loop() {
//  unsigned long currentMillis = millis();
//  
//  // Time difference.
//  if (currentMillis - previousMillis >= interval) {
//    // Save time.
//    previousMillis = currentMillis;
//
//  }
//  if (previousMillis >= LONG_RESET) {
//    previousMillis = 0;
//  }

  powerMotor(manageRide(findAngles(data)));
  delay(readDelay);
}


/**
 * Use sensor data for overall UX.
 *
 * 0 = resting
 * 1 = on
 * ...more to come.
 */
float manageRide(float *data) {
  float angle = data[0];
  float angleSize = abs(angle);

  // Shut down when tilted out-of-bounds.
  if (abs(data[1]) > maxTiltAngle) {
    if (debugging) {
      Serial.print("TILT! -- ");
    }
    rideStatus = 0;
    // @todo Duplicated.
    powerOffCycles = 0;
    powerDelayCycles = 0;
    return 0;
  }

  // Already on.
  if (rideStatus == 1) {
    // Within acceleration bounds.
    if (angleSize > minPowerAngle && angleSize < maxPowerAngle) {
      return angle;
    }
    // Level, glide.
    else if (angleSize < minPowerAngle) {
      return 0;
    }
    // Non-riding position.
    else {
      // Allow turning off for later power delay.
      if (powerOffCycles >= (powerOffDelay / readDelay)) {
        // Begin off state.
        rideStatus = 0;
        powerOffCycles = 0;
        powerDelayCycles = 0;
      }
      powerOffCycles++;
      return 0;
    }
  }
  else if (angleSize < powerDelayAngle) {
    // Off, but level enough to start...

    if (debugging) {
      Serial.print(" --- [ ");
      Serial.print(powerDelayCycles);
      Serial.print(" | ");
      Serial.print(powerDelay);
      Serial.print(" | ");
      Serial.print(readDelay);
      Serial.print(" ]");
    }

    // Allow crossing power delay.
    if (powerDelayCycles >= (powerDelay / readDelay)) {
      // Riding start state.
      rideStatus = 1;
      powerDelayCycles = 0;
      powerOffCycles = 0;
      return angle;
    }
    powerDelayCycles++;
    return 0;
  }
  else {
    // Non-use state.
    powerDelayCycles = 0;
    return 0;
  }
}

/**
 * Send power to motor. 0  = off, >0 = ride angle
 */
void powerMotor(float angle) {
  // Avoid computation when ride is off.
  if (angle != 0) {

    // Find desired voltage.
    float voltage = mapFloat(abs(angle), minPowerAngle, maxPowerAngle, minThrottleVolts, maxThrottleVolts);

    // Allow adjusting power.
    float powerLimit = 1.0; //map(analogRead(LIMITER), 0, 1063, 0, 1.0);

    // Find PWM output value.
    // @todo Use power limiter, powerLimit.
    float pwm = (255 * (voltage / BOARD_VOLTAGE)) + pwmAdjust;
    analogWrite(THROTTLE, pwm);

    // Set reverse.
    if (angle > 1) {
      digitalWrite(REVERSE, HIGH);
    }
    else {
      digitalWrite(REVERSE, LOW);
    }

    // Debugging.
    if (debugging) {
      Serial.print(" PWM: ");
      Serial.print(pwm);
      Serial.print(" ");
      Serial.print(voltage);
      Serial.print("v -- angle: ");
      Serial.print(angle);
      Serial.print(" -- status: ");
      Serial.println(rideStatus);
    }
  }
  else {
    // Ouput PWM off.
    analogWrite(THROTTLE, LOW);

    if (debugging) {
      Serial.print("OFF");
      Serial.print(" ----- status: ");
      Serial.println(rideStatus);
    }
  }
}

/**
 * Determine orientation of device.
 */
float* findAngles(float *data) {
  // Reading 6 bytes of data starting at register DATAX0 will retrieve the x,y and z acceleration values
  // from the ADXL345. The results of the read operation will get stored to the values[] buffer.
  readRegister(DATAX0, 6, values);

  // The ADXL345 gives 10-bit acceleration values, but they are stored as bytes (8-bits).
  // To get the full value, two bytes must be combined for each axis.
  x = ((int)values[1]<<8) | (int)values[0];
  y = ((int)values[3]<<8) | (int)values[2];
  z = ((int)values[5]<<8) | (int)values[4];

  // Calculate and angle between -360 and 360.
  // = atan2(x, z) * 180.0f / M_PI;
  // Overwrite new value.
  angles[readIndex] = (atan2((- x) , sqrt(y * y + z * z)) * 57.3) + angleAdjust;
  // Use average.
  data[0] = (angles[0] + angles[1] + angles[2] + angles[3] + angles[4]) / 5;
  readIndex++;
  if (readIndex >= 5) {
    readIndex = 0;
  }

  // Calculate tilt for out-of-bounds/crash sensing.
  //data[1] = atan2(y, z) * 180.0f / M_PI;
  data[1] = (atan2(y , z) * 57.3) + tiltAdjust;

  // Filter data (alternate sensor filter option).
  //  xf = FILTER_TIME * xf + (1.0 - FILTER_TIME) * x;
  //  yf = FILTER_TIME * yf + (1.0 - FILTER_TIME) * y;

  if (debugging) {
    Serial.print(x);
    Serial.print(", ");
    Serial.print(y);
    Serial.print(", ");
    Serial.print(z);

    Serial.print(" Pitch: ");
    Serial.print(data[0]);
    Serial.print(" - Roll: ");
    Serial.print(data[1]); 
    Serial.print(" - ");
  }

  return data;
}

/**
 * Map floats.
 */
float mapFloat(float x, float inMin, float inMax, float outMin, float outMax) {
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

/**
 * Write a value to a register on the ADXL345.
 *
 * char registerAddress - The register to write a value to
 * char value           - The value to be written to the specified register.
 */
void writeRegister(char registerAddress, char value) {
  // Signal beginning of an SPI packet.
  digitalWrite(CS, LOW);
  // Transfer the register address, then value.
  SPI.transfer(registerAddress);
  SPI.transfer(value);
  // Signal end of an SPI packet.
  digitalWrite(CS, HIGH);
}

/**
 * Read registers starting from address, store values in buffer.
 *
 * char registerAddress - Register addresses to start the read sequence.
 * int  numBytes        - Number of registers to read.
 * char * values        - Pointer to a buffer where operation results should be stored.
 */
void readRegister(char registerAddress, int numBytes, unsigned char * values) {
  // Since we're performing a read operation, the most significant bit of the register address should be set.
  char address = 0x80 | registerAddress;
  // If we're doing a multi-byte read, bit 6 needs to be set as well.
  if (numBytes > 1) address = address | 0x40;

  // Set the Chip select pin low to start an SPI packet.
  digitalWrite(CS, LOW);
  // Transfer the starting register address that needs to be read.
  SPI.transfer(address);
  // Continue to read registers until we've read the number specified, storing the results to the input buffer.
  for (int i=0; i<numBytes; i++) {
    values[i] = SPI.transfer(0x00);
  }
  // Set the Chips Select pin high to end the SPI packet.
  digitalWrite(CS, HIGH);
}
