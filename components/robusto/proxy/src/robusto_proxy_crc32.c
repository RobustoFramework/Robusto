#include "robusto_proxy_crc32.h"

uint32_t robusto_proxy_crc32_iso_hdlc(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    size_t index;
    uint8_t bit;

    for (index = 0; index < length; ++index)
    {
        crc ^= data[index];
        for (bit = 0; bit < 8U; ++bit)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFU;
}