/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * SDL Client
 *
 * Copyright 2022 Armin Novak <armin.novak@thincast.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <map>
#include <memory>
#include <sstream>
#include <vector>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>

#include <freerdp/freerdp.h>

#include <SDL3/SDL.h>

#include <sdl_common_utils.hpp>

#include "sdl_window.hpp"
#include "sdl_disp.hpp"
#include "sdl_clip.hpp"
#include "sdl_input.hpp"

#include "coffee_idle.hpp"

#include "dialogs/sdl_connection_dialog_wrapper.hpp"
#include "dialogs/sdl_floatbar.hpp"

class SdlContext
{
  public:
	enum CursorType
	{
		CURSOR_NULL,
		CURSOR_DEFAULT,
		CURSOR_IMAGE
	};

	explicit SdlContext(rdpContext* context);
	SdlContext(const SdlContext& other) = delete;
	SdlContext(SdlContext&& other) = delete;
	~SdlContext() = default;

	SdlContext& operator=(const SdlContext& other) = delete;
	SdlContext& operator=(SdlContext&& other) = delete;

	[[nodiscard]] bool redraw(bool suppress = false) const;

	void setConnected(bool val);
	[[nodiscard]] bool isConnected() const;
	void cleanup();

	[[nodiscard]] bool resizeable() const;
	[[nodiscard]] bool toggleResizeable();
	[[nodiscard]] bool setResizeable(bool enable);

	[[nodiscard]] bool fullscreen() const;
	[[nodiscard]] bool toggleFullscreen();
	[[nodiscard]] bool setFullscreen(bool enter, bool forceOriginalDisplay = false);

	[[nodiscard]] bool setMinimized();

	[[nodiscard]] bool grabMouse() const;
	[[nodiscard]] bool toggleGrabMouse();
	[[nodiscard]] bool setGrabMouse(bool enter);

	[[nodiscard]] bool grabKeyboard() const;
	[[nodiscard]] bool toggleGrabKeyboard();
	[[nodiscard]] bool setGrabKeyboard(bool enter);

	[[nodiscard]] rdpContext* context() const;
	[[nodiscard]] rdpClientContext* common() const;

	[[nodiscard]] bool setCursor(CursorType type);
	[[nodiscard]] bool setCursor(const rdpPointer* cursor);
	[[nodiscard]] rdpPointer* cursor() const;
	[[nodiscard]] bool restoreCursor();

	void setMonitorIds(const std::vector<SDL_DisplayID>& ids);
	[[nodiscard]] const std::vector<SDL_DisplayID>& monitorIds() const;
	[[nodiscard]] int64_t monitorId(uint32_t index) const;

	void push(std::vector<SDL_Rect>&& rects);
	[[nodiscard]] std::vector<SDL_Rect> pop();

	void setHasCursor(bool val);
	[[nodiscard]] bool hasCursor() const;

	void setMetadata();

	[[nodiscard]] int start();
	[[nodiscard]] int join();
	[[nodiscard]] bool shallAbort(bool ignoreDialogs = false);

	[[nodiscard]] bool createWindows();
	[[nodiscard]] bool updateWindowList();
	[[nodiscard]] bool updateWindow(SDL_WindowID id);

	[[nodiscard]] bool drawToWindows(const std::vector<SDL_Rect>& rects = {});
	[[nodiscard]] bool drawToWindow(SdlWindow& window, const std::vector<SDL_Rect>& rects = {});
	[[nodiscard]] bool minimizeAllWindows();
	[[nodiscard]] int exitCode() const;
	[[nodiscard]] SDL_PixelFormat pixelFormat() const;

	[[nodiscard]] const SdlWindow* getWindowForId(SDL_WindowID id) const;
	[[nodiscard]] SdlWindow* getWindowForId(SDL_WindowID id);
	[[nodiscard]] SdlWindow* getFirstWindow();

	[[nodiscard]] bool addDisplayWindow(SDL_DisplayID id);
	[[nodiscard]] bool removeDisplayWindow(SDL_DisplayID id);
	[[nodiscard]] bool detectDisplays();
	[[nodiscard]] rdpMonitor getDisplay(SDL_DisplayID id) const;
	[[nodiscard]] std::vector<SDL_DisplayID> getDisplayIds() const;

	[[nodiscard]] sdlDispContext& getDisplayChannelContext();
	[[nodiscard]] sdlInput& getInputChannelContext();
	[[nodiscard]] sdlClip& getClipboardChannelContext();

	[[nodiscard]] SdlConnectionDialogWrapper& getDialog();

