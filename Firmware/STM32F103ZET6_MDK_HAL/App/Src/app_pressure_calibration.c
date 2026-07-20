#include "app_pressure_calibration.h"
#include "app_pressure_calibration_store_logic.h"
#include "bsp_internal_flash.h"

#define CAL_STORE_MAGIC                    0x4C414350u
#define CAL_STORE_SCHEMA                   1u
#define CAL_STORE_HEADER_BYTES             24u
#define CAL_STORE_SLOT_BYTES               36u
#define CAL_STORE_IMAGE_BYTES              (CAL_STORE_HEADER_BYTES + \
                                            (APP_PRESSURE_SENSOR_COUNT * CAL_STORE_SLOT_BYTES))
#define CAL_STORE_CRC_OFFSET               16u
#define CAL_STORE_COMMIT_OFFSET            20u
#define CAL_STORE_COMMIT_VALUE             0xA55Au

typedef struct {
    AppPressureCalibrationProfile profiles[APP_PRESSURE_SENSOR_COUNT];
    uint16_t mask;
    uint32_t sequence;
    uint32_t active_page;
    uint8_t storage_loaded;
    uint8_t storage_fault;
} CalibrationStoreContext;

static CalibrationStoreContext s_store;
static uint8_t s_image[CAL_STORE_IMAGE_BYTES];

static void put_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t crc32_ieee(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0u; i < length; ++i) {
        uint8_t value = data[i];

        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            const uint32_t mix = (crc ^ value) & 1u;
            crc >>= 1;
            if (mix != 0u) {
                crc ^= 0xEDB88320u;
            }
            value >>= 1;
        }
    }
    return ~crc;
}

static uint32_t image_crc(uint8_t *image)
{
    uint32_t crc;
    uint32_t saved_crc = get_u32_le(&image[CAL_STORE_CRC_OFFSET]);

    put_u32_le(&image[CAL_STORE_CRC_OFFSET], 0u);
    crc = crc32_ieee(image, CAL_STORE_IMAGE_BYTES);
    put_u32_le(&image[CAL_STORE_CRC_OFFSET], saved_crc);
    return crc;
}

static uint8_t page_is_erased(uint32_t page)
{
    const uint32_t *words = (const uint32_t *)page;

    for (uint32_t i = 0u; i < (CAL_STORE_IMAGE_BYTES / 4u); ++i) {
        if (words[i] != 0xFFFFFFFFu) {
            return 0u;
        }
    }
    return 1u;
}

static int decode_page(uint32_t page,
                       AppPressureCalibrationProfile *profiles,
                       uint16_t *mask,
                       uint32_t *sequence)
{
    const uint8_t *image = (const uint8_t *)page;
    uint8_t copy[CAL_STORE_IMAGE_BYTES];
    uint16_t decoded_mask;

    if (get_u32_le(&image[0]) != CAL_STORE_MAGIC ||
        get_u16_le(&image[4]) != CAL_STORE_SCHEMA ||
        get_u16_le(&image[6]) != CAL_STORE_IMAGE_BYTES ||
        image[14] != APP_PRESSURE_SENSOR_COUNT ||
        image[15] != APP_PRESSURE_CAL_ANCHOR_COUNT ||
        get_u16_le(&image[CAL_STORE_COMMIT_OFFSET]) != CAL_STORE_COMMIT_VALUE) {
        return -1;
    }
    for (uint32_t i = 0u; i < CAL_STORE_IMAGE_BYTES; ++i) {
        copy[i] = image[i];
    }
    if (image_crc(copy) != get_u32_le(&image[CAL_STORE_CRC_OFFSET])) {
        return -1;
    }

    decoded_mask = (uint16_t)(get_u16_le(&image[12]) & 0x3FFFu);
    for (uint8_t slot = 0u; slot < APP_PRESSURE_SENSOR_COUNT; ++slot) {
        const uint32_t base = CAL_STORE_HEADER_BYTES + ((uint32_t)slot * CAL_STORE_SLOT_BYTES);

        for (uint8_t anchor = 0u; anchor < APP_PRESSURE_CAL_ANCHOR_COUNT; ++anchor) {
            profiles[slot].raw[anchor] = get_u32_le(&image[base + 4u + ((uint32_t)anchor * 4u)]);
            profiles[slot].pressure_001mmhg[anchor] =
                get_u32_le(&image[base + 20u + ((uint32_t)anchor * 4u)]);
        }
        if ((decoded_mask & ((uint16_t)1u << slot)) != 0u &&
            (image[base] == 0u || AppPressureCalibration_ValidateProfile(&profiles[slot]) != 0)) {
            return -1;
        }
    }

    *mask = decoded_mask;
    *sequence = get_u32_le(&image[8]);
    return 0;
}

