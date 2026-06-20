#include "usb_cdc_device.h"

#include "app_config.h"
#include "app_usb_control.h"
#include <stddef.h>
#include <string.h>

#define USB_PMA_ACCESS_WORDS             2u
#define USB_EP_COUNT                     8u

#define USB_EP_CTR_RX                    0x8000u
#define USB_EP_DTOG_RX                   0x4000u
#define USB_EP_STAT_RX                   0x3000u
#define USB_EP_SETUP                     0x0800u
#define USB_EP_TYPE_MASK                 0x0600u
#define USB_EP_KIND                      0x0100u
#define USB_EP_CTR_TX                    0x0080u
#define USB_EP_DTOG_TX                   0x0040u
#define USB_EP_STAT_TX                   0x0030u
#define USB_EPADDR_FIELD                 0x000Fu

#define USB_EP_TYPE_CONTROL              0x0200u
#define USB_EP_TYPE_ISO                  0x0400u
#define USB_EP_TYPE_BULK                 0x0000u
#define USB_EP_TYPE_INTERRUPT            0x0600u

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

#define CDC_EP0_SIZE                     64u
#define CDC_DATA_EP_SIZE                 64u
#define CDC_CMD_EP_SIZE                  8u
#define CDC_DATA_IN_EP                   1u
#define CDC_DATA_OUT_EP                  2u
#define CDC_CMD_IN_EP                    3u

#define PMA_EP0_TX                       0x0040u
#define PMA_EP0_RX                       0x0080u
#define PMA_CDC_IN                       0x00C0u
#define PMA_CDC_OUT                      0x0100u
#define PMA_CDC_CMD                      0x0140u

#define USB_REQ_TYPE_MASK                0x60u
#define USB_REQ_TYPE_STANDARD            0x00u
#define USB_REQ_TYPE_CLASS               0x20u
#define USB_REQ_RECIP_MASK               0x1Fu
#define USB_REQ_RECIP_DEVICE             0x00u
#define USB_REQ_RECIP_INTERFACE          0x01u

#define USB_REQ_GET_STATUS               0x00u
#define USB_REQ_CLEAR_FEATURE            0x01u
#define USB_REQ_SET_ADDRESS              0x05u
#define USB_REQ_GET_DESCRIPTOR           0x06u
#define USB_REQ_SET_DESCRIPTOR           0x07u
#define USB_REQ_GET_CONFIGURATION        0x08u
#define USB_REQ_SET_CONFIGURATION        0x09u
#define USB_REQ_GET_INTERFACE            0x0Au
#define USB_REQ_SET_INTERFACE            0x0Bu

#define USB_DESC_TYPE_DEVICE             0x01u
#define USB_DESC_TYPE_CONFIGURATION      0x02u
#define USB_DESC_TYPE_STRING             0x03u
#define USB_DESC_TYPE_DEVICE_QUALIFIER   0x06u

#define CDC_SET_LINE_CODING              0x20u
#define CDC_GET_LINE_CODING              0x21u
#define CDC_SET_CONTROL_LINE_STATE       0x22u

#define USB_RX_RING_SIZE                 512u
#define USB_TX_RING_SIZE                 512u

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
    EP0_DATA_OUT,
    EP0_STATUS_IN,
    EP0_STATUS_OUT,
    EP0_STALL
} Ep0State;

