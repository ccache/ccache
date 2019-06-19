#define BYTES_FROM_UINT16(bytes, uint16) \
	do { \
		(bytes)[0] = (uint16) >>  8 & 0xFF; \
		(bytes)[1] = (uint16) >>  0 & 0xFF; \
	} while (false)

#define UINT16_FROM_BYTES(bytes) \
	((uint16_t)(uint8_t)(bytes)[0] <<  8 | \
	 (uint16_t)(uint8_t)(bytes)[1] <<  0)

#define BYTES_FROM_UINT32(bytes, uint32) \
	do { \
		(bytes)[0] = (uint32) >> 24 & 0xFF; \
		(bytes)[1] = (uint32) >> 16 & 0xFF; \
		(bytes)[2] = (uint32) >>  8 & 0xFF; \
		(bytes)[3] = (uint32) >>  0 & 0xFF; \
	} while (false)

#define UINT32_FROM_BYTES(bytes) \
	((uint32_t)(uint8_t)(bytes)[0] << 24 | \
	 (uint32_t)(uint8_t)(bytes)[1] << 16 | \
	 (uint32_t)(uint8_t)(bytes)[2] <<  8 | \
	 (uint32_t)(uint8_t)(bytes)[3] <<  0)

#define BYTES_FROM_INT64(bytes, int64) \
	do { \
		(bytes)[0] = (int64) >> 56 & 0xFF; \
		(bytes)[1] = (int64) >> 48 & 0xFF; \
		(bytes)[2] = (int64) >> 40 & 0xFF; \
		(bytes)[3] = (int64) >> 32 & 0xFF; \
		(bytes)[4] = (int64) >> 24 & 0xFF; \
		(bytes)[5] = (int64) >> 16 & 0xFF; \
		(bytes)[6] = (int64) >>  8 & 0xFF; \
		(bytes)[7] = (int64) >>  0 & 0xFF; \
	} while (false)

#define INT64_FROM_BYTES(bytes) \
	((int64_t)(uint8_t)(bytes)[0] << 56 | \
	 (int64_t)(uint8_t)(bytes)[1] << 48 | \
	 (int64_t)(uint8_t)(bytes)[2] << 40 | \
	 (int64_t)(uint8_t)(bytes)[3] << 32 | \
	 (int64_t)(uint8_t)(bytes)[4] << 24 | \
	 (int64_t)(uint8_t)(bytes)[5] << 16 | \
	 (int64_t)(uint8_t)(bytes)[6] <<  8 | \
	 (int64_t)(uint8_t)(bytes)[7] <<  0)

#define BYTES_FROM_UINT64(bytes, uint64) \
	do { \
		(bytes)[0] = (uint64) >> 56 & 0xFF; \
		(bytes)[1] = (uint64) >> 48 & 0xFF; \
		(bytes)[2] = (uint64) >> 40 & 0xFF; \
		(bytes)[3] = (uint64) >> 32 & 0xFF; \
		(bytes)[4] = (uint64) >> 24 & 0xFF; \
		(bytes)[5] = (uint64) >> 16 & 0xFF; \
		(bytes)[6] = (uint64) >>  8 & 0xFF; \
		(bytes)[7] = (uint64) >>  0 & 0xFF; \
	} while (false)

#define UINT64_FROM_BYTES(bytes) \
	((uint64_t)(uint8_t)(bytes)[0] << 56 | \
	 (uint64_t)(uint8_t)(bytes)[1] << 48 | \
	 (uint64_t)(uint8_t)(bytes)[2] << 40 | \
	 (uint64_t)(uint8_t)(bytes)[3] << 32 | \
	 (uint64_t)(uint8_t)(bytes)[4] << 24 | \
	 (uint64_t)(uint8_t)(bytes)[5] << 16 | \
	 (uint64_t)(uint8_t)(bytes)[6] <<  8 | \
	 (uint64_t)(uint8_t)(bytes)[7] <<  0)
