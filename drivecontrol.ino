#include <EEPROM.h>
#include <TimedAction.h>

#include "pinout.h"
#include "link.h"
#include "drive.h"

#define E_MALLOC 0x00 // A malloc failed (out of memory?)
#define E_TIMEOUT 0x20 // Safety timer expired
#define E_WTF 0x90 // Received an unknown command

#define CMD_JOYSTICK 0x30
#define CMD_CALIBRATE 0x40
#define CMD_GET_CALIBRATION 0x41
#define CMD_SOFTSTOP 0xF0

#define RESP_CALIBRATION 0x41
#define RESP_STATUS 0x42
#define RESP_ERROR 0xEE

Link link = Link(dispatch_packet);
Drive drive = Drive();

boolean sendUpdates = true;

// TimedAction "thread" for the safety timer. Set to 1 second by default
TimedAction timeoutAction = TimedAction(1000, timeout);

void setup() {
  // Adjust timer 1 for higher frequency PWM
  TCCR1B = TCCR1B & 0b11111000 | 0x01; // 31250 Hz
  Serial.begin(115200);
  byte data[1] = {0x01};
  link.sendData(1, data); // Let the controller know we're here
  timeoutAction.disable(); // disable safety timer until needed
}

void loop() {
  timeoutAction.check();
  drive.update(driveStateChange);
  return;
  
}

void serialEvent() {
  link.service();
}

void dispatch_packet(int length, byte* packet) {
  byte len = packet[1] - 1; // we don't need the first payload byte
  byte cmd = packet[2];
  
  byte *data = (byte*) malloc((len) * sizeof(byte));
  
  if (data == NULL) {
    handleError(E_MALLOC);
    return;
  }
  // copy only the payload to a new array
  memcpy(data, packet + 3, len);
  
  switch (cmd) {
    case CMD_JOYSTICK:
      cmd_joystick(len, data);
      break;
    case CMD_CALIBRATE:
      cmd_calibrate(len, data);
      break;
    case CMD_GET_CALIBRATION:
      cmd_get_calibration();
      break;
    case CMD_SOFTSTOP:
      // soft stop
      drive.center();
      break;
    default:
      handleError(E_WTF);
      break;
  }
  free(data);
}

void cmd_joystick(int length, byte* data) {
  // Expects two signed 8-bit values, indicating the desired joystick
  // position relative to center.
  // TODO: This really needs some way to limit output to a defined range,
  //       as voltages outside 1-4V are detected as a joystick fault.
  char xpos = data[0];
  char ypos = data[1];
  
  if ((xpos | ypos) != 0) {
    timeoutAction.enable();
  }
  else {timeoutAction.disable();}
  
  drive.setPosition(xpos, ypos);
  timeoutAction.reset();
  return;
}

void cmd_calibrate(int length, byte* data) {
  switch (data[0]) {
    case 0x00:
      // Set x center
      drive.setXCenter(data[1]);
      break;
    case 0x01:
      // set y center
      drive.setYCenter(data[1]);
      break;
    case 0x10:
      // Write calibration to EEPROM
      drive.storeCalibration();
      break;
  }
}

void cmd_get_calibration() {
  byte* values = drive.getCalibration();
  byte data[5];
  data[0] = RESP_CALIBRATION;
  memcpy(data + 1, values, sizeof(byte) * 4);
  link.sendData(5, data);
}

void timeout() {
  drive.center();
  handleError(E_TIMEOUT);
}

void driveStateChange() {
  if (!sendUpdates) {
    return;
  }
  char *pos = drive.getPosition();
  byte *raw = drive.getRawPosition();
  byte status = drive.getStatus();
  byte data[6];
  data[0] = RESP_STATUS;
  data[1] = status;
  data[2] = pos[0];
  data[3] = pos[1];
  data[4] = raw[0];
  data[5] = raw[1];
  link.sendData(6, data);
}

void handleError(byte errcode) {
  byte data[2];
  data[0] = RESP_ERROR;
  data[1] = errcode;
  link.sendData(2, data);
}
