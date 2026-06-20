#include "usb_msc_device.h"

#include "app_config.h"
#include "storage_w25q_msc.h"
#include <stddef.h>
#include <string.h>

#define USB_PMA_ACCESS_WORDS             2u
#define USB_EP_COUNT                     8u

#define USB_EP_CTR_RX                    0x8000u
#define USB_EP_STAT_RX                   0x3000u
#define USB_EP_SETUP                     0x0800u
#define USB_EP_TYPE_MASK                 0x0600u
#define USB_EP_KIND                      0x0100u
#define USB_EP_CTR_TX                    0x0080u
#define USB_EP_STAT_TX                   0x0030u
#define USB_EPADDR_FIELD                 0x000Fu

#define USB_EP_TYPE_CONTROL              0x0200u
#define USB_EP_TYPE_BULK                 0x0000u

#define USB_EP_TX_DIS                    0x0000u
#define USB_EP_TX_STALL                  0x0010u
#define USB_EP_TX_NAK                    0x0020u
#define USB_EP_TX_VALID                  0x0030u
#define USB_EP_RX_DIS                    0x0000u
#define USB_EP_RX_STALL                  0x1000u
#define USB_EP_RX_NAK                    0x2000u
#define USB_EP_RX_VALID                  0x3000u

#define USB_CNTR_FRES                    0x0001u
#define USB_CNTR_PDWN                    0x0002u
#define USB_CNTR_RESETM                  0x0400u
#define USB_CNTR_CTRM                    0x8000u

#define USB_ISTR_EP_ID                   0x000Fu
#define USB_ISTR_DIR                     0x0010u
#define USB_ISTR_RESET                   0x0400u
#define USB_ISTR_CTR                     0x8000u

#define USB_DADDR_EF                     0x0080u

#define MSC_EP0_SIZE                     64u
#define MSC_BULK_EP_SIZE                 64u
#define MSC_IN_EP                        1u
#define MSC_OUT_EP                       2u

#define PMA_EP0_TX                       0x0040u
#define PMA_EP0_RX                       0x0080u
#define PMA_MSC_IN                       0x00C0u
#define PMA_MSC_OUT                      0x0100u

#define USB_REQ_TYPE_MASK                0x60u
#define USB_REQ_TYPE_STANDARD            0x00u
#define USB_REQ_TYPE_CLASS               0x20u
#define USB_REQ_GET_STATUS               0x00u
#define USB_REQ_CLEAR_FEATURE            0x01u
#define USB_REQ_SET_ADDRESS              0x05u
#define USB_REQ_GET_DESCRIPTOR           0x06u
#define USB_REQ_GET_CONFIGURATION        0x08u
#define USB_REQ_SET_CONFIGURATION        0x09u
#define USB_REQ_GET_INTERFACE            0x0Au
#define USB_REQ_SET_INTERFACE            0x0Bu

#define USB_DESC_TYPE_DEVICE             0x01u
#define USB_DESC_TYPE_CONFIGURATION      0x02u
#define USB_DESC_TYPE_STRING             0x03u
#define USB_DESC_TYPE_DEVICE_QUALIFIER   0x06u

#define MSC_REQ_GET_MAX_LUN              0xFEu
#define MSC_REQ_BULK_ONLY_RESET          0xFFu

#define MSC_CBW_SIGNATURE                0x43425355u
#define MSC_CSW_SIGNATURE                0x53425355u
#define MSC_CSW_PASS                     0u
#define MSC_CSW_FAIL                     1u

#define SCSI_TEST_UNIT_READY             0x00u
#define SCSI_REQUEST_SENSE               0x03u
#define SCSI_INQUIRY                     0x12u
#define SCSI_MODE_SENSE6                 0x1Au
#define SCSI_START_STOP_UNIT             0x1Bu
#define SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1Eu
#define SCSI_READ_FORMAT_CAPACITIES      0x23u
#define SCSI_READ_CAPACITY10             0x25u
#define SCSI_READ10                      0x28u
#define SCSI_WRITE10                     0x2Au

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} UsbSetupPacket;

typedef enum {
    EP0_IDLE = 0,
    EP0_DATA_IN,
    EP0_STATUS_IN,
    EP0_STATUS_OUT,
    EP0_STALL
} Ep0State;

