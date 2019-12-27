#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#include "BLE2902.h"
#include "BLEHIDDevice.h"
#include "HIDTypes.h"

#define JOYSTICK_MODE               // Define to report device as a Joystick, undefine to report device as Gamepad
#undef ADD_KEYBOARD                 // Define to add a 2nd keyboard device to the gamepad/joystick (for special keys etc.)

#define DEBOUNCE_MS     10          // Will must see 10 ms of continuous not-pressed input before we will report it.
#define CONNECT_LED_PIN 2           // Use built-in LED to dislay connection state

// Map GPIO pins to buttons and axis inputs. Bit 0 = button 1, bit 1 = button 2, etc. Bit 31, 30, 29, 28= Up, Down, Left, Right
#define GPIO39_MAP    0x00000000    // .
#define GPIO36_MAP    0x00000000    // .
#define GPIO35_MAP    0x00000000    // .
#define GPIO34_MAP    0x00000000    // .
#define GPIO33_MAP    0x00010000    // Right
#define GPIO32_MAP    0x00000000    // .
#define GPIO27_MAP    0x00080000    // Up
#define GPIO26_MAP    0x00040000    // Down
#define GPIO25_MAP    0x00020000    // Left
#define GPIO23_MAP    0x00000001    // button 1
#define GPIO22_MAP    0x00000002    // button 2
#define GPIO21_MAP    0x00000004    // button 3
#define GPIO19_MAP    0x00000008    // button 4
#define GPIO18_MAP    0x00000010    // button 5
#define GPIO17_MAP    0x00000020    // button 6
#define GPIO16_MAP    0x00000040    // button 7 
#define GPIO13_MAP    0x00000000    // .
#define GPIO4_MAP     0x00000080    // button 8

BLEHIDDevice*       hid;
BLECharacteristic*  input;
#ifdef ADD_KEYBOARD
BLECharacteristic*  input2;
BLECharacteristic*  output2;
#endif

bool      connected = false;
uint32_t  current_state = 0;    // current (debounced) mapped input state
int       next_zero_ms[32];     // next millisec timestamp each bit is allowed to be zeroed

// MAME4Droid 0.139 sees buttons as A, B, C, X, Y, Z, L1, R1, L2, R2, SELECT, START, MODE, THUMBL, THUMBR, unknown
const uint8_t report[] = {
    USAGE_PAGE(1),      0x01,       // Generic Desktop Ctrls
#ifdef JOYSTICK_MODE
    USAGE(1),           0x04,       // Joystick
#else
    USAGE(1),           0x05,       // Gamepad
#endif
    COLLECTION(1),      0x01,       // Application
    REPORT_ID(1),       0x01,       //   Report ID (1)
    USAGE_PAGE(1),      0x09,       //   Buttons...
    USAGE_MINIMUM(1),   0x01,       //   Minimum - button 1
    USAGE_MAXIMUM(1),   0x10,       //   Maximum - button 16
    LOGICAL_MINIMUM(1), 0x00,       //   Logical minimum 0 (not pressed)
    LOGICAL_MAXIMUM(1), 0x01,       //   Logical maximum 1 (pressed)
    REPORT_SIZE(1),     0x01,       //   1 bit per button
    REPORT_COUNT(1),    0x10,       //   16 buttons to report on (two bytes in total)
    HIDINPUT(1),        0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position
    USAGE_PAGE(1),      0x01,       //   Generic Desktop Ctrls
    USAGE(1),           0x30,       //   Usage (x-axis)
    USAGE(1),           0x31,       //   Usage (y-axis)
    LOGICAL_MINIMUM(1), 0x81,       //   Logical minimum -127
    LOGICAL_MAXIMUM(1), 0x7F,       //   Logical maximum 127
    REPORT_SIZE(1),     0x08,       //   1 byte per axis
    REPORT_COUNT(1),    0x02,       //   2 axis in the report (2 bytes in total)
    HIDINPUT(1),        0x02,       //   Data,Var,Abs,X & Y positions
    END_COLLECTION(0),
#ifdef ADD_KEYBOARD
    // composite keyboard device
    USAGE_PAGE(1),      0x01,       // Generic Desktop Ctrls
    USAGE(1),           0x06,       // Keyboard
    COLLECTION(1),      0x01,       // Application
    REPORT_ID(1),       0x02,       //   Report ID (2)
    USAGE_PAGE(1),      0x07,       //   Usage Page (Key Codes)
    USAGE_MINIMUM(1),   0xE0,
    USAGE_MAXIMUM(1),   0xE7,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1),     0x01,       //   1 byte (Modifiers)
    REPORT_COUNT(1),    0x08,
    HIDINPUT(1),        0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position
    REPORT_COUNT(1),    0x01,       //   1 byte (Reserved)
    REPORT_SIZE(1),     0x08,
    HIDINPUT(1),        0x01,       //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
    REPORT_COUNT(1),    0x06,       //   6 bytes (Keys)
    REPORT_SIZE(1),     0x08,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x65,       //   101 keys
    USAGE_MINIMUM(1),   0x00,
    USAGE_MAXIMUM(1),   0x65,
    HIDINPUT(1),        0x00,       //   Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
    REPORT_COUNT(1),    0x05,       //   5 bits (Num lock, Caps lock, Scroll lock, Compose, Kana)
    REPORT_SIZE(1),     0x01,
    USAGE_PAGE(1),      0x08,       //   LEDs
    USAGE_MINIMUM(1),   0x01,       //   Num Lock
    USAGE_MAXIMUM(1),   0x05,       //   Kana
    HIDOUTPUT(1),       0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
    REPORT_COUNT(1),    0x01,       //   3 bits (Padding)
    REPORT_SIZE(1),     0x03,
    HIDOUTPUT(1),       0x01,       //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
    END_COLLECTION(0)