typedef struct {
    volatile uint8_t active;
    volatile uint8_t configured;
    volatile uint8_t address_pending;
    volatile uint8_t pending_address;
    volatile uint8_t ep1_tx_busy;
    volatile uint8_t msc_reboot_pending;
    volatile uint8_t tx_ring_busy;
    Ep0State ep0_state;
    uint8_t configuration;
    uint8_t line_coding[7];
    uint8_t ep0_buf[CDC_EP0_SIZE];
    const uint8_t *ep0_tx;
    uint16_t ep0_tx_len;
    uint16_t ep0_tx_pos;
    uint8_t ep0_rx_expected;
    uint8_t rx_ring[USB_RX_RING_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    uint8_t tx_ring[USB_TX_RING_SIZE];
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;
} UsbCdcContext;

static UsbCdcContext s_usb_cdc;

static const uint8_t s_device_descriptor[] = {
    0x12, USB_DESC_TYPE_DEVICE,
    0x00, 0x02,
    0x02, 0x00, 0x00,
    CDC_EP0_SIZE,
    0x83, 0x04,
    0x40, 0x57,
    0x00, 0x01,
    0x01, 0x02, 0x03,
    0x01
};

static const uint8_t s_configuration_descriptor[] = {
    0x09, USB_DESC_TYPE_CONFIGURATION,
    0x43, 0x00,
    0x02,
    0x01,
    0x00,
    0x80,
    0x32,

    0x09, 0x04,
    0x00,
    0x00,
    0x01,
    0x02,
    0x02,
    0x01,
    0x00,

    0x05, 0x24,
    0x00,
    0x10, 0x01,

    0x05, 0x24,
    0x01,
    0x00,
    0x01,

    0x04, 0x24,
    0x02,
    0x02,

    0x05, 0x24,
    0x06,
    0x00,
    0x01,

    0x07, 0x05,
    (uint8_t)(0x80u | CDC_CMD_IN_EP),
    0x03,
    CDC_CMD_EP_SIZE, 0x00,
    0x10,

    0x09, 0x04,
    0x01,
    0x00,
    0x02,
    0x0A,
    0x00,
    0x00,
    0x00,

    0x07, 0x05,
    CDC_DATA_OUT_EP,
    0x02,
    CDC_DATA_EP_SIZE, 0x00,
    0x00,

    0x07, 0x05,
    (uint8_t)(0x80u | CDC_DATA_IN_EP),
    0x02,
    CDC_DATA_EP_SIZE, 0x00,
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
    'F', 0, 'i', 0, 'x', 0, 't', 0, 'u', 0, 'r', 0, 'e', 0, ' ', 0, 'C', 0, 'D', 0, 'C', 0
};

static const uint8_t s_serial_string[] = {
    18, USB_DESC_TYPE_STRING,
    'P', 0, 'F', 0, '2', 0, '0', 0, '2', 0, '6', 0, '0', 0, '1', 0
};

static volatile uint16_t *ep_reg(uint8_t ep)
{
    return &USB->EP0R + ((uint32_t)ep * 2u);
}

static volatile uint16_t *pma_ptr(uint16_t addr)
{
    return (__IO uint16_t *)(USB_BASE + 0x400u + ((uint32_t)addr * USB_PMA_ACCESS_WORDS));
}

static volatile uint16_t *pma_addr_ptr(uint16_t pma_addr)
{
    return pma_ptr(pma_addr);
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
    s_usb_cdc.ep0_state = EP0_STALL;
}

static void ep0_rx_valid(void)
{
    pma_set_rx(0u, PMA_EP0_RX, CDC_EP0_SIZE);
    ep_write_status(0u, USB_EP_STAT_RX, USB_EP_RX_VALID);
}

static void ep0_send_chunk(void)
{
    uint16_t remaining = (uint16_t)(s_usb_cdc.ep0_tx_len - s_usb_cdc.ep0_tx_pos);
    uint16_t chunk = remaining > CDC_EP0_SIZE ? CDC_EP0_SIZE : remaining;

    if (chunk > 0u) {
        pma_write(&s_usb_cdc.ep0_tx[s_usb_cdc.ep0_tx_pos], PMA_EP0_TX, chunk);
    }
    pma_set_tx(0u, PMA_EP0_TX, chunk);
    s_usb_cdc.ep0_tx_pos = (uint16_t)(s_usb_cdc.ep0_tx_pos + chunk);
    s_usb_cdc.ep0_state = EP0_DATA_IN;
    ep_write_status(0u, USB_EP_STAT_TX, USB_EP_TX_VALID);
}

static void ep0_send_data(const uint8_t *data, uint16_t len, uint16_t requested)
{
    if (len > requested) {
        len = requested;
    }
    s_usb_cdc.ep0_tx = data;
    s_usb_cdc.ep0_tx_len = len;
    s_usb_cdc.ep0_tx_pos = 0u;
    ep0_send_chunk();
}

static void ep0_status_in(void)
{
    pma_set_tx(0u, PMA_EP0_TX, 0u);
    s_usb_cdc.ep0_state = EP0_STATUS_IN;
    ep_write_status(0u, USB_EP_STAT_TX, USB_EP_TX_VALID);
}

static void ep0_status_out(void)
{
    s_usb_cdc.ep0_state = EP0_STATUS_OUT;
    ep0_rx_valid();
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void parse_setup(const uint8_t *buf, UsbSetupPacket *setup)
{
    setup->bmRequestType = buf[0];
    setup->bRequest = buf[1];
    setup->wValue = get_le16(&buf[2]);
    setup->wIndex = get_le16(&buf[4]);
    setup->wLength = get_le16(&buf[6]);
}

static void push_rx_bytes(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0u; i < len; ++i) {
        uint16_t next = (uint16_t)((s_usb_cdc.rx_head + 1u) % USB_RX_RING_SIZE);
        if (next == s_usb_cdc.rx_tail) {
            break;
        }
        s_usb_cdc.rx_ring[s_usb_cdc.rx_head] = data[i];
        s_usb_cdc.rx_head = next;
    }
}

static uint16_t tx_ring_count(void)
{
    uint16_t head = s_usb_cdc.tx_head;
    uint16_t tail = s_usb_cdc.tx_tail;
    return head >= tail ? (uint16_t)(head - tail) : (uint16_t)(USB_TX_RING_SIZE - tail + head);
}

static uint16_t tx_ring_pop(uint8_t *out, uint16_t max_len)
{
    uint16_t count = 0u;

    while (count < max_len && s_usb_cdc.tx_tail != s_usb_cdc.tx_head) {
        out[count++] = s_usb_cdc.tx_ring[s_usb_cdc.tx_tail];
        s_usb_cdc.tx_tail = (uint16_t)((s_usb_cdc.tx_tail + 1u) % USB_TX_RING_SIZE);
    }

    return count;
}

static void cdc_try_send_next(void)
{
    uint8_t packet[CDC_DATA_EP_SIZE];
    uint16_t len;

    if (s_usb_cdc.configured == 0u || s_usb_cdc.ep1_tx_busy != 0u) {
        return;
    }
    if (tx_ring_count() == 0u) {
        return;
    }

    len = tx_ring_pop(packet, sizeof(packet));
    if (len == 0u) {
        return;
    }

    pma_write(packet, PMA_CDC_IN, len);
    pma_set_tx(CDC_DATA_IN_EP, PMA_CDC_IN, len);
    s_usb_cdc.ep1_tx_busy = 1u;
    ep_write_status(CDC_DATA_IN_EP, USB_EP_STAT_TX, USB_EP_TX_VALID);
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
        s_usb_cdc.pending_address = (uint8_t)setup->wValue;
        s_usb_cdc.address_pending = 1u;
        ep0_status_in();
        return;

    case USB_REQ_SET_CONFIGURATION:
        if (setup->wLength != 0u || setup->wValue > 1u) {
            ep_stall(0u);
            return;
        }
        s_usb_cdc.configuration = (uint8_t)setup->wValue;
        s_usb_cdc.configured = s_usb_cdc.configuration != 0u ? 1u : 0u;
        ep0_status_in();
        return;

    case USB_REQ_GET_CONFIGURATION:
        ep0_send_data(&s_usb_cdc.configuration, 1u, setup->wLength);
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
    switch (setup->bRequest) {
    case CDC_SET_LINE_CODING:
        if (setup->wLength != sizeof(s_usb_cdc.line_coding)) {
            ep_stall(0u);
            return;
        }
        s_usb_cdc.ep0_rx_expected = sizeof(s_usb_cdc.line_coding);
        s_usb_cdc.ep0_state = EP0_DATA_OUT;
        ep0_rx_valid();
        return;

    case CDC_GET_LINE_CODING:
        ep0_send_data(s_usb_cdc.line_coding, sizeof(s_usb_cdc.line_coding), setup->wLength);
        return;

    case CDC_SET_CONTROL_LINE_STATE:
        ep0_status_in();
        return;

    default:
        ep_stall(0u);
        return;
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
    } else if ((setup.bmRequestType & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS &&
               (setup.bmRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_INTERFACE) {
        handle_class_request(&setup);
    } else {
        ep_stall(0u);
    }
}

static void handle_ep0_out(void)
{
    uint16_t count = pma_get_rx_count(0u);

    if (s_usb_cdc.ep0_state == EP0_DATA_OUT &&
        s_usb_cdc.ep0_rx_expected == sizeof(s_usb_cdc.line_coding) &&
        count == sizeof(s_usb_cdc.line_coding)) {
        pma_read(s_usb_cdc.line_coding, PMA_EP0_RX, count);
        s_usb_cdc.ep0_rx_expected = 0u;
        ep0_status_in();
    } else if (s_usb_cdc.ep0_state == EP0_STATUS_OUT) {
        s_usb_cdc.ep0_state = EP0_IDLE;
        ep0_rx_valid();
    } else {
        ep0_rx_valid();
    }
}

static void handle_ep0_in(void)
{
    if (s_usb_cdc.address_pending != 0u) {
        USB->DADDR = (uint16_t)(USB_DADDR_EF | s_usb_cdc.pending_address);
        s_usb_cdc.address_pending = 0u;
    }

    if (s_usb_cdc.ep0_state == EP0_DATA_IN) {
        if (s_usb_cdc.ep0_tx_pos < s_usb_cdc.ep0_tx_len) {
            ep0_send_chunk();
        } else {
            ep0_status_out();
        }
    } else if (s_usb_cdc.ep0_state == EP0_STATUS_IN) {
        s_usb_cdc.ep0_state = EP0_IDLE;
        ep0_rx_valid();
    } else {
        ep0_rx_valid();
    }
}

static void usb_reset(void)
{
    USB->BTABLE = 0u;
    USB->DADDR = USB_DADDR_EF;
    s_usb_cdc.configured = 0u;
    s_usb_cdc.configuration = 0u;
    s_usb_cdc.address_pending = 0u;
    s_usb_cdc.pending_address = 0u;
    s_usb_cdc.ep1_tx_busy = 0u;
    s_usb_cdc.ep0_state = EP0_IDLE;

    ep_set_type_addr(0u, USB_EP_TYPE_CONTROL, 0u);
    pma_set_tx(0u, PMA_EP0_TX, 0u);
    pma_set_rx(0u, PMA_EP0_RX, CDC_EP0_SIZE);
    ep_write_status(0u, USB_EP_STAT_TX, USB_EP_TX_NAK);
    ep_write_status(0u, USB_EP_STAT_RX, USB_EP_RX_VALID);

    ep_set_type_addr(CDC_DATA_IN_EP, USB_EP_TYPE_BULK, CDC_DATA_IN_EP);
    pma_set_tx(CDC_DATA_IN_EP, PMA_CDC_IN, 0u);
    ep_write_status(CDC_DATA_IN_EP, USB_EP_STAT_TX, USB_EP_TX_NAK);
    ep_write_status(CDC_DATA_IN_EP, USB_EP_STAT_RX, USB_EP_RX_DIS);

    ep_set_type_addr(CDC_DATA_OUT_EP, USB_EP_TYPE_BULK, CDC_DATA_OUT_EP);
    pma_set_rx(CDC_DATA_OUT_EP, PMA_CDC_OUT, CDC_DATA_EP_SIZE);
    ep_write_status(CDC_DATA_OUT_EP, USB_EP_STAT_TX, USB_EP_TX_DIS);
    ep_write_status(CDC_DATA_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_VALID);

    ep_set_type_addr(CDC_CMD_IN_EP, USB_EP_TYPE_INTERRUPT, CDC_CMD_IN_EP);
    pma_set_tx(CDC_CMD_IN_EP, PMA_CDC_CMD, 0u);
    ep_write_status(CDC_CMD_IN_EP, USB_EP_STAT_TX, USB_EP_TX_NAK);
    ep_write_status(CDC_CMD_IN_EP, USB_EP_STAT_RX, USB_EP_RX_DIS);
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
            } else if (ep == CDC_DATA_OUT_EP) {
                uint8_t packet[CDC_DATA_EP_SIZE];
                uint16_t count = pma_get_rx_count(CDC_DATA_OUT_EP);
                if (count > sizeof(packet)) {
                    count = sizeof(packet);
                }
                pma_read(packet, PMA_CDC_OUT, count);
                push_rx_bytes(packet, count);
                pma_set_rx(CDC_DATA_OUT_EP, PMA_CDC_OUT, CDC_DATA_EP_SIZE);
                ep_write_status(CDC_DATA_OUT_EP, USB_EP_STAT_RX, USB_EP_RX_VALID);
            }
        }
    } else {
        if ((reg & USB_EP_CTR_TX) != 0u) {
            ep_clear_ctr_tx(ep);
            if (ep == 0u) {
                handle_ep0_in();
            } else if (ep == CDC_DATA_IN_EP) {
                s_usb_cdc.ep1_tx_busy = 0u;
                cdc_try_send_next();
            }
        }
    }
}

