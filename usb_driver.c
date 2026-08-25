#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/delay.h>

#include "usb_driver.h"

#define DRIVER_NAME "usb_mass_storage"

#define VENDOR_ID   0x8564
#define PRODUCT_ID  0x4000


static const char *usb_speed_string(enum usb_device_speed speed)
{
	switch (speed) {
	case USB_SPEED_LOW:
		return "Low Speed (1.5 Mbps)";

	case USB_SPEED_FULL:
		return "Full Speed (12 Mbps)";

	case USB_SPEED_HIGH:
		return "High Speed (480 Mbps)";

	case USB_SPEED_SUPER:
		return "SuperSpeed (5 Gbps)";

	case USB_SPEED_SUPER_PLUS:
		return "SuperSpeed+ (10+ Gbps)";

	default:
		return "Unknown";
	}
}


/*
 * --------------------------------------------------------------------------
 * USB endpoint recovery
 * --------------------------------------------------------------------------
 */

static int clear_bulk_stalls(struct usb_mass_storage *dev)
{
	unsigned int pipe_in;
	unsigned int pipe_out;
	int ret_in;
	int ret_out;

	pr_warn("%s: clearing bulk endpoint stalls\n", DRIVER_NAME);

	pipe_in = usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep);
	pipe_out = usb_sndbulkpipe(dev->udev, dev->bulk_out_ep);

	ret_in = usb_clear_halt(dev->udev, pipe_in);
	ret_out = usb_clear_halt(dev->udev, pipe_out);

	if (ret_in)
		pr_err("%s: failed to clear IN halt: %d\n",
		       DRIVER_NAME, ret_in);

	if (ret_out)
		pr_err("%s: failed to clear OUT halt: %d\n",
		       DRIVER_NAME, ret_out);

	return (ret_in || ret_out) ? -EIO : 0;
}


/*
 * --------------------------------------------------------------------------
 * Bulk-Only Transport - CBW
 * --------------------------------------------------------------------------
 */

static int send_cbw(struct usb_mass_storage *dev,
		    struct bulk_cb_wrapper *cbw)
{
	unsigned int pipe;
	int actual;
	int ret;

	pipe = usb_sndbulkpipe(dev->udev, dev->bulk_out_ep);

	ret = usb_bulk_msg(dev->udev,
			   pipe,
			   cbw,
			   sizeof(*cbw),
			   &actual,
			   USB_TRANSFER_TIMEOUT_MS);

	if (ret) {
		pr_err("%s: CBW transfer failed: %d\n",
		       DRIVER_NAME, ret);

		if (ret == -EPIPE) {
			pr_warn("%s: CBW endpoint stalled\n",
				DRIVER_NAME);

			clear_bulk_stalls(dev);
		}

		return ret;
	}

	if (actual != sizeof(*cbw)) {
		pr_err("%s: short CBW transfer: %d bytes\n",
		       DRIVER_NAME, actual);
		return -EIO;
	}

	return 0;
}


/*
 * --------------------------------------------------------------------------
 * Bulk-Only Transport - CSW
 * --------------------------------------------------------------------------
 */