static void encode_image(uint32_t sequence)
{
    for (uint32_t i = 0u; i < CAL_STORE_IMAGE_BYTES; ++i) {
        s_image[i] = 0u;
    }

    put_u32_le(&s_image[0], CAL_STORE_MAGIC);
    put_u16_le(&s_image[4], CAL_STORE_SCHEMA);
    put_u16_le(&s_image[6], CAL_STORE_IMAGE_BYTES);
    put_u32_le(&s_image[8], sequence);
    put_u16_le(&s_image[12], (uint16_t)(s_store.mask & 0x3FFFu));
    s_image[14] = APP_PRESSURE_SENSOR_COUNT;
    s_image[15] = APP_PRESSURE_CAL_ANCHOR_COUNT;
    put_u16_le(&s_image[CAL_STORE_COMMIT_OFFSET], CAL_STORE_COMMIT_VALUE);

    for (uint8_t slot = 0u; slot < APP_PRESSURE_SENSOR_COUNT; ++slot) {
        const uint32_t base = CAL_STORE_HEADER_BYTES + ((uint32_t)slot * CAL_STORE_SLOT_BYTES);

        s_image[base] = (s_store.mask & ((uint16_t)1u << slot)) != 0u ? 1u : 0u;
        for (uint8_t anchor = 0u; anchor < APP_PRESSURE_CAL_ANCHOR_COUNT; ++anchor) {
            put_u32_le(&s_image[base + 4u + ((uint32_t)anchor * 4u)],
                       s_store.profiles[slot].raw[anchor]);
            put_u32_le(&s_image[base + 20u + ((uint32_t)anchor * 4u)],
                       s_store.profiles[slot].pressure_001mmhg[anchor]);
        }
    }
    put_u32_le(&s_image[CAL_STORE_CRC_OFFSET], 0u);
    put_u32_le(&s_image[CAL_STORE_CRC_OFFSET], image_crc(s_image));
}

static int persist(void)
{
    const uint32_t target = AppPressureCalibration_SelectInactivePage(s_store.active_page);
    const uint32_t next_sequence = s_store.sequence + 1u;
    AppPressureCalibrationProfile verify_profiles[APP_PRESSURE_SENSOR_COUNT];
    uint16_t verify_mask = 0u;
    uint32_t verify_sequence = 0u;

    encode_image(next_sequence);
    if (BspInternalFlash_ErasePage(target) != 0 ||
        BspInternalFlash_ProgramHalfwords(target, s_image, CAL_STORE_COMMIT_OFFSET) != 0 ||
        BspInternalFlash_ProgramHalfwords(target + CAL_STORE_COMMIT_OFFSET + 2u,
                                          &s_image[CAL_STORE_COMMIT_OFFSET + 2u],
                                          CAL_STORE_IMAGE_BYTES - CAL_STORE_COMMIT_OFFSET - 2u) != 0 ||
        BspInternalFlash_ProgramHalfword(target + CAL_STORE_COMMIT_OFFSET,
                                         CAL_STORE_COMMIT_VALUE) != 0 ||
        decode_page(target, verify_profiles, &verify_mask, &verify_sequence) != 0 ||
        verify_mask != s_store.mask || verify_sequence != next_sequence) {
        s_store.storage_fault = 1u;
        return -1;
    }

    s_store.active_page = target;
    s_store.sequence = next_sequence;
    s_store.storage_loaded = 1u;
    s_store.storage_fault = 0u;
    return 0;
}

