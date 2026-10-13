/**
 * Unit test for coffee_clip_policy.hpp: retry decision and the short-lived
 * failure memo used by sdlClip::ClipDataCb. Assert-based, matching
 * test_coffee_idle.cpp's style.
 */
#include <cstdio>
#include <cstdlib>

#include "../coffee_clip_policy.hpp"

namespace
{
int failures = 0;

void expect(bool cond, const char* what)
{
	if (!cond)
	{
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

void check_retry_decision()
{
	using coffee::clip::shouldRetryDataRequest;

	expect(shouldRetryDataRequest(1, 3, false), "first failure of 3 is retried");
	expect(shouldRetryDataRequest(2, 3, false), "second failure of 3 is retried");
	expect(!shouldRetryDataRequest(3, 3, false), "last attempt is not retried");
	expect(!shouldRetryDataRequest(1, 1, false), "single-attempt budget never retries");
	expect(!shouldRetryDataRequest(1, 3, true), "stale request (newer format list) is not retried");
	expect(!shouldRetryDataRequest(2, 3, true), "stale request is not retried on any attempt");
	expect(coffee::clip::kMaxDataRequestAttempts >= 2, "retry budget actually allows a retry");
}

void check_failure_memo()
{
	using namespace coffee::clip;
	using namespace std::chrono_literals;

	const auto t0 = Clock::time_point{} + 10s;

	RecentFailureMemo memo;
	expect(!memo.recentlyFailed(13, 1, t0), "empty memo reports nothing failed");

	memo.record(13, 1, t0);
	expect(memo.recentlyFailed(13, 1, t0), "failure visible immediately");
	expect(memo.recentlyFailed(13, 1, t0 + kFailureMemoWindow - 1ms),
	       "failure visible just inside the window");
	expect(!memo.recentlyFailed(13, 1, t0 + kFailureMemoWindow),
	       "failure expires at the window edge (a real re-paste goes to the server)");

	expect(!memo.recentlyFailed(14, 1, t0), "other server format is unaffected");
	expect(!memo.recentlyFailed(13, 2, t0), "newer format-list generation is unaffected");

	memo.record(14, 1, t0 + 5s);
	expect(!memo.recentlyFailed(13, 1, t0 + 5s), "old entry stays expired after later record");
	expect(memo.recentlyFailed(14, 1, t0 + 5s), "later record is tracked");

	memo.clear();
	expect(!memo.recentlyFailed(14, 1, t0 + 5s), "clear() forgets everything");
}
} // namespace

int main()
{
	check_retry_decision();
	check_failure_memo();

	if (failures)
	{
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	std::puts("coffee_clip_policy: all checks passed");
	return EXIT_SUCCESS;
}