static int receive_csw(struct usb_mass_storage *dev,
		       struct bulk_cs_wrapper *csw,
		       u32 expected_tag)
{
	unsigned int pipe;
	int actual;
	int ret;
	u32 received_tag;
	u32 signature;

	memset(csw, 0, sizeof(*csw));

	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep);

	ret = usb_bulk_msg(dev->udev,
			   pipe,
			   csw,
			   sizeof(*csw),
			   &actual,
			   USB_TRANSFER_TIMEOUT_MS);

	if (ret) {
		pr_err("%s: CSW transfer failed: %d\n",
		       DRIVER_NAME, ret);

		if (ret == -EPIPE) {
			pr_warn("%s: CSW endpoint stalled\n",
				DRIVER_NAME);

			clear_bulk_stalls(dev);
		}

		return ret;
	}

	if (actual != sizeof(*csw)) {
		pr_err("%s: short CSW transfer: %d bytes\n",
		       DRIVER_NAME, actual);
		return -EIO;
	}

	signature = le32_to_cpu(csw->signature);

	if (signature != CSW_SIGNATURE) {
		pr_err("%s: invalid CSW signature: 0x%08x\n",
		       DRIVER_NAME, signature);
		return -EIO;
	}

	received_tag = le32_to_cpu(csw->tag);

	if (received_tag != expected_tag) {
		pr_err("%s: CSW tag mismatch: received=%u expected=%u\n",
		       DRIVER_NAME,
		       received_tag,
		       expected_tag);
		return -EIO;
	}

	switch (csw->status) {
	case CSW_STATUS_OK:
		return 0;

	case CSW_STATUS_FAIL:
		pr_err("%s: SCSI command failed\n", DRIVER_NAME);
		return -EIO;

	case CSW_STATUS_PHASE_ERROR:
		pr_err("%s: SCSI phase error\n", DRIVER_NAME);
		clear_bulk_stalls(dev);
		return -EIO;

	default:
		pr_err("%s: invalid CSW status: %u\n",
		       DRIVER_NAME,
		       csw->status);
		return -EIO;
	}
}


/*
 * --------------------------------------------------------------------------
 * SCSI INQUIRY
 * --------------------------------------------------------------------------
 */

static int scsi_inquiry(struct usb_mass_storage *dev)
{
	struct bulk_cb_wrapper *cbw;
	struct bulk_cs_wrapper *csw;
	unsigned int pipe;
	u8 *buffer;
	u32 tag;
	int actual;
	int ret;

	cbw = kzalloc(sizeof(*cbw), GFP_KERNEL);
	csw = kzalloc(sizeof(*csw), GFP_KERNEL);
	buffer = kzalloc(SCSI_INQUIRY_LENGTH, GFP_KERNEL);

	if (!cbw || !csw || !buffer) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cbw->signature = cpu_to_le32(CBW_SIGNATURE);

	tag = ++dev->tag;

	cbw->tag = cpu_to_le32(tag);
	cbw->data_transfer_length =
		cpu_to_le32(SCSI_INQUIRY_LENGTH);
	cbw->flags = CBW_FLAG_DATA_IN;
	cbw->lun = 0;
	cbw->cdb_length = 6;

	cbw->cdb[0] = SCSI_INQUIRY;
	cbw->cdb[4] = SCSI_INQUIRY_LENGTH;

	ret = send_cbw(dev, cbw);
	if (ret)
		goto cleanup;

	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep);

	ret = usb_bulk_msg(dev->udev,
			   pipe,
			   buffer,
			   SCSI_INQUIRY_LENGTH,
			   &actual,
			   USB_TRANSFER_TIMEOUT_MS);

	if (ret) {
		pr_err("%s: INQUIRY data transfer failed: %d\n",
		       DRIVER_NAME, ret);

		if (ret == -EPIPE)
			clear_bulk_stalls(dev);

		goto cleanup;
	}

	if (actual != SCSI_INQUIRY_LENGTH) {
		pr_err("%s: short INQUIRY transfer: %d bytes\n",
		       DRIVER_NAME, actual);
		ret = -EIO;
		goto cleanup;
	}

	ret = receive_csw(dev, csw, tag);
	if (ret)
		goto cleanup;

	pr_info("%s: INQUIRY Vendor: %.8s Product: %.16s\n",
		DRIVER_NAME,
		(char *)&buffer[8],
		(char *)&buffer[16]);

	ret = 0;

cleanup:
	kfree(buffer);
	kfree(csw);
	kfree(cbw);

	return ret;
}


/*
 * --------------------------------------------------------------------------
 * SCSI TEST UNIT READY
 * --------------------------------------------------------------------------
 */

