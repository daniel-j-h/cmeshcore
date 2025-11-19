# cmeshcore

Tiny self-contained library for talking to MeshCore companion devices via USB-C.


# Overview

cmeshcore is a small C99 library implementing the radio usb companion firmware protocol.

Scope
1. Useful minimal features such as sending and receiving messages
2. Linux only e.g. platform specifics for serial communication
3. Serial communication via USB only; no Bluetooth support
4. No support guarantees; this is my personal side project

Note: This library requires the "radio usb" companion firmware flashed on the device; the "radio ble" firmware does not support serial communication.


# Usage

See the example and the library header.

```c
// create a new serial mesh device
cmeshcore_s mesh = cmeshcore_new(port);

// interact with the device via serial
cmeshcore_send_msg_txt(mesh, pk, msg);

// clean up the mesh device
cmeshcore_free(mesh);
```


# Building

To compile the library

```bash
make
```

To install the library

```bash
make install
```

Then
1. compile your own program against `cmeshcore.h`
2. link your own program against `libcmeshcore.so`


# Example

The example sends a short text message to a node identified by public key prefix.
You can build and run the example

```bash
make

./cmeshcore/cmeshcore-cli /dev/serial/by-id/usb-Heltec..
```

To debug the serial communication

```bash
strace -e open,read,write,close ./cmeshcore-cli /dev/serial/by-id/usb-Heltec..
```


## License

Copyright © 2025 Daniel J. Hofmann

Distributed under the MIT License (MIT).