void AppPressureCalibration_Init(void)
{
    AppPressureCalibrationProfile profiles_a[APP_PRESSURE_SENSOR_COUNT];
    AppPressureCalibrationProfile profiles_b[APP_PRESSURE_SENSOR_COUNT];
    uint16_t mask_a = 0u;
    uint16_t mask_b = 0u;
    uint32_t sequence_a = 0u;
    uint32_t sequence_b = 0u;
    const uint8_t valid_a = decode_page(APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS,
                                        profiles_a, &mask_a, &sequence_a) == 0 ? 1u : 0u;
    const uint8_t valid_b = decode_page(APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS,
                                        profiles_b, &mask_b, &sequence_b) == 0 ? 1u : 0u;
    const uint8_t use_b = valid_b != 0u &&
                          (valid_a == 0u || (int32_t)(sequence_b - sequence_a) > 0) ? 1u : 0u;

    s_store.mask = 0u;
    s_store.sequence = 0u;
    s_store.active_page = APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS;
    s_store.storage_loaded = 0u;
    s_store.storage_fault = 0u;
    for (uint8_t slot = 0u; slot < APP_PRESSURE_SENSOR_COUNT; ++slot) {
        for (uint8_t anchor = 0u; anchor < APP_PRESSURE_CAL_ANCHOR_COUNT; ++anchor) {
            s_store.profiles[slot].raw[anchor] = 0u;
            s_store.profiles[slot].pressure_001mmhg[anchor] = 0u;
        }
    }

    if (valid_a != 0u || valid_b != 0u) {
        const AppPressureCalibrationProfile *source = use_b != 0u ? profiles_b : profiles_a;

        s_store.mask = use_b != 0u ? mask_b : mask_a;
        s_store.sequence = use_b != 0u ? sequence_b : sequence_a;
        s_store.active_page = use_b != 0u ? APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS :
                                             APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS;
        s_store.storage_loaded = 1u;
        for (uint8_t slot = 0u; slot < APP_PRESSURE_SENSOR_COUNT; ++slot) {
            s_store.profiles[slot] = source[slot];
        }
    }
    if ((valid_a == 0u && page_is_erased(APP_PRESSURE_CAL_FLASH_PAGE_A_ADDRESS) == 0u) ||
        (valid_b == 0u && page_is_erased(APP_PRESSURE_CAL_FLASH_PAGE_B_ADDRESS) == 0u)) {
        s_store.storage_fault = 1u;
    }
}

uint16_t AppPressureCalibration_GetMask(void)
{
    return s_store.mask;
}

uint8_t AppPressureCalibration_IsStorageLoaded(void)
{
    return s_store.storage_loaded;
}

uint8_t AppPressureCalibration_HasStorageFault(void)
{
    return s_store.storage_fault;
}

uint32_t AppPressureCalibration_GetStorageSequence(void)
{
    return s_store.sequence;
}

int AppPressureCalibration_Convert(uint8_t slot,
                                   uint32_t raw,
                                   uint32_t *pressure_001mmhg)
{
    if (slot >= APP_PRESSURE_SENSOR_COUNT || pressure_001mmhg == 0 ||
        (s_store.mask & ((uint16_t)1u << slot)) == 0u) {
        return -1;
    }
    *pressure_001mmhg = AppPressureCalibration_ConvertProfile(&s_store.profiles[slot], raw);
    return 0;
}

int AppPressureCalibration_GetProfile(uint8_t slot,
                                      AppPressureCalibrationProfile *profile)
{
    if (slot >= APP_PRESSURE_SENSOR_COUNT || profile == 0 ||
        (s_store.mask & ((uint16_t)1u << slot)) == 0u) {
        return -1;
    }
    *profile = s_store.profiles[slot];
    return 0;
}

int AppPressureCalibration_SaveProfile(uint8_t slot,
                                       const AppPressureCalibrationProfile *profile)
{
    AppPressureCalibrationProfile previous;
    const uint16_t previous_mask = s_store.mask;

    if (slot >= APP_PRESSURE_SENSOR_COUNT || AppPressureCalibration_ValidateProfile(profile) != 0) {
        return -1;
    }
    previous = s_store.profiles[slot];
    s_store.profiles[slot] = *profile;
    s_store.mask |= (uint16_t)1u << slot;
    if (persist() != 0) {
        s_store.profiles[slot] = previous;
        s_store.mask = previous_mask;
        return -1;
    }
    return 0;
}

int AppPressureCalibration_ClearProfile(uint8_t slot)
{
    AppPressureCalibrationProfile previous;
    const uint16_t previous_mask = s_store.mask;

    if (slot >= APP_PRESSURE_SENSOR_COUNT) {
        return -1;
    }
    if (AppPressureCalibration_ClearNeedsPersist(s_store.mask,
                                                 slot,
                                                 s_store.storage_fault) == 0u) {
        return 0;
    }
    previous = s_store.profiles[slot];
    s_store.mask &= (uint16_t)~((uint16_t)1u << slot);
    if (persist() != 0) {
        s_store.profiles[slot] = previous;
        s_store.mask = previous_mask;
        return -1;
    }
    return 0;
}