static int scsi_test_unit_ready(struct usb_mass_storage *dev)
{
	struct bulk_cb_wrapper *cbw;
	struct bulk_cs_wrapper *csw;
	u32 tag;
	int ret;

	cbw = kzalloc(sizeof(*cbw), GFP_KERNEL);
	csw = kzalloc(sizeof(*csw), GFP_KERNEL);

	if (!cbw || !csw) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cbw->signature = cpu_to_le32(CBW_SIGNATURE);

	tag = ++dev->tag;

	cbw->tag = cpu_to_le32(tag);
	cbw->data_transfer_length = cpu_to_le32(0);
	cbw->flags = CBW_FLAG_DATA_OUT;
	cbw->lun = 0;
	cbw->cdb_length = 6;

	cbw->cdb[0] = SCSI_TEST_UNIT_READY;

	ret = send_cbw(dev, cbw);
	if (ret)
		goto cleanup;

	ret = receive_csw(dev, csw, tag);

	if (ret) {
		pr_warn("%s: TEST UNIT READY failed: %d\n",
			DRIVER_NAME, ret);
		goto cleanup;
	}

	pr_info("%s: TEST UNIT READY successful\n",
		DRIVER_NAME);

	ret = 0;

cleanup:
	kfree(csw);
	kfree(cbw);

	return ret;
}


/*
 * --------------------------------------------------------------------------
 * SCSI READ CAPACITY(10)
 * --------------------------------------------------------------------------
 */

static int scsi_read_capacity(struct usb_mass_storage *dev)
{
	struct bulk_cb_wrapper *cbw;
	struct bulk_cs_wrapper *csw;
	unsigned int pipe;
	u8 *buffer;
	u32 tag;
	u32 last_lba;
	u32 block_length;
	u64 total_bytes;
	int actual;
	int ret;

	cbw = kzalloc(sizeof(*cbw), GFP_KERNEL);
	csw = kzalloc(sizeof(*csw), GFP_KERNEL);
	buffer = kzalloc(SCSI_READ_CAPACITY_LENGTH, GFP_KERNEL);

	if (!cbw || !csw || !buffer) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cbw->signature = cpu_to_le32(CBW_SIGNATURE);

	tag = ++dev->tag;

	cbw->tag = cpu_to_le32(tag);
	cbw->data_transfer_length =
		cpu_to_le32(SCSI_READ_CAPACITY_LENGTH);
	cbw->flags = CBW_FLAG_DATA_IN;
	cbw->lun = 0;
	cbw->cdb_length = 10;

	cbw->cdb[0] = SCSI_READ_CAPACITY_10;

	ret = send_cbw(dev, cbw);
	if (ret)
		goto cleanup;

	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep);

	ret = usb_bulk_msg(dev->udev,
			   pipe,
			   buffer,
			   SCSI_READ_CAPACITY_LENGTH,
			   &actual,
			   USB_TRANSFER_TIMEOUT_MS);

	if (ret) {
		pr_err("%s: READ CAPACITY data transfer failed: %d\n",
		       DRIVER_NAME, ret);

		if (ret == -EPIPE)
			clear_bulk_stalls(dev);

		goto cleanup;
	}

	if (actual != SCSI_READ_CAPACITY_LENGTH) {
		pr_err("%s: short READ CAPACITY transfer\n",
		       DRIVER_NAME);
		ret = -EIO;
		goto cleanup;
	}

	ret = receive_csw(dev, csw, tag);
	if (ret)
		goto cleanup;

	last_lba = ((u32)buffer[0] << 24) |
		   ((u32)buffer[1] << 16) |
		   ((u32)buffer[2] << 8) |
		   buffer[3];

	block_length = ((u32)buffer[4] << 24) |
		       ((u32)buffer[5] << 16) |
		       ((u32)buffer[6] << 8) |
		       buffer[7];

	total_bytes = ((u64)last_lba + 1) * block_length;

	pr_info("%s: Last LBA     : %u\n",
		DRIVER_NAME, last_lba);

	pr_info("%s: Block length : %u bytes\n",
		DRIVER_NAME, block_length);

	pr_info("%s: Capacity     : %llu bytes (%llu MiB)\n",
		DRIVER_NAME,
		total_bytes,
		total_bytes >> 20);

	ret = 0;