	[[nodiscard]] wLog* getWLog();

	[[nodiscard]] bool moveMouseTo(const SDL_FPoint& pos);

	[[nodiscard]] SDL_FPoint screenToPixel(SDL_WindowID id, const SDL_FPoint& pos);

	[[nodiscard]] SDL_FPoint pixelToScreen(SDL_WindowID id, const SDL_FPoint& pos);
	[[nodiscard]] SDL_FRect pixelToScreen(SDL_WindowID id, const SDL_FRect& pos,
	                                      bool round = false);

	[[nodiscard]] bool handleEvent(const SDL_Event& ev);

	[[nodiscard]] COMMAND_LINE_ARGUMENT_A* args();
	[[nodiscard]] size_t argsCount() const;

	[[nodiscard]] static int argumentHandler(const COMMAND_LINE_ARGUMENT_A* arg, void* custom);

	[[nodiscard]] CriticalSection& lock();

	[[nodiscard]] std::vector<rdpPointer*>& pointers();
	[[nodiscard]] bool contains(const rdpPointer* ptr) const;

	[[nodiscard]] bool credentialsRead() const;
	void setCredentialsRead();

	/** intervalSeconds == 0 disables the idle keep-alive. `combo` is a
	 *  '+'-separated key combo string (e.g. "alt+tab", see
	 *  coffee_idle_parse_combo()). An unrecognized key name disables the
	 *  keep-alive with a warning rather than failing the connection. */
	void configureIdleKeepAlive(unsigned intervalSeconds, const std::string& combo);

	/** Called from the main event loop on every iteration where no real
	 *  input arrived (see sdl_freerdp.cpp's sdl_run()) -- fires the
	 *  configured combo once the idle interval has elapsed, and keeps
	 *  firing periodically through sustained idleness. */
	void checkIdleKeepAlive();

	/** Wires the floatbar's button actions to real session behavior, then
	 *  blocks until it is actually attached to a window. Called once from
	 *  postConnect(), which -- like the rest of the connect callbacks --
	 *  runs on the RDP background thread (see start()/rdpThreadRun()), not
	 *  the main SDL thread. Attaching touches SDL_Renderer/TTF font
	 *  resources, which on an OpenGL-backed renderer must only happen on the
	 *  thread that owns the GL context; doing that directly here crashed
	 *  ("Unable to make EGL context current") the first time this was wired
	 *  up. Mirrors waitForWindowsCreated()'s exact pattern: post a user
	 *  event, block on a WinPREvent the main thread sets once done. */
	[[nodiscard]] bool configureFloatbar();

	/** Attaches the floatbar to the first session window. Must run on the
	 *  main SDL thread -- see SDL_EVENT_USER_CONFIGURE_FLOATBAR in
	 *  sdl_freerdp.cpp's sdl_run(), configureFloatbar()'s only caller. */
	[[nodiscard]] bool attachFloatbar();

  private:
	/** Draws the floatbar overlay onto `renderer` if `window` is the one it
	 *  is currently attached to (see SDL_EVENT_WINDOW_MOUSE_ENTER handling in
	 *  handleEvent()) -- passed as SdlWindow::updateSurface()'s overlay
	 *  callback so the bar is redrawn on top of every RDP frame update for
	 *  free, without baking it into the persistent RDP render target. */
	void drawFloatbarOverlay(const SdlWindow& window, SDL_Renderer* renderer);

	/** Re-presents the currently-attached floatbar window with no new RDP
	 *  content, just to animate the bar -- called after a mouse-motion tick
	 *  changes its slide offset, since RDP dirty-rect updates alone would
	 *  leave the animation stalled on an otherwise-static remote desktop. */
	void redrawFloatbarOnly();

	/** Floatbar's Keep-alive button: turns the idle keep-alive on/off without
	 *  losing the interval/combo it was originally configured with, so
	 *  re-enabling restores the real setting rather than defaulting. */
	void toggleIdleKeepAlive();

	[[nodiscard]] bool resizeToScale(SdlWindow* window);
	[[nodiscard]] bool useLocalScale() const;

	[[nodiscard]] static BOOL preConnect(freerdp* instance);
	[[nodiscard]] static BOOL postConnect(freerdp* instance);
	static void postDisconnect(freerdp* instance);
	static void postFinalDisconnect(freerdp* instance);
	[[nodiscard]] static BOOL desktopResize(rdpContext* context);
	[[nodiscard]] static BOOL playSound(rdpContext* context, const PLAY_SOUND_UPDATE* play_sound);
	[[nodiscard]] static BOOL beginPaint(rdpContext* context);
	[[nodiscard]] static BOOL endPaint(rdpContext* context);
	[[nodiscard]] static DWORD WINAPI rdpThreadRun(SdlContext* sdl);

