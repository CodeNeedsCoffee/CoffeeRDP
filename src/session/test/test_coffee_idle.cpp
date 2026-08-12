/**
 * Unit test for coffee_idle (PLAN.md Phase 3, steps 3.1/3.2):
 * "log timer resets while typing" / "combo fires after threshold with no
 * input". Assert-based, matching test_coffee_quality.cpp's style.
 */
#include <cstdio>
#include <cstdlib>

#include "../coffee_idle.hpp"

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

void check_combo_parse()
{
	auto altTab = coffee_idle_parse_combo("alt+tab");
	expect(altTab.size() == 2 && altTab[0] == "Left Alt" && altTab[1] == "Tab",
	       "parse alt+tab -> Left Alt, Tab");

	auto winTab = coffee_idle_parse_combo("WIN+Tab");
	expect(winTab.size() == 2 && winTab[0] == "Left GUI" && winTab[1] == "Tab",
	       "parse WIN+Tab (case-insensitive) -> Left GUI, Tab");

	auto spaced = coffee_idle_parse_combo(" alt_l + tab ");
	expect(spaced.size() == 2 && spaced[0] == "Left Alt" && spaced[1] == "Tab",
	       "parse tolerates surrounding whitespace");

	auto ctrlAltDel = coffee_idle_parse_combo("ctrl+alt+del");
	expect(ctrlAltDel.size() == 3 && ctrlAltDel[0] == "Left Ctrl" && ctrlAltDel[1] == "Left Alt" &&
	           ctrlAltDel[2] == "Delete",
	       "parse three-key combo ctrl+alt+del");

	auto passthrough = coffee_idle_parse_combo("Left Alt+F4");
	expect(passthrough.size() == 2 && passthrough[0] == "Left Alt" && passthrough[1] == "F4",
	       "unrecognized token passed through unchanged for caller-side SDL validation");

	expect(coffee_idle_parse_combo("").empty(), "empty combo string yields empty list");
}

void check_timer()
{
	using namespace std::chrono;
	CoffeeIdleTimer t;

	// Disabled by default / with 0.
	expect(!t.enabled(), "disabled before configure()");
	t.configure(0);
	auto t0 = steady_clock::now();
	t.noteInput(t0);
	expect(!t.due(t0 + seconds(600)), "0 = disabled, never due regardless of elapsed time");

	// 30s interval, matching the user's actual Remmina config.
	t.configure(30);
	t.noteInput(t0);
	expect(!t.due(t0 + seconds(10)), "not due before interval elapses");
	expect(!t.due(t0 + seconds(29)), "not due at 29s");
	expect(t.due(t0 + seconds(31)), "due once interval has elapsed");

	// Fires again after another full interval of continued idleness.
	expect(!t.due(t0 + seconds(31) + seconds(29)), "not due again before a further 30s");
	expect(t.due(t0 + seconds(31) + seconds(31)), "due again after a further 30s idle");

	// Real input resets the countdown -- this is the Remmina bug we avoid:
	// their timer only resets on fire, so it fires every interval even
	// while the user is actively typing.
	auto t1 = t0 + seconds(1000);
	t.noteInput(t1);
	expect(!t.due(t1 + seconds(29)), "input resets the timer: not due right after typing");
	expect(t.due(t1 + seconds(31)), "due 30s after the reset, not 30s after the original start");
}
} // namespace

int main()
{
	check_combo_parse();
	check_timer();

	if (failures == 0)
		std::printf("OK: all coffee_idle checks passed\n");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