cleanup:
	kfree(buffer);
	kfree(csw);
	kfree(cbw);

	return ret;
}


/*
 * --------------------------------------------------------------------------
 * SCSI REQUEST SENSE
 * --------------------------------------------------------------------------
 */

static int scsi_request_sense(struct usb_mass_storage *dev)
{
	struct bulk_cb_wrapper *cbw;
	struct bulk_cs_wrapper *csw;
	unsigned int pipe;
	u8 *buffer;
	u8 sense_key;
	u8 asc;
	u8 ascq;
	u32 tag;
	int actual;
	int ret;

	cbw = kzalloc(sizeof(*cbw), GFP_KERNEL);
	csw = kzalloc(sizeof(*csw), GFP_KERNEL);
	buffer = kzalloc(SCSI_SENSE_LENGTH, GFP_KERNEL);

	if (!cbw || !csw || !buffer) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cbw->signature = cpu_to_le32(CBW_SIGNATURE);

	tag = ++dev->tag;

	cbw->tag = cpu_to_le32(tag);
	cbw->data_transfer_length =
		cpu_to_le32(SCSI_SENSE_LENGTH);
	cbw->flags = CBW_FLAG_DATA_IN;
	cbw->lun = 0;
	cbw->cdb_length = 6;

	cbw->cdb[0] = SCSI_REQUEST_SENSE;
	cbw->cdb[4] = SCSI_SENSE_LENGTH;

	ret = send_cbw(dev, cbw);
	if (ret)
		goto cleanup;

	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep);

	ret = usb_bulk_msg(dev->udev,
			   pipe,
			   buffer,
			   SCSI_SENSE_LENGTH,
			   &actual,
			   USB_TRANSFER_TIMEOUT_MS);

	if (ret) {
		pr_err("%s: REQUEST SENSE data transfer failed: %d\n",
		       DRIVER_NAME, ret);

		if (ret == -EPIPE)
			clear_bulk_stalls(dev);

		goto cleanup;
	}

	ret = receive_csw(dev, csw, tag);
	if (ret)
		goto cleanup;

	sense_key = buffer[2] & 0x0f;
	asc = buffer[12];
	ascq = buffer[13];

	pr_info("%s: Sense Key=0x%02x ASC=0x%02x ASCQ=0x%02x\n",
		DRIVER_NAME,
		sense_key,
		asc,
		ascq);

	ret = 0;

cleanup:
	kfree(buffer);
	kfree(csw);
	kfree(cbw);

	return ret;
}


/*
 * --------------------------------------------------------------------------
 * SCSI READ(10)
 * --------------------------------------------------------------------------
 */

