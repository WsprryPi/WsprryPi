#ifndef RP1_GPCLK_CONTRACT_H
#define RP1_GPCLK_CONTRACT_H

#include <linux/rp1_gpclk.h>
#include <linux/math64.h>
#include <linux/overflow.h>

#define RP1_GPCLK_EVENT_MAX_WRITES \
	((u64)RP1_GPCLK_WRITES_PER_SYMBOL * RP1_GPCLK_WSPR_SYMBOL_COUNT)

static inline u64 rp1_gpclk_event_cumulative_writes(u64 duration_ns)
{
	return mul_u64_u64_div_u64(duration_ns,
		RP1_GPCLK_EVENT_MAX_WRITES,
		RP1_GPCLK_WSPR_FRAME_NOMINAL_NS);
}

static inline u32 rp1_gpclk_preserve_terminal_reason(u32 existing, u32 next)
{
	return existing == RP1_GPCLK_TERMINAL_DEADLINE_MISSED ||
		existing == RP1_GPCLK_TERMINAL_ADAPTER_FAILED ? existing : next;
}

static inline void rp1_gpclk_mark_event_failed(u32 *state, u32 *reason,
	bool *submitted, bool *armed, u32 failure_reason)
{
	*state = RP1_GPCLK_STATE_FAILED;
	*reason = rp1_gpclk_preserve_terminal_reason(*reason, failure_reason);
	*submitted = false;
	*armed = false;
}

static inline bool rp1_gpclk_valid_header(u16 version, u16 size,
	size_t expected)
{
	return version == RP1_GPCLK_UAPI_VERSION && size == expected;
}

static inline bool rp1_gpclk_valid_drive(u32 drive)
{
	return drive == 2 || drive == 4 || drive == 8 || drive == 12;
}

static inline u32 rp1_gpclk_pack_fraction(u64 divider_word)
{
	return (divider_word & 0xffffU) << 16;
}

static inline bool rp1_gpclk_valid_frame_elapsed(u64 elapsed_ns)
{
	const u64 nominal = RP1_GPCLK_WSPR_FRAME_NOMINAL_NS;
	const u64 tolerance = RP1_GPCLK_WSPR_FRAME_TOLERANCE_NS;

	return elapsed_ns >= nominal - tolerance &&
		elapsed_ns <= nominal + tolerance;
}

static inline bool rp1_gpclk_valid_program(
	const struct rp1_gpclk_program *request, u64 previous_generation)
{
	u32 i;

	if (!rp1_gpclk_valid_header(request->version, request->size,
			sizeof(*request)) || request->fractional_bits != 16 ||
		request->generation <= previous_generation || request->reserved ||
		request->writes_per_symbol != RP1_GPCLK_WRITES_PER_SYMBOL ||
		request->tick_divider != RP1_GPCLK_TICK_DIVIDER ||
		request->symbol_count != RP1_GPCLK_WSPR_SYMBOL_COUNT ||
		request->tone_count != 4)
		return false;

	for (i = 0; i < request->tone_count; ++i) {
		const struct rp1_gpclk_symbol *symbol = &request->tones[i];

		if (symbol->lower_count + symbol->upper_count !=
				RP1_GPCLK_WRITES_PER_SYMBOL ||
			(symbol->lower_divider_word >> 16) != 3 ||
			(symbol->upper_divider_word >> 16) != 3)
			return false;
	}
	for (i = 0; i < RP1_GPCLK_WSPR_SYMBOL_COUNT; ++i)
		if (request->symbols[i] >= request->tone_count)
			return false;
	return true;
}

static inline bool rp1_gpclk_valid_event_program(
	const struct rp1_gpclk_event_program *request, u64 previous_generation)
{
	u64 total = 0;
	u32 i;

	if (request->version != RP1_GPCLK_EVENT_UAPI_VERSION ||
		request->size != sizeof(*request) || request->fractional_bits != 16 ||
		request->tick_divider != RP1_GPCLK_TICK_DIVIDER ||
		!request->generation || request->generation <= previous_generation ||
		request->flags || request->reserved || !request->tone_count ||
		request->tone_count > RP1_GPCLK_EVENT_MAX_TONES ||
		!request->event_count ||
		request->event_count > RP1_GPCLK_EVENT_MAX_EVENTS)
		return false;
	for (i = 0; i < request->tone_count; ++i) {
		const struct rp1_gpclk_symbol *tone = &request->tones[i];
		u32 count;

		if (!tone->lower_divider_word || !tone->upper_divider_word ||
			(!tone->lower_count && !tone->upper_count) ||
			check_add_overflow(tone->lower_count, tone->upper_count, &count) ||
			(tone->lower_divider_word >> 16) != 3 ||
			(tone->upper_divider_word >> 16) != 3)
			return false;
	}
	for (i = 0; i < request->event_count; ++i) {
		const struct rp1_gpclk_event *event = &request->events[i];

		if (!event->duration_ns || event->reserved ||
			(event->flags & ~RP1_GPCLK_EVENT_RF_ON) ||
			((event->flags & RP1_GPCLK_EVENT_RF_ON) &&
			 event->tone_index >= request->tone_count) ||
			check_add_overflow(total, event->duration_ns, &total))
			return false;
	}
	if (total != request->total_duration_ns ||
		rp1_gpclk_event_cumulative_writes(total) >
			RP1_GPCLK_EVENT_MAX_WRITES)
		return false;
	total = 0;
	for (i = 0; i < request->event_count; ++i) {
		u64 next = total + request->events[i].duration_ns;

		if (rp1_gpclk_event_cumulative_writes(next) ==
			rp1_gpclk_event_cumulative_writes(total))
			return false;
		total = next;
	}
	return true;
}

#endif
