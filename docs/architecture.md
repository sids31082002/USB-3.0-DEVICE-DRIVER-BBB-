# Driver Architecture

## Overview

The driver follows the Linux USB driver model.

```text
                    USB Core
                       |
                       v
              usb_mass_storage_driver
                       |
                       v
                    probe()
                       |
        +--------------+--------------+
        |              |              |
        v              v              v
 Device Descriptor  Interface      Endpoints
                    Descriptor
                       |
                       v
              Bulk IN / Bulk OUT
                       |
                       v
                SCSI Commands
                       |
          +------------+------------+
          |            |            |
          v            v            v
       INQUIRY       TUR        READ CAPACITY
                                      |
                                      v
                                  READ(10)



Linux USB Driver Framework
        ↓
USB Enumeration
        ↓
Device / Interface / Endpoint Descriptors
        ↓
USB 3.x SuperSpeed
        ↓
SuperSpeed Endpoint Companion Descriptor
        ↓
Bulk-Only Transport (BBB)
        ↓
CBW
        ↓
SCSI Data Phase
        ↓
CSW
        ↓
SCSI Commands
        ↓
INQUIRY
TEST UNIT READY
READ CAPACITY(10)
REQUEST SENSE
READ(10)
        ↓
MBR / Partition Inspection