static int scsi_read10(struct usb_mass_storage *dev,
		       u32 lba,
		       u16 blocks)
{
	struct bulk_cb_wrapper *cbw;
	struct bulk_cs_wrapper *csw;
	unsigned int pipe;
	u8 *buffer;
	u32 tag;
	u32 transfer_length;
	int actual;
	int ret;

	if (!blocks)
		return -EINVAL;

	transfer_length = (u32)blocks * SCSI_SECTOR_SIZE;

	cbw = kzalloc(sizeof(*cbw), GFP_KERNEL);
	csw = kzalloc(sizeof(*csw), GFP_KERNEL);
	buffer = kzalloc(transfer_length, GFP_KERNEL);

	if (!cbw || !csw || !buffer) {
		ret = -ENOMEM;
		goto cleanup;
	}

	cbw->signature = cpu_to_le32(CBW_SIGNATURE);

	tag = ++dev->tag;

	cbw->tag = cpu_to_le32(tag);
	cbw->data_transfer_length =
		cpu_to_le32(transfer_length);
	cbw->flags = CBW_FLAG_DATA_IN;
	cbw->lun = 0;
	cbw->cdb_length = 10;

	cbw->cdb[0] = SCSI_READ_10;

	/* LBA - big endian */
	cbw->cdb[2] = (lba >> 24) & 0xff;
	cbw->cdb[3] = (lba >> 16) & 0xff;
	cbw->cdb[4] = (lba >> 8) & 0xff;
	cbw->cdb[5] = lba & 0xff;

	/* Transfer length - big endian */
	cbw->cdb[7] = (blocks >> 8) & 0xff;
	cbw->cdb[8] = blocks & 0xff;

	ret = send_cbw(dev, cbw);
	if (ret)
		goto cleanup;

	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep);

	ret = usb_bulk_msg(dev->udev,
			   pipe,
			   buffer,
			   transfer_length,
			   &actual,
			   USB_TRANSFER_TIMEOUT_MS);

	if (ret) {
		pr_err("%s: READ(10) data transfer failed: %d\n",
		       DRIVER_NAME, ret);

		if (ret == -EPIPE)
			clear_bulk_stalls(dev);

		goto cleanup;
	}

	if (actual != transfer_length) {
		pr_err("%s: READ(10) short transfer: %d/%u bytes\n",
		       DRIVER_NAME,
		       actual,
		       transfer_length);

		ret = -EIO;
		goto cleanup;
	}

	ret = receive_csw(dev, csw, tag);
	if (ret)
		goto cleanup;

	pr_info("%s: READ(10): LBA=%u blocks=%u bytes=%u\n",
		DRIVER_NAME,
		lba,
		blocks,
		transfer_length);

	pr_info("%s: First 16 bytes: %16ph\n",
		DRIVER_NAME,
		buffer);

	/*
	 * Inspect the MBR if LBA 0 was requested.
	 */
	if (lba == 0 && blocks == 1) {
		int i;

		if (buffer[510] == 0x55 &&
		    buffer[511] == 0xaa) {

			pr_info("%s: Valid MBR signature detected\n",
				DRIVER_NAME);

			for (i = 0; i < 4; i++) {
				u8 *entry;
				u8 partition_type;
				u32 start_lba;
				u32 sector_count;

				entry = &buffer[446 + (i * 16)];

				partition_type = entry[4];

				if (!partition_type)
					continue;

				start_lba =
					(u32)entry[8] |
					((u32)entry[9] << 8) |
					((u32)entry[10] << 16) |
					((u32)entry[11] << 24);

				sector_count =
					(u32)entry[12] |
					((u32)entry[13] << 8) |
					((u32)entry[14] << 16) |
					((u32)entry[15] << 24);

				pr_info(
					"%s: Partition %d: type=0x%02x "
					"start=%u sectors=%u size=%llu MiB\n",
					DRIVER_NAME,
					i + 1,
					partition_type,
					start_lba,
					sector_count,
					((u64)sector_count * 512) >> 20);
			}
		} else {
			pr_info("%s: No valid MBR signature\n",
				DRIVER_NAME);
		}
	}

	ret = 0;

cleanup:
	kfree(buffer);
	kfree(csw);
	kfree(cbw);

	return ret;
}


/*
 * --------------------------------------------------------------------------
 * USB Descriptor Parsing
 * --------------------------------------------------------------------------
 */