	[[nodiscard]] bool eventToPixelCoordinates(SDL_WindowID id, SDL_Event& ev);

	[[nodiscard]] SDL_FPoint applyLocalScaling(const SDL_FPoint& val) const;
	void removeLocalScaling(float& x, float& y) const;

	[[nodiscard]] bool handleEvent(const SDL_WindowEvent& ev);
	[[nodiscard]] bool handleEvent(const SDL_DisplayEvent& ev);
	[[nodiscard]] bool handleEvent(const SDL_MouseButtonEvent& ev);
	[[nodiscard]] bool handleEvent(const SDL_MouseMotionEvent& ev);
	[[nodiscard]] bool handleEvent(const SDL_MouseWheelEvent& ev);
	[[nodiscard]] bool handleEvent(const SDL_TouchFingerEvent& ev);

	void addOrUpdateDisplay(SDL_DisplayID id);
	void deleteDisplay(SDL_DisplayID id);

	[[nodiscard]] bool createPrimary();
	[[nodiscard]] std::string windowTitle() const;
	[[nodiscard]] bool waitForWindowsCreated();

	void sdl_client_cleanup(int exit_code, const std::string& error_msg);
	[[nodiscard]] int sdl_client_thread_connect(std::string& error_msg);
	[[nodiscard]] int sdl_client_thread_run(std::string& error_msg);

	[[nodiscard]] int error_info_to_error(DWORD* pcode, char** msg, size_t* len) const;

	void applyMonitorOffset(SDL_WindowID window, float& x, float& y) const;

	[[nodiscard]] std::vector<SDL_DisplayID>
	updateDisplayOffsetsForNeighbours(SDL_DisplayID id,
	                                  const std::vector<SDL_DisplayID>& ignore = {});
	void updateMonitorDataFromOffsets();

	rdpContext* _context = nullptr;
	wLog* _log = nullptr;

	std::atomic<bool> _connected = false;
	bool _cursor_visible = true;
	std::unique_ptr<rdpPointer, void (*)(rdpPointer*)> _cursor;
	CursorType _cursorType = CURSOR_NULL;
	std::vector<SDL_DisplayID> _monitorIds;
	std::mutex _queue_mux;
	std::queue<std::vector<SDL_Rect>> _queue;
	/* SDL */
	bool _fullscreen = false;
	bool _resizeable = false;
	bool _grabMouse = false;
	/* Defaults on: this now also gates whether ordinary keystrokes forward
	 * to the remote session at all (see sdlInput::handleEvent()), not just
	 * OS-level shortcut capture -- has to start true or typing wouldn't
	 * work out of the box. attachFloatbar() engages the matching real
	 * SDL_SetWindowKeyboardGrab() at connect so the floatbar's "Capture
	 * Kbd: On" default reflects what's actually active, not just the flag. */
	bool _grabKeyboard = true;
	int _exitCode = -1;
	std::atomic<bool> _rdpThreadRunning = false;
	SDL_PixelFormat _sdlPixelFormat = SDL_PIXELFORMAT_UNKNOWN;

	CriticalSection _critical;

	using SDLSurfacePtr = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

	SDLSurfacePtr _primary;
	SDL_FPoint _localScale{ 1.0f, 1.0f };

	sdlDispContext _disp;
	sdlInput _input;
	sdlClip _clip;

	SdlConnectionDialogWrapper _dialog;

	std::map<SDL_DisplayID, rdpMonitor> _displays;
	std::map<SDL_WindowID, SdlWindow> _windows;
	std::map<SDL_DisplayID, std::pair<SDL_Rect, SDL_Rect>> _offsets;

	uint32_t _windowWidth = 0;
	uint32_t _windowHeigth = 0;
	WinPREvent _windowsCreatedEvent;
	std::thread _thread;
	std::vector<COMMAND_LINE_ARGUMENT_A> _args;
	std::vector<rdpPointer*> _valid_pointers;
	bool _credentialsRead = false;

	CoffeeIdleTimer _idleTimer;
	std::vector<UINT32> _idleComboRdpScancodes;
	unsigned _idleConfiguredInterval = 0;
	std::string _idleConfiguredCombo;

	SdlFloatbar _floatbar;
	SDL_WindowID _floatbarWindowId = 0;
	WinPREvent _floatbarConfiguredEvent;
};
