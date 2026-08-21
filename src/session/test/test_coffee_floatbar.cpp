/**
 * Unit test for coffee_floatbar (PLAN.md Phase 6, steps 6.1/6.2): "static bar
 * renders over the session" / "behaves on both backends" for the auto-hide,
 * hover-reveal, pin and drag logic. Assert-based, matching
 * test_coffee_idle.cpp's style.
 */
#include <cstdio>
#include <cstdlib>

#include "../coffee_floatbar.hpp"

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

// Shared geometry for most tests: a 200px-wide bar in an 800px-wide window,
// so it starts centered at offsetX == (800-200)/2 == 300.
constexpr int kBarHeight = 28;
constexpr int kBarWidth = 200;
constexpr int kWindowWidth = 800;
constexpr int kPeek = 2;
constexpr int kRevealZone = 10;
constexpr int kStep = 4;

void configureDefault(CoffeeFloatbarState& bar)
{
	bar.configure(kBarHeight, kBarWidth, kWindowWidth, kPeek, kRevealZone, kStep);
}

void check_initial_state()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);

	expect(bar.visible(), "configured bar is always at least peek-visible");
	expect(bar.offsetY() == -26, "starts fully retracted, leaving a 2px peek sliver (-28+2)");
	expect(!bar.pinned(), "not pinned by default");
	expect(!bar.dragging(), "not dragging by default");
	expect(bar.barWidth() == kBarWidth, "reports the configured bar width");
	expect(bar.offsetX() == 300, "starts horizontally centered: (800-200)/2 == 300");
}

void check_reveal_on_hover()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);

	bar.noteMouseY(3); // inside the 10px reveal zone
	int lastOffset = bar.offsetY();
	int ticks = 0;
	while (bar.tick())
	{
		expect(bar.offsetY() > lastOffset, "reveal moves the bar toward 0 each tick");
		lastOffset = bar.offsetY();
		if (++ticks > 100)
			break; // guard against an infinite loop on a logic bug
	}
	expect(bar.offsetY() == 0, "settles fully shown once revealed");
	expect(!bar.tick(), "no further change once settled at the target");
}

// Regression test for a real bug found in live use (2026-08-13): the bar
// hid itself out from under the pointer while the user was moving toward a
// button lower in the (already-shown) bar, because "stay open" used the
// same narrow strip as "start opening", and most of the bar's own height
// sits below that strip once shown.
void check_stays_open_while_moving_within_the_bar()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);

	bar.noteMouseY(3); // reveal it
	while (bar.tick())
		;
	expect(bar.offsetY() == 0, "precondition: fully shown");

	// kBarHeight=28, kRevealZone=10 -- y=20 is within the bar but well
	// outside the original narrow reveal strip.
	bar.noteMouseY(20);
	expect(!bar.tick(), "moving within the bar's own height must not start "
	                    "retracting it, even though 20 > the reveal-zone strip");
	expect(bar.offsetY() == 0, "stays fully shown while the pointer is anywhere in the bar");

	// But once it's back to fully retracted, the narrow strip is correctly
	// required again to re-open it -- this isn't "any Y always keeps it open".
	bar.noteMouseLeft();
	while (bar.tick())
		;
	expect(bar.offsetY() == -26, "precondition: retracted again");
	bar.noteMouseY(20); // within the *shown* bar's height, but not the reveal strip
	expect(!bar.tick(), "y=20 does not re-open an already-retracted bar (only the "
	                    "narrow reveal-zone strip does)");
}

void check_retract_when_pointer_leaves()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);

	bar.noteMouseY(3);
	while (bar.tick())
		; // reveal fully first
	expect(bar.offsetY() == 0, "precondition: fully shown");

	bar.noteMouseLeft();
	int ticks = 0;
	while (bar.tick())
	{
		if (++ticks > 100)
			break;
	}
	expect(bar.offsetY() == -26, "retracts back to the peek sliver once the pointer leaves");
}

void check_pin_forces_shown()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);

	bar.noteMouseY(500); // far from the top edge
	expect(!bar.pinned(), "starts unpinned");

	bool nowPinned = bar.togglePinned();
	expect(nowPinned, "togglePinned() flips to pinned and returns the new state");

	int ticks = 0;
	while (bar.tick())
	{
		if (++ticks > 100)
			break;
	}
	expect(bar.offsetY() == 0, "pinned bar stays fully shown even with the pointer far away");

	bar.setPinned(false);
	ticks = 0;
	while (bar.tick())
	{
		if (++ticks > 100)
			break;
	}
	expect(bar.offsetY() == -26, "unpinning re-applies the pointer-driven target");
}