static int parse_usb_endpoints(struct usb_mass_storage *dev)
{
	struct usb_interface *interface = dev->interface;
	struct usb_host_interface *alt;
	struct usb_device *udev = dev->udev;
	int i;

	alt = interface->cur_altsetting;

	pr_info("%s: Interface class    = 0x%02x\n",
		DRIVER_NAME,
		alt->desc.bInterfaceClass);

	pr_info("%s: Interface subclass = 0x%02x\n",
		DRIVER_NAME,
		alt->desc.bInterfaceSubClass);

	pr_info("%s: Interface protocol = 0x%02x\n",
		DRIVER_NAME,
		alt->desc.bInterfaceProtocol);

	pr_info("%s: Endpoint count     = %u\n",
		DRIVER_NAME,
		alt->desc.bNumEndpoints);

	if (alt->desc.bInterfaceClass != USB_MASS_STORAGE_CLASS ||
	    alt->desc.bInterfaceSubClass != USB_MASS_STORAGE_SUBCLASS ||
	    alt->desc.bInterfaceProtocol != USB_MASS_STORAGE_PROTOCOL) {
		pr_err("%s: unsupported USB interface\n",
		       DRIVER_NAME);
		return -ENODEV;
	}

	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep;
		struct usb_ss_ep_comp_descriptor *ss_comp;

		ep = &alt->endpoint[i].desc;

		pr_info("%s: EP[%d] address=0x%02x attributes=0x%02x maxpkt=%u\n",
			DRIVER_NAME,
			i,
			ep->bEndpointAddress,
			ep->bmAttributes,
			usb_endpoint_maxp(ep));

		if (udev->speed >= USB_SPEED_SUPER) {
			ss_comp = &alt->endpoint[i].ss_ep_comp;

			pr_info(
				"%s: EP[%d] SS Companion: "
				"burst=%u attributes=0x%02x "
				"bytes/interval=%u\n",
				DRIVER_NAME,
				i,
				ss_comp->bMaxBurst,
				ss_comp->bmAttributes,
				le16_to_cpu(
					ss_comp->wBytesPerInterval));
		}

		if (usb_endpoint_is_bulk_in(ep)) {
			dev->bulk_in_ep = ep->bEndpointAddress;
			dev->max_packet_in = usb_endpoint_maxp(ep);

			pr_info("%s: Bulk IN endpoint = 0x%02x\n",
				DRIVER_NAME,
				dev->bulk_in_ep);
		}

		if (usb_endpoint_is_bulk_out(ep)) {
			dev->bulk_out_ep = ep->bEndpointAddress;
			dev->max_packet_out = usb_endpoint_maxp(ep);

			pr_info("%s: Bulk OUT endpoint = 0x%02x\n",
				DRIVER_NAME,
				dev->bulk_out_ep);
		}
	}

	if (!dev->bulk_in_ep || !dev->bulk_out_ep) {
		pr_err("%s: bulk IN/OUT endpoints not found\n",
		       DRIVER_NAME);
		return -ENODEV;
	}

	return 0;
}


/*
 * --------------------------------------------------------------------------
 * USB Probe
 * --------------------------------------------------------------------------
 */