#endif
};


class ServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer* pServer)
    {
     connected = true;
     digitalWrite(CONNECT_LED_PIN, HIGH);
     BLE2902* desc = (BLE2902*)input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
     desc->setNotifications(true);
    }

    void onDisconnect(BLEServer* pServer)
    {
     connected = false;
     digitalWrite(CONNECT_LED_PIN, LOW);
     BLE2902* desc = (BLE2902*)input->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
     desc->setNotifications(false);
    }
};

#ifdef ADD_KEYBOARD
class OutputCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic* me)
    {
     // uint8_t* value = (uint8_t*)(me->getValue().c_str());
     // printf("special keys: %d", *value);
    }
};
#endif

void setup()
{
 Serial.begin(115200);
 printf("Starting BLE Gamepad device\n!");

 BLEDevice::init("ESP32-Gamepad");
 BLEServer *pServer = BLEDevice::createServer();
 pServer->setCallbacks(new ServerCallbacks());

 hid = new BLEHIDDevice(pServer);
 input = hid->inputReport(1); // <-- input REPORTID 1 from report map

#ifdef ADD_KEYBOARD
 input2 = hid->inputReport(2); // <-- input REPORTID 2 from report map
 output2 = hid->outputReport(2); // <-- output REPORTID 2 from report map
 output2->setCallbacks(new OutputCallbacks());
#endif

 BLESecurity *pSecurity = new BLESecurity();
 pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);

 std::string name = "DeonVanDerWesthuysen";
 hid->manufacturer()->setValue(name);
 hid->pnp(0x02, 0xADDE, 0xEFBE, 0x0100);    // High and low bytes of words get swapped
 hid->hidInfo(0x00, 0x02);
 hid->reportMap((uint8_t*)report, sizeof(report));
 hid->startServices();

 BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
#ifdef JOYSTICK_MODE
 pAdvertising->setAppearance(HID_JOYSTICK);
#else
 pAdvertising->setAppearance(HID_GAMEPAD);
#endif
 pAdvertising->addServiceUUID(hid->hidService()->getUUID());
 pAdvertising->start();
 hid->setBatteryLevel(100);

 // Initialise next zero-able time for each bit to 0 ms.
 memset (next_zero_ms, 0, sizeof(next_zero_ms));

 // Setup joystick input pins.
 pinMode(39, INPUT_PULLUP);         // no internal pull-up available
 pinMode(36, INPUT_PULLUP);         // no internal pull-up available
 pinMode(35, INPUT_PULLUP);         // no internal pull-up available
 pinMode(34, INPUT_PULLUP);         // no internal pull-up available
 pinMode(33, INPUT_PULLUP);         //
 pinMode(32, INPUT_PULLUP);         //
 pinMode(27, INPUT_PULLUP);         //
 pinMode(26, INPUT_PULLUP);         //
 pinMode(25, INPUT_PULLUP);         //
 pinMode(23, INPUT_PULLUP);         //
 pinMode(22, INPUT_PULLUP);         //
 pinMode(21, INPUT_PULLUP);         //
 pinMode(19, INPUT_PULLUP);         //
 pinMode(18, INPUT_PULLUP);         //
 pinMode(17, INPUT_PULLUP);         //
 pinMode(16, INPUT_PULLUP);         //
 pinMode(13, INPUT_PULLUP);         //
 pinMode(4, INPUT_PULLUP);          //

 pinMode(CONNECT_LED_PIN, OUTPUT);
 printf ("Init complete!\n");
}

