/**
 * Clipboard data-request retry/failure policy for sdl_clip.cpp.
 *
 * Header-only and free of SDL/FreeRDP/WinPR so it can be unit-tested in
 * isolation (test/test_coffee_clip_policy.cpp). The actual request/response
 * plumbing stays in sdlClip::ClipDataCb.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <utility>

namespace coffee::clip
{
using Clock = std::chrono::steady_clock;

/** Attempts per ClipDataCb call (first try included). */
inline constexpr int kMaxDataRequestAttempts = 3;

/** Pause between a failed attempt and the next one. */
inline constexpr std::chrono::milliseconds kRetryDelay{ 150 };

/** How long a fully-failed request keeps short-circuiting identical ones. */
inline constexpr std::chrono::milliseconds kFailureMemoWindow{ 1000 };

/** Whether a failed attempt should be retried.
 *
 *  @param attempt             1-based number of the attempt that just failed
 *  @param maxAttempts         total attempts allowed
 *  @param formatListSuperseded the server sent a newer format list since the
 *                             request was made, so the request is stale and
 *                             retrying it is pointless
 */
[[nodiscard]] constexpr bool shouldRetryDataRequest(int attempt, int maxAttempts,
                                                    bool formatListSuperseded)
{
	return !formatListSuperseded && attempt < maxAttempts;
}

/** Remembers that a (server format, format-list generation) request just
 *  failed for good, for a short window.
 *
 *  After a failed text paste a consumer typically re-asks for several
 *  equivalent mimes (text/plain, UTF8_STRING, STRING, ...) back to back.
 *  They all map to the same server format, so without this each one would
 *  repeat the whole retry sequence against a request the server just
 *  refused, blocking the SDL thread for no benefit. The window is
 *  deliberately short: a genuine second paste attempt a moment later still
 *  goes to the server.
 */
class RecentFailureMemo
{
  public:
	void record(uint32_t formatId, uint64_t generation, Clock::time_point now)
	{
		prune(now);
		_failed[{ formatId, generation }] = now;
	}

	[[nodiscard]] bool recentlyFailed(uint32_t formatId, uint64_t generation,
	                                  Clock::time_point now) const
	{
		const auto it = _failed.find({ formatId, generation });
		return it != _failed.end() && (now - it->second) < kFailureMemoWindow;
	}

	void clear()
	{
		_failed.clear();
	}

  private:
	void prune(Clock::time_point now)
	{
		for (auto it = _failed.begin(); it != _failed.end();)
		{
			if ((now - it->second) >= kFailureMemoWindow)
				it = _failed.erase(it);
			else
				++it;
		}
	}

	std::map<std::pair<uint32_t, uint64_t>, Clock::time_point> _failed;
};
} // namespace coffee::clip
