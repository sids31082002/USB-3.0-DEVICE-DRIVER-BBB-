#ifndef USB_MASS_STORAGE_H
#define USB_MASS_STORAGE_H

#include <linux/types.h>
#include <linux/usb.h>

/*
 * USB Mass Storage Class
 */
#define USB_MASS_STORAGE_CLASS       0x08
#define USB_MASS_STORAGE_SUBCLASS    0x06
#define USB_MASS_STORAGE_PROTOCOL    0x50

/*
 * Bulk-Only Transport signatures
 */
#define CBW_SIGNATURE                0x43425355
#define CSW_SIGNATURE                0x53425355

#define CBW_FLAG_DATA_IN             0x80
#define CBW_FLAG_DATA_OUT            0x00

#define CSW_STATUS_OK                0
#define CSW_STATUS_FAIL              1
#define CSW_STATUS_PHASE_ERROR       2

#define USB_TRANSFER_TIMEOUT_MS      5000

/*
 * SCSI commands
 */
#define SCSI_TEST_UNIT_READY         0x00
#define SCSI_REQUEST_SENSE           0x03
#define SCSI_INQUIRY                 0x12
#define SCSI_READ_CAPACITY_10        0x25
#define SCSI_READ_10                 0x28

#define SCSI_INQUIRY_LENGTH          36
#define SCSI_SENSE_LENGTH            18
#define SCSI_READ_CAPACITY_LENGTH   8
#define SCSI_SECTOR_SIZE             512

/*
 * USB Mass Storage Bulk-Only Command Block Wrapper
 *
 * Total size = 31 bytes
 */
struct bulk_cb_wrapper {
	__le32 signature;
	__le32 tag;
	__le32 data_transfer_length;
	__u8 flags;
	__u8 lun;
	__u8 cdb_length;
	__u8 cdb[16];
} __packed;

/*
 * USB Mass Storage Bulk-Only Command Status Wrapper
 *
 * Total size = 13 bytes
 */
struct bulk_cs_wrapper {
	__le32 signature;
	__le32 tag;
	__le32 residue;
	__u8 status;
} __packed;

/*
 * Driver private data
 */
struct usb_mass_storage {
	struct usb_device *udev;
	struct usb_interface *interface;

	u8 bulk_in_ep;
	u8 bulk_out_ep;

	u16 max_packet_in;
	u16 max_packet_out;

	u32 tag;
};

#endif /* USB_MASS_STORAGE_H */