void loop()
{
 uint32_t input_state = 0;             // Will contain a 1 bit for each mapped button press.
 uint32_t next_state = current_state;  // Update current inputs with debounced input
 uint32_t changed;                     // Bits that changed since update
 int      count;
 int      mask = 1;
 int      now = millis();

 // Add value of each mapped button/input
 if (digitalRead(39)==LOW) input_state |= GPIO39_MAP;
 if (digitalRead(36)==LOW) input_state |= GPIO36_MAP;
 if (digitalRead(35)==LOW) input_state |= GPIO35_MAP;
 if (digitalRead(34)==LOW) input_state |= GPIO34_MAP;
 if (digitalRead(33)==LOW) input_state |= GPIO33_MAP;
 if (digitalRead(32)==LOW) input_state |= GPIO32_MAP;
 if (digitalRead(27)==LOW) input_state |= GPIO27_MAP;
 if (digitalRead(26)==LOW) input_state |= GPIO26_MAP;
 if (digitalRead(25)==LOW) input_state |= GPIO25_MAP;

 if (digitalRead(23)==LOW) input_state |= GPIO23_MAP;
 if (digitalRead(22)==LOW) input_state |= GPIO22_MAP;
 if (digitalRead(21)==LOW) input_state |= GPIO21_MAP;
 if (digitalRead(19)==LOW) input_state |= GPIO19_MAP;
 if (digitalRead(18)==LOW) input_state |= GPIO18_MAP;
 if (digitalRead(17)==LOW) input_state |= GPIO17_MAP;
 if (digitalRead(16)==LOW) input_state |= GPIO16_MAP;
 if (digitalRead(13)==LOW) input_state |= GPIO13_MAP;
 if (digitalRead(4)==LOW) input_state |= GPIO4_MAP;

 for (count = 0; count < 32; count++)
 {
  if (input_state & mask)
  {
   // Button pressed - set button bit and update the timestamp when input may be reported as zero
   next_state |= mask;
   next_zero_ms[count] = now + DEBOUNCE_MS;
  }
  else if (now > next_zero_ms[count])
  {
   // Button was not pressed and we reached the end of the debounce period.
   //   If button was pressed during debounce period the timestamp would have been updated.
   //   This logic fails for the first DEBOUNCE_MS after timer roll-over but auto recovers.
   next_state &= (~mask);
  }
  mask<<= 1;
 }

 // Send a HID report if there is a change in the current debounced state
 changed= current_state ^ next_state;
 if (changed)
 {
  uint8_t   hidreport[] = {0x0, 0x0, 0x0, 0x0};
#ifdef ADD_KEYBOARD
  uint8_t   hidreport2[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}; // Modifier, Reserved, 6 key bytes
  int       idx;
#endif

  hidreport[0]= next_state & 0xFF;
  hidreport[1]= (next_state>>8) & 0xFF;
  if (next_state&0x00020000) hidreport[2]-= 127;   // Left button
  if (next_state&0x00010000) hidreport[2]+= 127;   // Right button
  if (next_state&0x00080000) hidreport[3]-= 127;   // Up button
  if (next_state&0x00040000) hidreport[3]+= 127;   // Down button
  //printf ("report values %02X %02X %02X %02X ", hidreport[0], hidreport[1], hidreport[2], hidreport[3]);

#ifdef ADD_KEYBOARD
  idx= 2;   // First index of keyboard hid report
  // sample key mapping - update to match your requirements
  if ((idx<sizeof(hidreport2)) && (next_state&0x01000000)) hidreport2[idx++]= 4;    // 'a'
  if ((idx<sizeof(hidreport2)) && (next_state&0x02000000)) hidreport2[idx++]= 5;    // 'b'
  if ((idx<sizeof(hidreport2)) && (next_state&0x04000000)) hidreport2[idx++]= 6;    // 'c'
  if ((idx<sizeof(hidreport2)) && (next_state&0x08000000)) hidreport2[idx++]= 7;    // 'd'
  if ((idx<sizeof(hidreport2)) && (next_state&0x10000000)) hidreport2[idx++]= 8;    // 'e'
  if ((idx<sizeof(hidreport2)) && (next_state&0x20000000)) hidreport2[idx++]= 9;    // 'f'
  if ((idx<sizeof(hidreport2)) && (next_state&0x40000000)) hidreport2[idx++]= 10;    // 'g'
  if ((idx<sizeof(hidreport2)) && (next_state&0x80000000)) hidreport2[idx++]= 11;    // 'h'
  //printf ("report2 values %02X %02X %02X %02X %02X %02X %02X %02X\n", hidreport2[0], hidreport2[1], hidreport2[2], hidreport2[3], hidreport2[4], hidreport2[5], hidreport2[6], hidreport2[7]);
#endif
   
  if (connected)
  {
   if (changed & 0x000FFFFF)  // If bits mapped into Joystick changed, send joystick report
   {
    input->setValue(hidreport, sizeof(hidreport));
    input->notify();
   }
#ifdef ADD_KEYBOARD
   if (changed & 0xFFF00000)  // If bits mapped into Keyboard changed, send keyboard report
   {
    input2->setValue(hidreport2,sizeof(hidreport2));
    input2->notify();
   }
#endif   
  }
  current_state= next_state;
 }
}