typedef enum {
    MSC_WAIT_CBW = 0,
    MSC_DATA_IN,
    MSC_DATA_OUT,
    MSC_SEND_CSW
} MscState;

typedef struct {
    volatile uint8_t active;
    volatile uint8_t configured;
    volatile uint8_t address_pending;
    volatile uint8_t pending_address;
    volatile uint8_t ep1_tx_busy;
    uint8_t configuration;
    Ep0State ep0_state;
    const uint8_t *ep0_tx;
    uint16_t ep0_tx_len;
    uint16_t ep0_tx_pos;
    MscState state;
    uint8_t cbw[31];
    uint8_t csw[13];
    uint32_t tag;
    uint32_t residue;
    uint8_t csw_status;
    uint32_t lba;
    uint32_t blocks_left;
    uint8_t block_buf[MSC_BLOCK_BYTES];
    uint16_t block_pos;
    uint16_t block_valid;
    uint8_t sense_key;
    uint8_t asc;
    uint8_t ascq;
} UsbMscContext;

static UsbMscContext s_msc;

static const uint8_t s_device_descriptor[] = {
    0x12, USB_DESC_TYPE_DEVICE,
    0x00, 0x02,
    0x00, 0x00, 0x00,
    MSC_EP0_SIZE,
    0x83, 0x04,
    0x50, 0x57,
    0x00, 0x01,
    0x01, 0x02, 0x03,
    0x01
};

static const uint8_t s_configuration_descriptor[] = {
    0x09, USB_DESC_TYPE_CONFIGURATION,
    0x20, 0x00,
    0x01,
    0x01,
    0x00,
    0x80,
    0x32,

    0x09, 0x04,
    0x00,
    0x00,
    0x02,
    0x08,
    0x06,
    0x50,
    0x00,

    0x07, 0x05,
    (uint8_t)(0x80u | MSC_IN_EP),
    0x02,
    MSC_BULK_EP_SIZE, 0x00,
    0x00,

    0x07, 0x05,
    MSC_OUT_EP,
    0x02,
    MSC_BULK_EP_SIZE, 0x00,
    0x00
};

static const uint8_t s_lang_id_descriptor[] = {
    0x04, USB_DESC_TYPE_STRING, 0x09, 0x04
};

static const uint8_t s_manufacturer_string[] = {
    20, USB_DESC_TYPE_STRING,
    'P', 0, 'r', 0, 'e', 0, 's', 0, 's', 0, 'u', 0, 'r', 0, 'e', 0, 'F', 0
};

static const uint8_t s_product_string[] = {
    42, USB_DESC_TYPE_STRING,
    'P', 0, 'r', 0, 'e', 0, 's', 0, 's', 0, 'u', 0, 'r', 0, 'e', 0, ' ', 0,
    'F', 0, 'i', 0, 'x', 0, 't', 0, 'u', 0, 'r', 0, 'e', 0, ' ', 0, 'M', 0, 'S', 0, 'C', 0
};

static const uint8_t s_serial_string[] = {
    18, USB_DESC_TYPE_STRING,
    'M', 0, 'S', 0, 'C', 0, '2', 0, '0', 0, '2', 0, '6', 0, '0', 0
};

static const uint8_t s_inquiry_data[] = {
    0x00, 0x80, 0x02, 0x02, 0x1F, 0x00, 0x00, 0x00,
    'P', 'r', 'e', 's', 's', 'u', 'r', 'e',
    'F', 'i', 'x', 't', 'u', 'r', 'e', ' ', 'M', 'S', 'C', ' ', ' ', ' ', ' ', ' ',
    '1', '.', '0', '0'
};

static volatile uint16_t *ep_reg(uint8_t ep)
{
    return &USB->EP0R + ((uint32_t)ep * 2u);
}

static volatile uint16_t *pma_ptr(uint16_t addr)
{
    return (__IO uint16_t *)(USB_BASE + 0x400u + ((uint32_t)addr * USB_PMA_ACCESS_WORDS));
}

static void pma_write(const uint8_t *src, uint16_t pma_addr, uint16_t len)
{
    volatile uint16_t *dst = pma_ptr(pma_addr);
    uint16_t i = 0u;

    while (i < len) {
        uint16_t v = src[i++];
        if (i < len) {
            v |= (uint16_t)src[i++] << 8;
        }
        *dst = v;
        dst += USB_PMA_ACCESS_WORDS;
    }
}

