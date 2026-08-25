Linux USB 3.0 Mass Storage Host Driver

A Linux kernel USB host driver developed to study and implement USB 3.x SuperSpeed device communication using the USB Mass Storage Bulk-Only Transport (BBB) protocol and SCSI commands.

Project Overview

This project implements a custom Linux USB kernel driver for USB Mass Storage Class devices using the Bulk-Only Transport protocol.

The driver identifies supported USB mass-storage interfaces, parses USB and SuperSpeed endpoint descriptors, discovers bulk IN/OUT endpoints, and communicates with the device using SCSI commands transported over USB Bulk endpoints.

The project focuses on understanding the Linux USB driver framework, USB 3.x descriptors, USB data transfer, SCSI command transport, error handling, and device-level storage communication.

Features
USB device matching using Vendor/Product ID and USB Mass Storage Class descriptors
Linux USB driver registration using struct usb_driver
USB device enumeration and descriptor inspection
USB 3.x SuperSpeed detection
SuperSpeed Endpoint Companion Descriptor parsing
Bulk IN/OUT endpoint discovery
USB Bulk-Only Transport (BBB)
Command Block Wrapper (CBW) generation
Command Status Wrapper (CSW) validation
SCSI INQUIRY command
SCSI TEST UNIT READY command
SCSI READ CAPACITY(10) command
SCSI REQUEST SENSE command
SCSI READ(10) command
MBR partition-table inspection
USB endpoint STALL detection and recovery
USB device reference-count management
Kernel memory allocation using kzalloc() / kfree()
USB device disconnect handling
USB 3.x Link Power Management status inspection
USB Mass Storage Communication Flow

The driver communicates with the storage device using the Bulk-Only Transport protocol:

Host
 |
 |---- CBW ----------------------> USB Device
 |
 |<--- SCSI Data ----------------- USB Device
 |
 |<--- CSW ----------------------- USB Device
 |
 +---- Command completed


For a SCSI READ(10) operation:

SCSI READ(10)
      |
      v
Create CBW
      |
      v
Bulk OUT
      |
      v
Device reads requested sectors
      |
      v
Bulk IN
      |
      v
Receive CSW
      |
      v
Validate Signature + Tag + Status

Implemented SCSI Commands
Command	Opcode	Purpose
INQUIRY	0x12	Retrieve device identification information
TEST UNIT READY	0x00	Check whether the storage medium is ready
REQUEST SENSE	0x03	Retrieve device error/sense information
READ CAPACITY(10)	0x25	Determine last logical block and block size
READ(10)	0x28	Read logical blocks from the storage device
USB Driver Flow
USB Device Connected
        |
        v
USB Core
        |
        v
my_usb_probe()
        |
        +--> Read Device Descriptor
        |
        +--> Check USB Speed
        |
        +--> Parse Interface Descriptor
        |
        +--> Parse Endpoint Descriptors
        |
        +--> Parse SuperSpeed Companion Descriptor
        |
        +--> Identify Bulk IN / OUT endpoints
        |
        v
SCSI INQUIRY
        |
        v
TEST UNIT READY
        |
        v
READ CAPACITY(10)
        |
        v
READ(10)
        |
        v
Inspect MBR / Partition Entries

Error Handling

The driver checks for:

USB bulk transfer failures
Short transfers
Bulk endpoint STALL conditions
Invalid CSW signatures
CBW/CSW tag mismatches
CSW command failures
SCSI phase errors
Invalid USB device/interface descriptors

When a bulk endpoint reports -EPIPE, the driver attempts endpoint recovery using:

usb_clear_halt()

Kernel Concepts Demonstrated
Linux USB driver framework
struct usb_driver
probe() and disconnect() callbacks
USB device/interface structures
USB endpoint descriptors
SuperSpeed endpoint companion descriptors
USB bulk transfers
usb_bulk_msg()
USB device reference counting
Kernel dynamic memory allocation
Endianness conversion
Kernel logging with printk() / pr_info()
SCSI command construction
USB error handling
Project Status

This project is intended as a learning and experimental USB Mass Storage host driver. It demonstrates low-level USB/SCSI communication but does not implement a complete Linux block-device interface.

It should therefore be considered a custom USB/SCSI communication driver rather than a replacement for the Linux kernel's production USB mass-storage driver.

Future Improvements
Convert synchronous usb_bulk_msg() transfers to asynchronous URB-based transfers
Add mutex-based command serialization
Implement complete SCSI error recovery
Implement WRITE(10)
Add a character-device interface for user-space access
Add sysfs attributes
Implement proper block-device integration
Add support for multiple LUNs
Improve USB reset/recovery handling
Add dynamic command buffers and improved memory management
Add kmemleak-based memory leak testing
