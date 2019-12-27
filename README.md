# ESP32_GAMEPAD
ESP32 Gamepad/Joystick interface using HID over BLE.

Features:
* Code can be compiled to either create and HID joystick or gamepad. Currently the configuration is fixed with 2 axes and 16 buttons.
* Easy to configure the mapping of GPIO pins to specific buttons. It is possible to skip buttons - for example to map GPIOs to buttons 1, 2, 5, 6 and skip buttons 3 and 4.
* Debounce of input to avoid accdental button presses. Button pressed as immediately reported, for a release to be registered the button has to remain in a released state for 10ms.
* Suports creating an additional keyboard device in order to report key presses. Disabled by default.

Default pin mapping:
* Up/Down/Left/Right: GPIO 27/26/25/33
* Button 1-8: GPIO 23, 22, 21, 19, 18, 17, 16, 4