static int usb_mass_storage_probe(struct usb_interface *interface,
				  const struct usb_device_id *id)
{
	struct usb_mass_storage *dev;
	struct usb_device *udev;
	int ret;
	int attempt;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	udev = interface_to_usbdev(interface);

	dev->udev = usb_get_dev(udev);
	dev->interface = interface;

	usb_set_intfdata(interface, dev);

	pr_info("\n");
	pr_info("=============================================\n");
	pr_info("%s: USB Mass Storage Device Connected\n",
		DRIVER_NAME);
	pr_info("=============================================\n");

	pr_info("%s: VID:PID = %04x:%04x\n",
		DRIVER_NAME,
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct));

	pr_info("%s: USB speed = %s\n",
		DRIVER_NAME,
		usb_speed_string(udev->speed));

	pr_info("%s: bcdUSB = 0x%04x\n",
		DRIVER_NAME,
		le16_to_cpu(udev->descriptor.bcdUSB));

	if (udev->bos)
		pr_info("%s: BOS descriptor present\n",
			DRIVER_NAME);
	else
		pr_info("%s: BOS descriptor not present\n",
			DRIVER_NAME);

	if (udev->speed >= USB_SPEED_SUPER)
		pr_info("%s: SuperSpeed connection detected\n",
			DRIVER_NAME);
	else
		pr_warn("%s: Device is not connected at SuperSpeed\n",
			DRIVER_NAME);

	pr_info("%s: LPM capable=%u U1=%u U2=%u\n",
		DRIVER_NAME,
		udev->lpm_capable,
		udev->usb3_lpm_u1_enabled,
		udev->usb3_lpm_u2_enabled);

	ret = parse_usb_endpoints(dev);
	if (ret)
		goto error;

	/*
	 * SCSI INQUIRY
	 */
	ret = scsi_inquiry(dev);
	if (ret) {
		pr_err("%s: INQUIRY failed: %d\n",
		       DRIVER_NAME, ret);
		goto error;
	}

	/*
	 * TEST UNIT READY
	 *
	 * Some removable-media devices need a short delay before
	 * reporting that the medium is ready.
	 */
	for (attempt = 0; attempt < 5; attempt++) {
		ret = scsi_test_unit_ready(dev);

		if (!ret)
			break;

		msleep(300);
	}

	if (ret) {
		pr_warn("%s: device is not ready\n",
			DRIVER_NAME);

		scsi_request_sense(dev);
	} else {
		/*
		 * READ CAPACITY
		 */
		ret = scsi_read_capacity(dev);

		if (ret)
			pr_err("%s: READ CAPACITY failed: %d\n",
			       DRIVER_NAME, ret);
	}

	/*
	 * Read first sector for demonstration.
	 */
	ret = scsi_read10(dev, 0, 1);

	if (ret)
		pr_err("%s: READ(10) failed: %d\n",
		       DRIVER_NAME, ret);

	pr_info("%s: probe completed\n", DRIVER_NAME);

	return 0;

error:
	usb_set_intfdata(interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);

	return ret;
}


/*
 * --------------------------------------------------------------------------
 * USB Disconnect
 * --------------------------------------------------------------------------
 */

static void usb_mass_storage_disconnect(struct usb_interface *interface)
{
	struct usb_mass_storage *dev;

	dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);

	if (dev) {
		usb_put_dev(dev->udev);
		kfree(dev);
	}

	pr_info("%s: USB Mass Storage Device disconnected\n",
		DRIVER_NAME);
}


/*
 * --------------------------------------------------------------------------
 * USB Device ID Table
 * --------------------------------------------------------------------------
 */

static const struct usb_device_id usb_mass_storage_table[] = {
	{
		USB_DEVICE(VENDOR_ID, PRODUCT_ID)
	},

	{
		USB_INTERFACE_INFO(
			USB_MASS_STORAGE_CLASS,
			USB_MASS_STORAGE_SUBCLASS,
			USB_MASS_STORAGE_PROTOCOL)
	},

	{}
};

MODULE_DEVICE_TABLE(usb, usb_mass_storage_table);


/*
 * --------------------------------------------------------------------------
 * USB Driver
 * --------------------------------------------------------------------------
 */

static struct usb_driver usb_mass_storage_driver = {
	.name = DRIVER_NAME,
	.id_table = usb_mass_storage_table,
	.probe = usb_mass_storage_probe,
	.disconnect = usb_mass_storage_disconnect,
};


/*
 * --------------------------------------------------------------------------
 * Module Init / Exit
 * --------------------------------------------------------------------------
 */

static int __init usb_init(void)
{
	int ret;

	pr_info("%s: loading driver\n", DRIVER_NAME);

	ret = usb_register(&usb_mass_storage_driver);

	if (ret) {
		pr_err("%s: USB driver registration failed: %d\n",
		       DRIVER_NAME, ret);
		return ret;
	}

	pr_info("%s: driver registered successfully\n",
		DRIVER_NAME);

	return 0;
}


static void __exit usb_exit(void)
{
	usb_deregister(&usb_mass_storage_driver);

	pr_info("%s: driver unloaded\n", DRIVER_NAME);
}

module_init(usb_init);
module_exit(usb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Siddarth M");
MODULE_DESCRIPTION("Linux USB 3.x Mass Storage Host Driver using "
                    "Bulk-Only Transport and SCSI commands");
MODULE_VERSION("1.0");