static void pma_read(uint8_t *dst, uint16_t pma_addr, uint16_t len)
{
    volatile uint16_t *src = pma_ptr(pma_addr);
    uint16_t i = 0u;

    while (i < len) {
        uint16_t v = *src;
        dst[i++] = (uint8_t)(v & 0xFFu);
        if (i < len) {
            dst[i++] = (uint8_t)(v >> 8);
        }
        src += USB_PMA_ACCESS_WORDS;
    }
}

static uint16_t rx_count_value(uint16_t len)
{
    if (len > 62u) {
        return (uint16_t)(0x8000u | (((len + 31u) / 32u) << 10));
    }
    return (uint16_t)(((len + 1u) / 2u) << 10);
}

static volatile uint16_t *pma_addr_ptr(uint16_t pma_addr)
{
    return pma_ptr(pma_addr);
}

static void pma_set_tx(uint8_t ep, uint16_t addr, uint16_t count)
{
    uint16_t table = (uint16_t)(USB->BTABLE + ((uint16_t)ep * 8u));
    *pma_addr_ptr(table) = addr;
    *pma_addr_ptr((uint16_t)(table + 2u)) = count;
}

static void pma_set_rx(uint8_t ep, uint16_t addr, uint16_t count)
{
    uint16_t table = (uint16_t)(USB->BTABLE + ((uint16_t)ep * 8u));
    *pma_addr_ptr((uint16_t)(table + 4u)) = addr;
    *pma_addr_ptr((uint16_t)(table + 6u)) = rx_count_value(count);
}

static uint16_t pma_get_rx_count(uint8_t ep)
{
    uint16_t table = (uint16_t)(USB->BTABLE + ((uint16_t)ep * 8u));
    return (uint16_t)(*pma_addr_ptr((uint16_t)(table + 6u)) & 0x03FFu);
}

static void ep_write_status(uint8_t ep, uint16_t mask, uint16_t status)
{
    volatile uint16_t *reg = ep_reg(ep);
    uint16_t current = *reg;
    uint16_t keep = (uint16_t)(current & (USB_EP_TYPE_MASK | USB_EP_KIND | USB_EPADDR_FIELD));
    uint16_t toggle = (uint16_t)((current & mask) ^ status);

    *reg = (uint16_t)(USB_EP_CTR_RX | USB_EP_CTR_TX | keep | toggle);
}

static void ep_clear_ctr_rx(uint8_t ep)
{
    volatile uint16_t *reg = ep_reg(ep);
    uint16_t val = *reg;
    val &= (uint16_t)(USB_EP_TYPE_MASK | USB_EP_KIND | USB_EPADDR_FIELD);
    val |= USB_EP_CTR_TX;
    *reg = val;
}

static void ep_clear_ctr_tx(uint8_t ep)
{
    volatile uint16_t *reg = ep_reg(ep);
    uint16_t val = *reg;
    val &= (uint16_t)(USB_EP_TYPE_MASK | USB_EP_KIND | USB_EPADDR_FIELD);
    val |= USB_EP_CTR_RX;
    *reg = val;
}

static void ep_set_type_addr(uint8_t ep, uint16_t type, uint8_t addr)
{
    *ep_reg(ep) = (uint16_t)(USB_EP_CTR_RX | USB_EP_CTR_TX | type | (addr & USB_EPADDR_FIELD));
}

