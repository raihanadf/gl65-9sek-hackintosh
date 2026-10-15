// verbatim copy of the queue check from filterInterruptRequest, built for userspace
#include <stdint.h>
#include <stdbool.h>
bool pending(uint8_t *queues) {
	uint8_t* completionQueues[] = {queues + 0x40, queues + 0x10};
	bool pending = false;
	for (int i = 0; i < 2; i++) {
		uint8_t *cq = completionQueues[i];
		uint16_t head = *(volatile uint16_t*)(cq + 0x08);
		uint8_t *entries = *(uint8_t* volatile*)(cq + 0x10);
		uint32_t phase = *(volatile uint32_t*)(cq + 0x18);
		uint16_t status = *(volatile uint16_t*)(entries + (uint32_t)head * 16 + 0x0e);
		pending |= ((status ^ (phase >> 1)) & 1) == 0;
	}
	return pending;
}