#if !APP_PC_LINK_JLINK_UART8_ENABLED && !APP_PC_LINK_JLINK_RTT_ENABLED

int UsbCdcControl_Start(void)
{
    memset(&s_usb_cdc, 0, sizeof(s_usb_cdc));
    s_usb_cdc.active = 1u;
    s_usb_cdc.line_coding[0] = 0x00u;
    s_usb_cdc.line_coding[1] = 0xC2u;
    s_usb_cdc.line_coding[2] = 0x01u;
    s_usb_cdc.line_coding[3] = 0x00u;
    s_usb_cdc.line_coding[4] = 0x00u;
    s_usb_cdc.line_coding[5] = 0x00u;
    s_usb_cdc.line_coding[6] = 0x08u;

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

int UsbCdcControl_Read(uint8_t *data, uint16_t max_len)
{
    uint16_t count = 0u;

    if (data == 0 || max_len == 0u) {
        return 0;
    }

    __disable_irq();
    while (count < max_len && s_usb_cdc.rx_tail != s_usb_cdc.rx_head) {
        data[count++] = s_usb_cdc.rx_ring[s_usb_cdc.rx_tail];
        s_usb_cdc.rx_tail = (uint16_t)((s_usb_cdc.rx_tail + 1u) % USB_RX_RING_SIZE);
    }
    __enable_irq();

    return (int)count;
}

int UsbCdcControl_Write(const uint8_t *data, uint16_t len)
{
    uint16_t written = 0u;

    if (data == 0 || len == 0u || s_usb_cdc.configured == 0u) {
        return 0;
    }

    __disable_irq();
    while (written < len) {
        uint16_t next = (uint16_t)((s_usb_cdc.tx_head + 1u) % USB_TX_RING_SIZE);
        if (next == s_usb_cdc.tx_tail) {
            break;
        }
        s_usb_cdc.tx_ring[s_usb_cdc.tx_head] = data[written++];
        s_usb_cdc.tx_head = next;
    }
    __enable_irq();

    cdc_try_send_next();
    return (int)written;
}

void UsbCdcControl_RequestMscReboot(void)
{
    s_usb_cdc.msc_reboot_pending = 1u;
}

uint8_t UsbCdcControl_IsMscRebootPending(void)
{
    return s_usb_cdc.msc_reboot_pending;
}

#endif

void UsbCdcDevice_IrqHandler(void)
{
    uint16_t istr;

    if (s_usb_cdc.active == 0u) {
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