static void ep_stall(uint8_t ep)
{
    ep_write_status(ep, USB_EP_STAT_RX, USB_EP_RX_STALL);
    ep_write_status(ep, USB_EP_STAT_TX, USB_EP_TX_STALL);
    s_msc.ep0_state = EP0_STALL;
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           p[3];
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static void parse_setup(const uint8_t *buf, UsbSetupPacket *setup)
{
    setup->bmRequestType = buf[0];
    setup->bRequest = buf[1];
    setup->wValue = get_le16(&buf[2]);
    setup->wIndex = get_le16(&buf[4]);
    setup->wLength = get_le16(&buf[6]);
}

static void ep0_rx_valid(void)
{
    pma_set_rx(0u, PMA_EP0_RX, MSC_EP0_SIZE);
    ep_write_status(0u, USB_EP_STAT_RX, USB_EP_RX_VALID);
}

static void ep0_send_chunk(void)
{
    uint16_t remaining = (uint16_t)(s_msc.ep0_tx_len - s_msc.ep0_tx_pos);
    uint16_t chunk = remaining > MSC_EP0_SIZE ? MSC_EP0_SIZE : remaining;

    if (chunk > 0u) {
        pma_write(&s_msc.ep0_tx[s_msc.ep0_tx_pos], PMA_EP0_TX, chunk);
    }
    pma_set_tx(0u, PMA_EP0_TX, chunk);
    s_msc.ep0_tx_pos = (uint16_t)(s_msc.ep0_tx_pos + chunk);
    s_msc.ep0_state = EP0_DATA_IN;
    ep_write_status(0u, USB_EP_STAT_TX, USB_EP_TX_VALID);
}

static void ep0_send_data(const uint8_t *data, uint16_t len, uint16_t requested)
{
    if (len > requested) {
        len = requested;
    }
    s_msc.ep0_tx = data;
    s_msc.ep0_tx_len = len;
    s_msc.ep0_tx_pos = 0u;
    ep0_send_chunk();
}

static void ep0_status_in(void)
{
    pma_set_tx(0u, PMA_EP0_TX, 0u);
    s_msc.ep0_state = EP0_STATUS_IN;
    ep_write_status(0u, USB_EP_STAT_TX, USB_EP_TX_VALID);
}

static void ep0_status_out(void)
{
    s_msc.ep0_state = EP0_STATUS_OUT;
    ep0_rx_valid();
}

static const uint8_t *string_descriptor(uint8_t index, uint16_t *len)
{
    switch (index) {
    case 0u:
        *len = sizeof(s_lang_id_descriptor);
        return s_lang_id_descriptor;
    case 1u:
        *len = sizeof(s_manufacturer_string);
        return s_manufacturer_string;
    case 2u:
        *len = sizeof(s_product_string);
        return s_product_string;
    case 3u:
        *len = sizeof(s_serial_string);
        return s_serial_string;
    default:
        *len = 0u;
        return 0;
    }
}

static void handle_standard_request(const UsbSetupPacket *setup)
{
    uint8_t descriptor_type;
    uint8_t descriptor_index;
    uint16_t len = 0u;
    const uint8_t *data = 0;
    static const uint8_t zero[2] = {0u, 0u};

    switch (setup->bRequest) {
    case USB_REQ_GET_DESCRIPTOR:
        descriptor_type = (uint8_t)(setup->wValue >> 8);
        descriptor_index = (uint8_t)(setup->wValue & 0xFFu);
        if (descriptor_type == USB_DESC_TYPE_DEVICE) {
            data = s_device_descriptor;
            len = sizeof(s_device_descriptor);
        } else if (descriptor_type == USB_DESC_TYPE_CONFIGURATION) {
            data = s_configuration_descriptor;
            len = sizeof(s_configuration_descriptor);
        } else if (descriptor_type == USB_DESC_TYPE_STRING) {
            data = string_descriptor(descriptor_index, &len);
        } else if (descriptor_type == USB_DESC_TYPE_DEVICE_QUALIFIER) {
            ep_stall(0u);
            return;
        }
        if (data == 0 || len == 0u) {
            ep_stall(0u);
            return;
        }
        ep0_send_data(data, len, setup->wLength);
        return;

    case USB_REQ_SET_ADDRESS:
        if (setup->wLength != 0u || setup->wValue > 127u) {
            ep_stall(0u);
            return;
        }
        s_msc.pending_address = (uint8_t)setup->wValue;
        s_msc.address_pending = 1u;
        ep0_status_in();
        return;

    case USB_REQ_SET_CONFIGURATION:
        if (setup->wLength != 0u || setup->wValue > 1u) {
            ep_stall(0u);
            return;
        }
        s_msc.configuration = (uint8_t)setup->wValue;
        s_msc.configured = s_msc.configuration != 0u ? 1u : 0u;
        ep0_status_in();
        return;

    case USB_REQ_GET_CONFIGURATION:
        ep0_send_data(&s_msc.configuration, 1u, setup->wLength);
        return;

    case USB_REQ_GET_STATUS:
        ep0_send_data(zero, sizeof(zero), setup->wLength);
        return;

    case USB_REQ_GET_INTERFACE:
        ep0_send_data(zero, 1u, setup->wLength);
        return;

    case USB_REQ_SET_INTERFACE:
    case USB_REQ_CLEAR_FEATURE:
        ep0_status_in();
        return;

    default:
        ep_stall(0u);
        return;
    }
}

static void handle_class_request(const UsbSetupPacket *setup)
{
    static const uint8_t max_lun = 0u;

    if (setup->bRequest == MSC_REQ_GET_MAX_LUN && setup->wLength == 1u) {
        ep0_send_data(&max_lun, 1u, setup->wLength);
    } else if (setup->bRequest == MSC_REQ_BULK_ONLY_RESET && setup->wLength == 0u) {
        s_msc.state = MSC_WAIT_CBW;
        s_msc.ep1_tx_busy = 0u;
        ep0_status_in();
    } else {
        ep_stall(0u);
    }
}

static void send_bulk_packet(const uint8_t *data, uint16_t len)
{
    if (len > MSC_BULK_EP_SIZE) {
        len = MSC_BULK_EP_SIZE;
    }
    if (len > 0u) {
        pma_write(data, PMA_MSC_IN, len);
    }
    pma_set_tx(MSC_IN_EP, PMA_MSC_IN, len);
    s_msc.ep1_tx_busy = 1u;
    ep_write_status(MSC_IN_EP, USB_EP_STAT_TX, USB_EP_TX_VALID);
}

static void send_csw(void)
{
    put_le32(&s_msc.csw[0], MSC_CSW_SIGNATURE);
    put_le32(&s_msc.csw[4], s_msc.tag);
    put_le32(&s_msc.csw[8], s_msc.residue);
    s_msc.csw[12] = s_msc.csw_status;
    s_msc.state = MSC_SEND_CSW;
    send_bulk_packet(s_msc.csw, sizeof(s_msc.csw));
}

static void set_sense(uint8_t key, uint8_t asc, uint8_t ascq)
{
    s_msc.sense_key = key;
    s_msc.asc = asc;
    s_msc.ascq = ascq;
}

static void send_scsi_data(const uint8_t *data, uint16_t len)
{
    uint16_t send_len = len;
    if (send_len > s_msc.residue) {
        send_len = (uint16_t)s_msc.residue;
    }
    s_msc.residue -= send_len;
    s_msc.csw_status = MSC_CSW_PASS;
    s_msc.state = MSC_DATA_IN;
    send_bulk_packet(data, send_len);
}

static void send_read_capacity(void)
{
    uint8_t data[8];
    uint32_t last_lba = MSC_BLOCK_COUNT - 1u;
    put_be32(&data[0], last_lba);
    put_be32(&data[4], MSC_BLOCK_BYTES);
    send_scsi_data(data, sizeof(data));
}

static void send_format_capacities(void)
{
    uint8_t data[12] = {0};
    data[3] = 0x08u;
    put_be32(&data[4], MSC_BLOCK_COUNT);
    data[8] = 0x02u;
    data[9] = (uint8_t)((MSC_BLOCK_BYTES >> 16) & 0xFFu);
    data[10] = (uint8_t)((MSC_BLOCK_BYTES >> 8) & 0xFFu);
    data[11] = (uint8_t)(MSC_BLOCK_BYTES & 0xFFu);
    send_scsi_data(data, sizeof(data));
}

static void send_request_sense(void)
{
    uint8_t data[18] = {0};
    data[0] = 0x70u;
    data[2] = s_msc.sense_key;
    data[7] = 0x0Au;
    data[12] = s_msc.asc;
    data[13] = s_msc.ascq;
    set_sense(0u, 0u, 0u);
    send_scsi_data(data, sizeof(data));
}

static void send_mode_sense6(void)
{
    uint8_t data[4] = {0x03u, 0x00u, 0x00u, 0x00u};
    send_scsi_data(data, sizeof(data));
}

static void start_read10(const uint8_t *cb)
{
    s_msc.lba = get_be32(&cb[2]);
    s_msc.blocks_left = get_be16(&cb[7]);
    s_msc.block_pos = 0u;
    s_msc.block_valid = 0u;
    s_msc.csw_status = MSC_CSW_PASS;
    s_msc.state = MSC_DATA_IN;
    if (s_msc.blocks_left == 0u) {
        send_csw();
    } else if ((s_msc.lba + s_msc.blocks_left) > MSC_BLOCK_COUNT ||
               STORAGE_Read_FS(0u, s_msc.block_buf, s_msc.lba, 1u) != 0) {
        set_sense(0x05u, 0x21u, 0x00u);
        s_msc.csw_status = MSC_CSW_FAIL;
        send_csw();
    } else {
        s_msc.block_valid = MSC_BLOCK_BYTES;
        send_bulk_packet(s_msc.block_buf, MSC_BULK_EP_SIZE);
        s_msc.block_pos = MSC_BULK_EP_SIZE;
        s_msc.residue = s_msc.residue >= MSC_BULK_EP_SIZE ? s_msc.residue - MSC_BULK_EP_SIZE : 0u;
    }
}

static void continue_read10(void)
{
    if (s_msc.blocks_left == 0u) {
        send_csw();
        return;
    }

    if (s_msc.block_pos >= s_msc.block_valid) {
        --s_msc.blocks_left;
        ++s_msc.lba;
        s_msc.block_pos = 0u;
        s_msc.block_valid = 0u;
        if (s_msc.blocks_left == 0u) {
            send_csw();
            return;
        }
        if (STORAGE_Read_FS(0u, s_msc.block_buf, s_msc.lba, 1u) != 0) {
            set_sense(0x03u, 0x11u, 0x00u);
            s_msc.csw_status = MSC_CSW_FAIL;
            send_csw();
            return;
        }
        s_msc.block_valid = MSC_BLOCK_BYTES;
    }

    uint16_t remaining = (uint16_t)(s_msc.block_valid - s_msc.block_pos);
    uint16_t chunk = remaining > MSC_BULK_EP_SIZE ? MSC_BULK_EP_SIZE : remaining;
    send_bulk_packet(&s_msc.block_buf[s_msc.block_pos], chunk);
    s_msc.block_pos = (uint16_t)(s_msc.block_pos + chunk);
    s_msc.residue = s_msc.residue >= chunk ? s_msc.residue - chunk : 0u;
}

static void start_write10(const uint8_t *cb)
{
    s_msc.lba = get_be32(&cb[2]);
    s_msc.blocks_left = get_be16(&cb[7]);
    s_msc.block_pos = 0u;
    s_msc.csw_status = MSC_CSW_PASS;
    s_msc.state = MSC_DATA_OUT;

    if ((s_msc.lba + s_msc.blocks_left) > MSC_BLOCK_COUNT) {
        set_sense(0x05u, 0x21u, 0x00u);
        s_msc.csw_status = MSC_CSW_FAIL;
        send_csw();
    } else if (s_msc.blocks_left == 0u) {
        send_csw();
    } else {
        ep_write_status(MSC_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_VALID);
    }
}

static void handle_write_data(const uint8_t *data, uint16_t len)
{
    uint16_t copied = 0u;

    while (copied < len && s_msc.blocks_left > 0u) {
        uint16_t room = (uint16_t)(MSC_BLOCK_BYTES - s_msc.block_pos);
        uint16_t chunk = (uint16_t)(len - copied);
        if (chunk > room) {
            chunk = room;
        }
        memcpy(&s_msc.block_buf[s_msc.block_pos], &data[copied], chunk);
        s_msc.block_pos = (uint16_t)(s_msc.block_pos + chunk);
        copied = (uint16_t)(copied + chunk);
        s_msc.residue = s_msc.residue >= chunk ? s_msc.residue - chunk : 0u;

        if (s_msc.block_pos >= MSC_BLOCK_BYTES) {
            if (STORAGE_Write_FS(0u, s_msc.block_buf, s_msc.lba, 1u) != 0) {
                set_sense(0x03u, 0x0Cu, 0x00u);
                s_msc.csw_status = MSC_CSW_FAIL;
                s_msc.blocks_left = 0u;
                send_csw();
                return;
            }
            s_msc.block_pos = 0u;
            ++s_msc.lba;
            --s_msc.blocks_left;
        }
    }

    if (s_msc.blocks_left == 0u) {
        send_csw();
    } else {
        ep_write_status(MSC_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_VALID);
    }
}

static void handle_cbw(const uint8_t *cbw, uint16_t len)
{
    const uint8_t *cb = &cbw[15];
    uint8_t opcode;

    if (len != 31u || get_le32(&cbw[0]) != MSC_CBW_SIGNATURE) {
        ep_write_status(MSC_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_STALL);
        return;
    }

    s_msc.tag = get_le32(&cbw[4]);
    s_msc.residue = get_le32(&cbw[8]);
    s_msc.csw_status = MSC_CSW_PASS;
    opcode = cb[0];

    switch (opcode) {
    case SCSI_TEST_UNIT_READY:
    case SCSI_START_STOP_UNIT:
    case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
        send_csw();
        break;
    case SCSI_INQUIRY:
        send_scsi_data(s_inquiry_data, sizeof(s_inquiry_data));
        break;
    case SCSI_REQUEST_SENSE:
        send_request_sense();
        break;
    case SCSI_MODE_SENSE6:
        send_mode_sense6();
        break;
    case SCSI_READ_FORMAT_CAPACITIES:
        send_format_capacities();
        break;
    case SCSI_READ_CAPACITY10:
        send_read_capacity();
        break;
    case SCSI_READ10:
        start_read10(cb);
        break;
    case SCSI_WRITE10:
        start_write10(cb);
        break;
    default:
        set_sense(0x05u, 0x20u, 0x00u);
        s_msc.csw_status = MSC_CSW_FAIL;
        send_csw();
        break;
    }
}

static void handle_setup(void)
{
    uint8_t setup_raw[8];
    UsbSetupPacket setup;

    pma_read(setup_raw, PMA_EP0_RX, sizeof(setup_raw));
    parse_setup(setup_raw, &setup);
    ep_write_status(0u, USB_EP_STAT_TX, USB_EP_TX_NAK);

    if ((setup.bmRequestType & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD) {
        handle_standard_request(&setup);
    } else if ((setup.bmRequestType & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS) {
        handle_class_request(&setup);
    } else {
        ep_stall(0u);
    }
}

static void handle_ep0_out(void)
{
    if (s_msc.ep0_state == EP0_STATUS_OUT) {
        s_msc.ep0_state = EP0_IDLE;
    }
    ep0_rx_valid();
}

static void handle_ep0_in(void)
{
    if (s_msc.address_pending != 0u) {
        USB->DADDR = (uint16_t)(USB_DADDR_EF | s_msc.pending_address);
        s_msc.address_pending = 0u;
    }

    if (s_msc.ep0_state == EP0_DATA_IN) {
        if (s_msc.ep0_tx_pos < s_msc.ep0_tx_len) {
            ep0_send_chunk();
        } else {
            ep0_status_out();
        }
    } else if (s_msc.ep0_state == EP0_STATUS_IN) {
        s_msc.ep0_state = EP0_IDLE;
        ep0_rx_valid();
    } else {
        ep0_rx_valid();
    }
}

static void usb_reset(void)
{
    USB->BTABLE = 0u;
    USB->DADDR = USB_DADDR_EF;
    s_msc.configured = 0u;
    s_msc.configuration = 0u;
    s_msc.address_pending = 0u;
    s_msc.pending_address = 0u;
    s_msc.ep1_tx_busy = 0u;
    s_msc.ep0_state = EP0_IDLE;
    s_msc.state = MSC_WAIT_CBW;

    ep_set_type_addr(0u, USB_EP_TYPE_CONTROL, 0u);
    pma_set_tx(0u, PMA_EP0_TX, 0u);
    pma_set_rx(0u, PMA_EP0_RX, MSC_EP0_SIZE);
    ep_write_status(0u, USB_EP_STAT_TX, USB_EP_TX_NAK);
    ep_write_status(0u, USB_EP_STAT_RX, USB_EP_RX_VALID);

    ep_set_type_addr(MSC_IN_EP, USB_EP_TYPE_BULK, MSC_IN_EP);
    pma_set_tx(MSC_IN_EP, PMA_MSC_IN, 0u);
    ep_write_status(MSC_IN_EP, USB_EP_STAT_TX, USB_EP_TX_NAK);
    ep_write_status(MSC_IN_EP, USB_EP_STAT_RX, USB_EP_RX_DIS);

    ep_set_type_addr(MSC_OUT_EP, USB_EP_TYPE_BULK, MSC_OUT_EP);
    pma_set_rx(MSC_OUT_EP, PMA_MSC_OUT, MSC_BULK_EP_SIZE);
    ep_write_status(MSC_OUT_EP, USB_EP_STAT_TX, USB_EP_TX_DIS);
    ep_write_status(MSC_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_VALID);
}

static void handle_ctr(void)
{
    uint16_t istr = USB->ISTR;
    uint8_t ep = (uint8_t)(istr & USB_ISTR_EP_ID);
    uint16_t reg;

    if (ep >= USB_EP_COUNT) {
        return;
    }

    reg = *ep_reg(ep);
    if ((istr & USB_ISTR_DIR) != 0u) {
        if ((reg & USB_EP_CTR_RX) != 0u) {
            ep_clear_ctr_rx(ep);
            if (ep == 0u) {
                if ((reg & USB_EP_SETUP) != 0u) {
                    handle_setup();
                } else {
                    handle_ep0_out();
                }
            } else if (ep == MSC_OUT_EP) {
                uint8_t packet[MSC_BULK_EP_SIZE];
                uint16_t count = pma_get_rx_count(MSC_OUT_EP);
                if (count > sizeof(packet)) {
                    count = sizeof(packet);
                }
                pma_read(packet, PMA_MSC_OUT, count);
                if (s_msc.state == MSC_WAIT_CBW) {
                    handle_cbw(packet, count);
                } else if (s_msc.state == MSC_DATA_OUT) {
                    handle_write_data(packet, count);
                }
                if (s_msc.state == MSC_WAIT_CBW || s_msc.state == MSC_DATA_OUT) {
                    pma_set_rx(MSC_OUT_EP, PMA_MSC_OUT, MSC_BULK_EP_SIZE);
                    ep_write_status(MSC_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_VALID);
                }
            }
        }
    } else if ((reg & USB_EP_CTR_TX) != 0u) {
        ep_clear_ctr_tx(ep);
        if (ep == 0u) {
            handle_ep0_in();
        } else if (ep == MSC_IN_EP) {
            s_msc.ep1_tx_busy = 0u;
            if (s_msc.state == MSC_DATA_IN) {
                if (s_msc.blocks_left > 0u || s_msc.block_pos < s_msc.block_valid) {
                    continue_read10();
                } else {
                    send_csw();
                }
            } else if (s_msc.state == MSC_SEND_CSW) {
                s_msc.state = MSC_WAIT_CBW;
                pma_set_rx(MSC_OUT_EP, PMA_MSC_OUT, MSC_BULK_EP_SIZE);
                ep_write_status(MSC_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_VALID);
            }
        }
    }
}

int UsbMscDevice_Start(void)
{
    memset(&s_msc, 0, sizeof(s_msc));
    s_msc.active = 1u;
    s_msc.state = MSC_WAIT_CBW;
    if (STORAGE_Init_FS(0u) != 0) {
        return -1;
    }

    __HAL_RCC_USB_FORCE_RESET();
    HAL_Delay(1u);
    __HAL_RCC_USB_RELEASE_RESET();
    __HAL_RCC_USB_CLK_ENABLE();

    USB->CNTR = USB_CNTR_FRES | USB_CNTR_PDWN;
    HAL_Delay(2u);
    USB->CNTR = USB_CNTR_FRES;
    HAL_Delay(1u);
    USB->CNTR = 0u;
    USB->ISTR = 0u;
    USB->CNTR = USB_CNTR_CTRM | USB_CNTR_RESETM;

    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(USB_HP_CAN1_TX_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(USB_HP_CAN1_TX_IRQn);

    return 0;
}

void UsbMscDevice_IrqHandler(void)
{
    uint16_t istr;

    if (s_msc.active == 0u) {
        return;
    }

    while (((istr = USB->ISTR) & USB_ISTR_CTR) != 0u) {
        handle_ctr();
    }

    if ((istr & USB_ISTR_RESET) != 0u) {
        USB->ISTR = (uint16_t)~USB_ISTR_RESET;
        usb_reset();
    }
}