void check_force_shown_independent_of_pinned()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);

	bar.noteMouseY(500); // far from the top edge
	bar.setForceShown(true);

	int ticks = 0;
	while (bar.tick())
	{
		if (++ticks > 100)
			break;
	}
	expect(bar.offsetY() == 0, "forceShown keeps the bar fully shown with the pointer far away");
	expect(!bar.pinned(), "forceShown does not itself flip pinned() -- the pin icon must only "
	                      "reflect an actual user pin, not the dropdown menu happening to be open");

	bar.setForceShown(false);
	ticks = 0;
	while (bar.tick())
	{
		if (++ticks > 100)
			break;
	}
	expect(bar.offsetY() == -26, "clearing forceShown re-applies the pointer-driven target");
}

void check_hit_test()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);
	bar.setPinned(true);
	while (bar.tick())
		;
	expect(bar.offsetY() == 0, "precondition: fully shown at y=0");
	expect(bar.offsetX() == 300, "precondition: centered at x=300 (bar width 200)");

	expect(bar.containsPoint(300, 0), "top-left corner of the centered bar is inside it");
	expect(bar.containsPoint(499, 27), "bottom-right pixel of the centered bar is inside it");
	expect(!bar.containsPoint(500, 0), "x at the bar's right edge is out of bounds (exclusive)");
	expect(!bar.containsPoint(299, 0), "x just left of the bar is out of bounds");
	expect(!bar.containsPoint(300, 28), "y just below the bar height is outside it");
	expect(!bar.containsPoint(0, 0), "unlike a full-width bar, the window's own top-left corner "
	                                 "is NOT inside a centered compact bar");

	bar.setPinned(false);
	bar.noteMouseLeft();
	while (bar.tick())
		;
	expect(bar.offsetY() == -26, "precondition: retracted to the peek sliver");
	expect(bar.containsPoint(300, -26), "the peek sliver itself is still hit-testable");
	expect(!bar.containsPoint(300, 10), "well below the peek sliver is outside it");
}

void check_drag()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);
	bar.setPinned(true); // keep offsetY settled at 0 so this test only exercises X

	// Grab 50px into the bar (bar starts at x=300) and drag it right.
	bar.beginDrag(350);
	expect(bar.dragging(), "beginDrag() starts a drag");

	bar.updateDrag(450);
	expect(bar.offsetX() == 400, "keeps the original grab point (50px in) under the pointer: "
	                              "450 - 50 == 400");

	bar.updateDrag(1000);
	expect(bar.offsetX() == 600, "clamps so the bar never goes past the window's right edge: "
	                              "max offsetX == 800 - 200 == 600");

	bar.updateDrag(-1000);
	expect(bar.offsetX() == 0, "clamps so the bar never goes past the window's left edge");

	bar.endDrag();
	expect(!bar.dragging(), "endDrag() stops the drag");

	bar.updateDrag(300);
	expect(bar.offsetX() == 0, "updateDrag() is a no-op once the drag has ended");
}

void check_drag_forces_shown()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);
	bar.noteMouseY(500); // pointer far from the top edge -- would normally retract

	bar.beginDrag(bar.offsetX());
	int ticks = 0;
	while (bar.tick())
	{
		if (++ticks > 100)
			break;
	}
	expect(bar.offsetY() == 0, "an in-progress drag keeps the bar fully shown, like a pin, even "
	                           "though the pointer isn't in the reveal zone");

	bar.endDrag();
	ticks = 0;
	while (bar.tick())
	{
		if (++ticks > 100)
			break;
	}
	expect(bar.offsetY() == -26, "ending the drag lets it retract again per the last known "
	                             "pointer position");
}

void check_reposition_on_size_change()
{
	CoffeeFloatbarState bar;
	configureDefault(bar);
	expect(bar.offsetX() == 300, "precondition: centered");

	// Not yet dragged -- widening the window should recenter, not clamp.
	bar.setWindowWidth(1000);
	expect(bar.offsetX() == 400, "recenters on window resize until the user has dragged it: "
	                             "(1000-200)/2 == 400");

	bar.beginDrag(bar.offsetX());
	bar.updateDrag(bar.offsetX() + 350); // drag it away from center
	bar.endDrag();
	const int draggedX = bar.offsetX();
	expect(draggedX != 400, "precondition: user has now positioned the bar off-center");

	// Once user-positioned, a resize should reclamp, not recenter back.
	bar.setWindowWidth(1000);
	expect(bar.offsetX() == draggedX, "resize after a user drag preserves position (reclamped, "
	                                  "not recentered)");

	// Shrinking the window below the dragged position must clamp it back on-screen.
	bar.setWindowWidth(250);
	expect(bar.offsetX() == 50, "reclamps to stay fully on-screen when the window shrinks: "
	                            "250 - 200 == 50");
}
} // namespace

int main()
{
	check_initial_state();
	check_reveal_on_hover();
	check_stays_open_while_moving_within_the_bar();
	check_retract_when_pointer_leaves();
	check_pin_forces_shown();
	check_force_shown_independent_of_pinned();
	check_hit_test();
	check_drag();
	check_drag_forces_shown();
	check_reposition_on_size_change();

	if (failures == 0)
		std::printf("OK: all coffee_floatbar checks passed\n");
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
