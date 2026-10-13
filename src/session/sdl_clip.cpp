/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * SDL Client keyboard helper
 *
 * Copyright 2024 Armin Novak <armin.novak@thincast.com>
 * Copyright 2024 Thincast Technologies GmbH
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

#include <string>
#include <sstream>
#include <mutex>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <utility>

#include <winpr/wlog.h>
#include <winpr/image.h>

#include "sdl_clip.hpp"
#include "sdl_context.hpp"

#define TAG CLIENT_TAG("sdl.cliprdr")

#define mime_text_plain "text/plain"
// NOLINTNEXTLINE(bugprone-suspicious-missing-comma)
const char mime_text_utf8[] = mime_text_plain ";charset=utf-8";

[[nodiscard]] static const std::vector<const char*>& s_mime_text()
{
	static std::vector<const char*> values;
	if (values.empty())
	{
		values = std::vector<const char*>(
		    { mime_text_plain, mime_text_utf8, "UTF8_STRING", "COMPOUND_TEXT", "TEXT", "STRING" });
	}
	return values;
}

static const char s_mime_png[] = "image/png";
static const char s_mime_webp[] = "image/webp";
static const char s_mime_jpg[] = "image/jpeg";

static const char s_mime_tiff[] = "image/tiff";
static const char s_mime_uri_list[] = "text/uri-list";
static const char s_mime_html[] = "text/html";

#define BMP_MIME_LIST "image/bmp", "image/x-bmp", "image/x-MS-bmp", "image/x-win-bitmap"

[[nodiscard]] static const std::vector<const char*>& s_mime_bitmap()
{
	static std::vector<const char*> values;
	if (values.empty())
	{
		values = std::vector<const char*>({ BMP_MIME_LIST });
	}
	return values;
}

[[nodiscard]] static const std::vector<const char*>& s_mime_image()
{
	static std::vector<const char*> values;
	if (values.empty())
	{
		if (winpr_image_format_is_supported(WINPR_IMAGE_WEBP))
			values.push_back(s_mime_webp);

		if (winpr_image_format_is_supported(WINPR_IMAGE_PNG))
			values.push_back(s_mime_png);

		if (winpr_image_format_is_supported(WINPR_IMAGE_JPEG))
			values.push_back(s_mime_jpg);

		auto bmp = std::vector<const char*>({ s_mime_tiff, BMP_MIME_LIST });
		values.insert(values.end(), bmp.begin(), bmp.end());
	}
	return values;
}

static const char s_mime_gnome_copied_files[] = "x-special/gnome-copied-files";
static const char s_mime_mate_copied_files[] = "x-special/mate-copied-files";

static const char s_mime_freerdp_update[] = "x-special/freerdp-clipboard-update";

static const char* s_type_HtmlFormat = "HTML Format";
static const char* s_type_FileGroupDescriptorW = "FileGroupDescriptorW";

class ClipboardLockGuard
{
  public:
	explicit ClipboardLockGuard(wClipboard* clipboard) : _clipboard(clipboard)
	{
		ClipboardLock(_clipboard);
	}
	ClipboardLockGuard(const ClipboardLockGuard& other) = delete;
	ClipboardLockGuard(ClipboardLockGuard&& other) = delete;

	ClipboardLockGuard& operator=(const ClipboardLockGuard& rhs) = delete;
	ClipboardLockGuard& operator=(ClipboardLockGuard&& rhs) = delete;

	~ClipboardLockGuard()
	{
		ClipboardUnlock(_clipboard);
	}

  private:
	wClipboard* _clipboard;
};

/* One summary line per clipboard transfer, written when the transfer's scope
 * ends so no exit path can forget it: direction, mime, server format, outcome,
 * size, attempts, format-list generation and elapsed time. Success is INFO,
 * anything else WARN, which makes "which paste failed, and why" a single
 * grep of the journal. An exit nobody labelled reports "unspecified-exit" --
 * that itself means a code path is missing its label.
 *
 * Never logs clipboard contents (they can hold passwords): sizes and names
 * only. */
class TransferTrace
{
  public:
	TransferTrace(wLog* log, const char* direction, std::string mime = {})
	    : _log(log), _direction(direction), _mime(std::move(mime)),
	      _start(coffee::clip::Clock::now())
	{
	}
	TransferTrace(const TransferTrace&) = delete;
	TransferTrace& operator=(const TransferTrace&) = delete;

	~TransferTrace()
	{
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		                    coffee::clip::Clock::now() - _start)
		                    .count();
		WLog_Print(_log, _ok ? WLOG_INFO : WLOG_WARN,
		           "clipboard %s mime=%s format=%" PRIu32 "[%s] outcome=%s bytes=%" PRIu64
		           " attempts=%d generation=%" PRIu64 " elapsed=%lldms",
		           _direction, _mime.empty() ? "-" : _mime.c_str(), _format, _formatName.c_str(),
		           _outcome, _bytes, _attempts, _generation, static_cast<long long>(ms));
	}

	void mime(std::string m)
	{
		_mime = std::move(m);
	}
	void format(uint32_t id, const char* name)
	{
		_format = id;
		_formatName = name ? name : "-";
	}
	void generation(uint64_t g)
	{
		_generation = g;
	}
	void attempts(int a)
	{
		_attempts = a;
	}
	void ok(const char* outcome, uint64_t bytes)
	{
		_outcome = outcome;
		_bytes = bytes;
		_ok = true;
	}
	void fail(const char* outcome)
	{
		_outcome = outcome;
		_ok = false;
	}

  private:
	wLog* _log;
	const char* _direction;
	std::string _mime;
	coffee::clip::Clock::time_point _start;
	uint32_t _format = 0;
	std::string _formatName = "-";
	const char* _outcome = "unspecified-exit";
	uint64_t _bytes = 0;
	int _attempts = 0;
	uint64_t _generation = 0;
	bool _ok = false;
};

static bool operator<(const CLIPRDR_FORMAT& lhs, const CLIPRDR_FORMAT& rhs)
{
	return (lhs.formatId < rhs.formatId);
}

static bool operator==(const CLIPRDR_FORMAT& lhs, const CLIPRDR_FORMAT& rhs)
{
	return (lhs.formatId == rhs.formatId);
}

sdlClip::sdlClip(SdlContext* sdl)
    : _sdl(sdl), _file(cliprdr_file_context_new(this)), _log(WLog_Get(TAG)),
      _system(ClipboardCreate()), _event(CreateEventA(nullptr, TRUE, FALSE, nullptr)),
      _uuid(sdl::utils::generate_uuid_v4())
{
	WINPR_ASSERT(sdl);

	std::stringstream ss;
	ss << s_mime_freerdp_update << "-" << _uuid;
	_mime_uuid = ss.str();

	std::ignore = cliprdr_file_context_set_locally_available(_file, TRUE);
}

sdlClip::~sdlClip()
{
	cliprdr_file_context_free(_file);
	ClipboardDestroy(_system);
	std::ignore = CloseHandle(_event);
}

bool sdlClip::init(CliprdrClientContext* clip)
{
	WINPR_ASSERT(clip);
	_ctx = clip;
	clip->custom = this;
	_ctx->MonitorReady = sdlClip::MonitorReady;
	_ctx->ServerCapabilities = sdlClip::ReceiveServerCapabilities;
	_ctx->ServerFormatList = sdlClip::ReceiveServerFormatList;
	_ctx->ServerFormatListResponse = sdlClip::ReceiveFormatListResponse;
	_ctx->ServerFormatDataRequest = sdlClip::ReceiveFormatDataRequest;
	_ctx->ServerFormatDataResponse = sdlClip::ReceiveFormatDataResponse;

	WLog_Print(_log, WLOG_INFO, "clipboard channel initialised");
	return cliprdr_file_context_init(_file, _ctx);
}

bool sdlClip::uninit(CliprdrClientContext* clip)
{
	WINPR_ASSERT(clip);
	if (!cliprdr_file_context_uninit(_file, _ctx))
		return false;

	{
		/* ClipDataCb's retry loop can be mid-wait/retry on another thread
		 * when the channel goes down; take the same lock it holds around
		 * _ctx use so it always observes either a fully-valid _ctx or a
		 * cleanly-null one, never a half-torn-down pointer. */
		std::scoped_lock lock(_lock);
		_ctx = nullptr;
	}
	clip->custom = nullptr;
	WLog_Print(_log, WLOG_INFO, "clipboard channel closed (generation=%" PRIu64 ")",
	           _serverFormatGeneration.load());
	return true;
}

bool sdlClip::contains(const char** mime_types, Sint32 count)
{
	for (Sint32 x = 0; x < count; x++)
	{
		const auto mime = mime_types[x];
		if (mime && (strcmp(_mime_uuid.c_str(), mime) == 0))
			return true;
	}
	return false;
}

bool sdlClip::handleEvent(const SDL_ClipboardEvent& ev)
{
	if (!_ctx || !_sync || ev.owner)
	{
		_last_timestamp = ev.timestamp;
		if (!_current_mimetypes.empty())
		{
			_cache_data.clear();
			auto rc =
			    SDL_SetClipboardData(sdlClip::ClipDataCb, sdlClip::ClipCleanCb, this, ev.mime_types,
			                         WINPR_ASSERTING_INT_CAST(size_t, ev.num_mime_types));
			if (!rc)
				WLog_Print(_log, WLOG_WARN,
				           "SDL_SetClipboardData failed for the server clipboard (%d mime types): %s",
				           static_cast<int>(ev.num_mime_types), SDL_GetError());
			else
				WLog_Print(_log, WLOG_INFO,
				           "server clipboard offered to the desktop: %d mime types, generation=%" PRIu64,
				           static_cast<int>(ev.num_mime_types), _serverFormatGeneration.load());
			_current_mimetypes.clear();
			return rc;
		}
		return true;
	}

	if (ev.timestamp == _last_timestamp)
	{
		return true;
	}

	if (contains(ev.mime_types, ev.num_mime_types))
	{
		return true;
	}

	clearServerFormats();

	std::string mime_html = s_mime_html;

	std::vector<std::string> mime_bitmap = { BMP_MIME_LIST };
	std::string mime_webp = s_mime_webp;
	std::string mime_png = s_mime_png;
	std::string mime_jpeg = s_mime_jpg;
	std::string mime_tiff = s_mime_tiff;
	std::vector<std::string> mime_images = { mime_webp, mime_png, mime_jpeg, mime_tiff };

	std::vector<std::string> clientFormatNames;
	std::vector<CLIPRDR_FORMAT> clientFormats;

	size_t nformats = WINPR_ASSERTING_INT_CAST(size_t, ev.num_mime_types);
	const char** clipboard_mime_formats = ev.mime_types;

	WLog_Print(_log, WLOG_TRACE, "SDL has %" PRIuz " formats", nformats);

	bool textPushed = false;
	bool imgPushed = false;
	bool filePushed = false;

	for (size_t i = 0; i < nformats; i++)
	{
		std::string local_mime = clipboard_mime_formats[i];
		WLog_Print(_log, WLOG_TRACE, " - %s", local_mime.c_str());

		if (std::find(s_mime_text().begin(), s_mime_text().end(), local_mime) !=
		    s_mime_text().end())
		{
			/* text formats */
			if (!textPushed)
			{
				clientFormats.push_back({ CF_TEXT, nullptr });
				clientFormats.push_back({ CF_OEMTEXT, nullptr });
				clientFormats.push_back({ CF_UNICODETEXT, nullptr });
				textPushed = true;
			}
		}
		else if (local_mime == mime_html)
			/* html */
			clientFormatNames.emplace_back(s_type_HtmlFormat);
		else if ((std::find(mime_bitmap.begin(), mime_bitmap.end(), local_mime) !=
		          mime_bitmap.end()) ||
		         (std::find(mime_images.begin(), mime_images.end(), local_mime) !=
		          mime_images.end()))
		{
			/* image formats */
			if (!imgPushed)
			{
				clientFormats.push_back({ CF_DIB, nullptr });
#if defined(WINPR_UTILS_IMAGE_DIBv5)
				clientFormats.push_back({ CF_DIBV5, nullptr });
#endif

				for (auto& bmp : mime_bitmap)
					clientFormatNames.push_back(bmp);

				for (auto& img : mime_images)
					clientFormatNames.push_back(img);

				clientFormatNames.emplace_back(s_type_HtmlFormat);
				imgPushed = true;
			}
		}
		else if (mime_is_file(local_mime))
		{
			if (!filePushed)
			{
				clientFormatNames.emplace_back(s_type_FileGroupDescriptorW);
				filePushed = true;
			}
		}
	}

	for (auto& name : clientFormatNames)
	{
		clientFormats.push_back({ ClipboardRegisterFormat(_system, name.c_str()), name.data() });
	}

	std::sort(clientFormats.begin(), clientFormats.end(),
	          [](const auto& a, const auto& b) { return a < b; });
	auto u = std::unique(clientFormats.begin(), clientFormats.end());
	clientFormats.erase(u, clientFormats.end());

	const CLIPRDR_FORMAT_LIST formatList = {
		{ CB_FORMAT_LIST, 0, 0 },
		static_cast<UINT32>(clientFormats.size()),
		clientFormats.data(),
	};

	WLog_Print(_log, WLOG_TRACE,
	           "-------------- client format list [%" PRIu32 "] ------------------",
	           formatList.numFormats);
	for (UINT32 x = 0; x < formatList.numFormats; x++)
	{
		auto format = &formatList.formats[x];
		WLog_Print(_log, WLOG_TRACE, "client announces %" PRIu32 " [%s][%s]", format->formatId,
		           ClipboardGetFormatIdString(format->formatId), format->formatName);
	}

	if (cliprdr_file_context_notify_new_client_format_list(_file) != CHANNEL_RC_OK)
	{
		WLog_Print(_log, WLOG_WARN, "file clipboard refused the new local format list");
		return false;
	}

	WINPR_ASSERT(_ctx);
	WINPR_ASSERT(_ctx->ClientFormatList);
	const UINT sent = _ctx->ClientFormatList(_ctx, &formatList);
	if (sent != CHANNEL_RC_OK)
		WLog_Print(_log, WLOG_WARN, "sending the local format list failed with 0x%08" PRIx32,
		           static_cast<uint32_t>(sent));
	else
		WLog_Print(_log, WLOG_INFO,
		           "local clipboard announced to server: %" PRIu32 " formats from %d mime types",
		           formatList.numFormats, static_cast<int>(ev.num_mime_types));
	return sent == CHANNEL_RC_OK;
}

UINT sdlClip::MonitorReady(CliprdrClientContext* context, const CLIPRDR_MONITOR_READY* monitorReady)
{
	WINPR_UNUSED(monitorReady);
	WINPR_ASSERT(context);
	WINPR_ASSERT(monitorReady);

	auto clipboard = static_cast<sdlClip*>(
	    cliprdr_file_context_get_context(static_cast<CliprdrFileContext*>(context->custom)));
	WINPR_ASSERT(clipboard);

	auto ret = clipboard->SendClientCapabilities();
	if (ret != CHANNEL_RC_OK)
		return ret;

	clipboard->_sync = true;
	WLog_Print(clipboard->_log, WLOG_INFO, "clipboard monitor ready, sync enabled");
	if (!sdl_push_user_event(SDL_EVENT_CLIPBOARD_UPDATE))
	{
		WLog_Print(clipboard->_log, WLOG_ERROR, "could not queue the initial clipboard update");
		return ERROR_INTERNAL_ERROR;
	}

	return CHANNEL_RC_OK;
}

UINT sdlClip::SendClientCapabilities()
{
	CLIPRDR_GENERAL_CAPABILITY_SET generalCapabilitySet = {
		CB_CAPSTYPE_GENERAL, 12, CB_CAPS_VERSION_2,
		CB_USE_LONG_FORMAT_NAMES | cliprdr_file_context_current_flags(_file)
	};
	const CLIPRDR_CAPABILITIES capabilities = {
		{ CB_TYPE_NONE, 0, 0 }, 1, reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&generalCapabilitySet)
	};

	WINPR_ASSERT(_ctx);
	WINPR_ASSERT(_ctx->ClientCapabilities);
	return _ctx->ClientCapabilities(_ctx, &capabilities);
}

void sdlClip::clearServerFormats()
{
	/* Same lock order as ClipDataCb (clipboard system, then _lock): this runs
	 * on the channel/main thread while ClipDataCb may be mid-request on the
	 * SDL thread, and both touch the cache and the flags below. */
	ClipboardLockGuard systemlock(_system);
	std::scoped_lock lock(_lock);
	_serverFormats.clear();
	_cache_data.clear();
	_systemHoldsServerData = false;
	_failureMemo.clear();
	cliprdr_file_context_clear(_file);
	++_serverFormatGeneration;
}

UINT sdlClip::SendFormatListResponse(BOOL status)
{
	const CLIPRDR_FORMAT_LIST_RESPONSE formatListResponse = {
		{ CB_FORMAT_LIST_RESPONSE, static_cast<UINT16>(status ? CB_RESPONSE_OK : CB_RESPONSE_FAIL),
		  0 }
	};
	WINPR_ASSERT(_ctx);
	WINPR_ASSERT(_ctx->ClientFormatListResponse);
	return _ctx->ClientFormatListResponse(_ctx, &formatListResponse);
}

UINT sdlClip::SendDataResponse(const BYTE* data, size_t size)
{
	CLIPRDR_FORMAT_DATA_RESPONSE response = {};

	if (size > UINT32_MAX)
		return ERROR_INVALID_PARAMETER;

	response.common.msgFlags = (data) ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
	response.common.dataLen = static_cast<UINT32>(size);
	response.requestedFormatData = data;

	WINPR_ASSERT(_ctx);
	WINPR_ASSERT(_ctx->ClientFormatDataResponse);
	return _ctx->ClientFormatDataResponse(_ctx, &response);
}

UINT sdlClip::SendDataRequest(uint32_t formatID, const std::string& mime)
{
	const CLIPRDR_FORMAT_DATA_REQUEST request = { { CB_TYPE_NONE, 0, 0 }, formatID };

	/* Channel may have gone away concurrently (uninit() nulls _ctx under
	 * this same lock on disconnect) -- fail cleanly instead of dereferencing
	 * a null context. */
	if (!_ctx)
	{
		WLog_Print(_log, WLOG_WARN, "clipboard channel no longer available, cancelling request");
		return ERROR_INVALID_HANDLE;
	}

	_request_queue.emplace(formatID, mime);

	WINPR_ASSERT(_ctx->ClientFormatDataRequest);
	UINT ret = _ctx->ClientFormatDataRequest(_ctx, &request);
	if (ret != CHANNEL_RC_OK)
	{
		WLog_Print(_log, WLOG_ERROR,
				           "error sending ClientFormatDataRequest for format %" PRIu32
				           " (0x%08" PRIx32 "), cancelling request",
				           formatID, static_cast<uint32_t>(ret));
		_request_queue.pop();
	}

	return ret;
}

std::string sdlClip::getServerFormat(uint32_t id)
{
	for (auto& fmt : _serverFormats)
	{
		if (fmt.formatId() == id)
		{
			if (fmt.formatName())
				return fmt.formatName();
			break;
		}
	}

	return "";
}

uint32_t sdlClip::serverIdForMime(const std::string& mime)
{
	std::string cmp = mime;
	if (mime_is_html(mime))
		cmp = s_type_HtmlFormat;
	if (mime_is_file(mime))
		cmp = s_type_FileGroupDescriptorW;

	for (auto& format : _serverFormats)
	{
		if (!format.formatName())
			continue;
		if (cmp == format.formatName())
			return format.formatId();
	}

	/* Standard predefined formats (CF_DIB, CF_UNICODETEXT, ...) are plain
	 * numeric IDs on the wire with no name, so the named-format loop above
	 * can never confirm the server actually offers one -- it only rules
	 * out named/registered formats. Previously this fell through to
	 * guessing CF_UNICODETEXT/CF_DIB unconditionally for any text/image
	 * mime request, even when the server's format list was e.g. a plain
	 * file copy with no text representation at all, producing a doomed
	 * ClientFormatDataRequest the server always fails. Only guess a
	 * standard format ID if the server's list genuinely includes it. */
	auto hasFormatId = [this](uint32_t id)
	{
		for (auto& format : _serverFormats)
		{
			if (format.formatId() == id)
				return true;
		}
		return false;
	};

	if (mime_is_image(mime) && hasFormatId(CF_DIB))
		return CF_DIB;
	if (mime_is_text(mime))
	{
		if (hasFormatId(CF_UNICODETEXT))
			return CF_UNICODETEXT;
		if (hasFormatId(CF_TEXT))
			return CF_TEXT;
		if (hasFormatId(CF_OEMTEXT))
			return CF_OEMTEXT;
	}

	return 0;
}

UINT sdlClip::ReceiveServerCapabilities(CliprdrClientContext* context,
                                        const CLIPRDR_CAPABILITIES* capabilities)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(capabilities);

	auto capsPtr = reinterpret_cast<const BYTE*>(capabilities->capabilitySets);
	WINPR_ASSERT(capsPtr);

	auto clipboard = static_cast<sdlClip*>(
	    cliprdr_file_context_get_context(static_cast<CliprdrFileContext*>(context->custom)));
	WINPR_ASSERT(clipboard);

	if (!cliprdr_file_context_remote_set_flags(clipboard->_file, 0))
		return ERROR_INTERNAL_ERROR;

	for (UINT32 i = 0; i < capabilities->cCapabilitiesSets; i++)
	{
		auto caps = reinterpret_cast<const CLIPRDR_CAPABILITY_SET*>(capsPtr);

		if (caps->capabilitySetType == CB_CAPSTYPE_GENERAL)
		{
			auto generalCaps = reinterpret_cast<const CLIPRDR_GENERAL_CAPABILITY_SET*>(caps);

			if (!cliprdr_file_context_remote_set_flags(clipboard->_file, generalCaps->generalFlags))
				return ERROR_INTERNAL_ERROR;
		}

		capsPtr += caps->capabilitySetLength;
	}

	return CHANNEL_RC_OK;
}

UINT sdlClip::ReceiveServerFormatList(CliprdrClientContext* context,
                                      const CLIPRDR_FORMAT_LIST* formatList)
{
	BOOL html = FALSE;
	BOOL text = FALSE;
	BOOL image = FALSE;
	BOOL file = FALSE;

	if (!context || !context->custom)
		return ERROR_INVALID_PARAMETER;

	auto filecontext = static_cast<CliprdrFileContext*>(context->custom);
	auto clipboard = static_cast<sdlClip*>(cliprdr_file_context_get_context(filecontext));
	WINPR_ASSERT(clipboard);

	clipboard->clearServerFormats();

	for (UINT32 i = 0; i < formatList->numFormats; i++)
	{
		const CLIPRDR_FORMAT* format = &formatList->formats[i];

		clipboard->_serverFormats.emplace_back(format->formatId, format->formatName);

		if (format->formatName)
		{
			if (strcmp(format->formatName, s_type_HtmlFormat) == 0)
			{
				text = TRUE;
				html = TRUE;
			}
			else if (strcmp(format->formatName, s_type_FileGroupDescriptorW) == 0)
			{
				file = TRUE;
				text = TRUE;
			}
		}
		else
		{
			switch (format->formatId)
			{
				case CF_TEXT:
				case CF_OEMTEXT:
				case CF_UNICODETEXT:
					text = TRUE;
					break;

				case CF_DIB:
					image = TRUE;
					break;

				default:
					break;
			}
		}
	}

	{
		/* Names/ids only, never contents. Windows Explorer rebroadcasts
		 * this list repeatedly after a copy, which is the usual cause of
		 * refused data requests -- see ClipDataCb. */
		std::string summary;
		const UINT32 shown = std::min<UINT32>(formatList->numFormats, 12);
		for (UINT32 i = 0; i < shown; i++)
		{
			const CLIPRDR_FORMAT* f = &formatList->formats[i];
			if (i)
				summary += ", ";
			summary += f->formatName ? f->formatName : ClipboardGetFormatIdString(f->formatId);
		}
		if (formatList->numFormats > shown)
			summary += ", ...";
		WLog_Print(clipboard->_log, WLOG_INFO,
		           "server clipboard changed: generation=%" PRIu64 " formats=%" PRIu32 " [%s]",
		           clipboard->_serverFormatGeneration.load(), formatList->numFormats,
		           summary.c_str());
	}

	clipboard->_current_mimetypes.clear();

	{
		ClipboardLockGuard systemlock(clipboard->_system);
		std::scoped_lock lock(clipboard->_lock);
		auto res = cliprdr_file_context_notify_new_server_format_list(filecontext);
		if (res != CHANNEL_RC_OK)
			return res;
	}

	if (text)
	{
		clipboard->_current_mimetypes.insert(clipboard->_current_mimetypes.end(),
		                                     s_mime_text().begin(), s_mime_text().end());
	}
	if (image)
	{
		clipboard->_current_mimetypes.insert(clipboard->_current_mimetypes.end(),
		                                     s_mime_bitmap().begin(), s_mime_bitmap().end());
		clipboard->_current_mimetypes.insert(clipboard->_current_mimetypes.end(),
		                                     s_mime_image().begin(), s_mime_image().end());
	}
	if (html)
	{
		clipboard->_current_mimetypes.push_back(s_mime_html);
	}
	if (file)
	{
		clipboard->_current_mimetypes.push_back(s_mime_uri_list);
		clipboard->_current_mimetypes.push_back(s_mime_gnome_copied_files);
		clipboard->_current_mimetypes.push_back(s_mime_mate_copied_files);
	}
	clipboard->_current_mimetypes.push_back(clipboard->_mime_uuid.c_str());

	auto s = clipboard->_current_mimetypes.size();
	SDL_Event ev = { SDL_EVENT_CLIPBOARD_UPDATE };
	ev.clipboard.owner = true;
	ev.clipboard.timestamp = SDL_GetTicksNS();
	ev.clipboard.num_mime_types = WINPR_ASSERTING_INT_CAST(Sint32, s);
	ev.clipboard.mime_types = clipboard->_current_mimetypes.data();

	auto rc = (SDL_PushEvent(&ev) == 1);
	return clipboard->SendFormatListResponse(rc);
}

UINT sdlClip::ReceiveFormatListResponse(WINPR_ATTR_UNUSED CliprdrClientContext* context,
                                        const CLIPRDR_FORMAT_LIST_RESPONSE* formatListResponse)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(formatListResponse);

	if (formatListResponse->common.msgFlags & CB_RESPONSE_FAIL)
		WLog_WARN(TAG, "format list update failed");
	return CHANNEL_RC_OK;
}

[[nodiscard]]
static const char* getCurrentTextMime()
{

	for (auto m : s_mime_text())
	{
		if (SDL_HasClipboardData(m))
			return m;
	}
	return nullptr;
}

[[nodiscard]]
static const char* getCurrentImageMime()
{
	for (auto m : s_mime_image())
	{
		if (SDL_HasClipboardData(m))
			return m;
	}
	return nullptr;
}

[[nodiscard]]
static const char* getCurrentBitmapMime()
{
	for (auto m : s_mime_bitmap())
	{
		if (SDL_HasClipboardData(m))
			return m;
	}
	return nullptr;
}

std::shared_ptr<BYTE> sdlClip::ReceiveFormatDataRequestHandle(
    sdlClip* clipboard, const CLIPRDR_FORMAT_DATA_REQUEST* formatDataRequest, uint32_t& len)
{
	const char* mime = nullptr;
	UINT32 formatId = 0;

	BOOL res = FALSE;

	std::shared_ptr<BYTE> data;

	WINPR_ASSERT(clipboard);
	WINPR_ASSERT(formatDataRequest);

	len = 0;
	auto localFormatId = formatId = formatDataRequest->requestedFormatId;
	WLog_Print(clipboard->_log, WLOG_DEBUG, "Requesting format %s [0x%08" PRIx32 "] [%s]",
	           ClipboardGetFormatIdString(localFormatId), localFormatId,
	ClipboardGetFormatName(clipboard->_system, localFormatId));

	/* Declared before the locks so it is destroyed (and logs) after they are
	 * released. */
	TransferTrace trace(clipboard->_log, "local->server");
	trace.format(formatId, ClipboardGetFormatName(clipboard->_system, formatId));
	trace.generation(clipboard->_serverFormatGeneration);

	ClipboardLockGuard systemlock(clipboard->_system);
	std::scoped_lock lock(clipboard->_lock);

	const UINT32 fileFormatId =
	    ClipboardGetFormatId(clipboard->_system, s_type_FileGroupDescriptorW);
	const UINT32 htmlFormatId = ClipboardGetFormatId(clipboard->_system, s_type_HtmlFormat);

	switch (formatId)
	{
		case CF_TEXT:
		case CF_OEMTEXT:
		case CF_UNICODETEXT:
			mime = getCurrentTextMime();
			if (!mime)
			{
				trace.fail("no-local-text");
				return {};
			}
			localFormatId = ClipboardGetFormatId(clipboard->_system, mime);
			break;

		case CF_DIB:
		case CF_DIBV5:
			mime = getCurrentBitmapMime();
			if (!mime)
				mime = getCurrentImageMime();
			if (!mime)
			{
				trace.fail("no-local-image");
				return {};
			}
			localFormatId = ClipboardGetFormatId(clipboard->_system, mime);
			break;

		case CF_TIFF:
			mime = s_mime_tiff;
			break;

		default:
			if (formatId == fileFormatId)
			{
				localFormatId = ClipboardGetFormatId(clipboard->_system, s_mime_uri_list);
				mime = s_mime_uri_list;
			}
			else if (formatId == htmlFormatId)
			{
				/* In case HTML format was requested but we only have images in local clipboard */
				if (!SDL_HasClipboardData(s_mime_html))
				{
					mime = getCurrentImageMime();
					if (mime)
						localFormatId = ClipboardGetFormatId(clipboard->_system, mime);
				}
				else
				{
					localFormatId = ClipboardGetFormatId(clipboard->_system, s_mime_html);
					mime = s_mime_html;
				}
			}
			else
			{
				const char* formatName = ClipboardGetFormatName(clipboard->_system, formatId);
				if (formatName && SDL_HasClipboardData(formatName))
				{
					localFormatId = ClipboardGetFormatId(clipboard->_system, formatName);
					mime = formatName;
				}
				else
				{
					trace.fail("no-local-data-for-format");
					return data;
				}
			}
			break;
	}

	trace.mime(mime ? mime : "");

	{
		size_t size = 0;
		auto sdldata = std::shared_ptr<void>(SDL_GetClipboardData(mime, &size), SDL_free);
		if (!sdldata)
		{
			trace.fail("sdl-clipboard-empty");
			return data;
		}

		if (fileFormatId == formatId)
		{
			auto bdata = static_cast<const char*>(sdldata.get());
			if (!cliprdr_file_context_update_client_data(clipboard->_file, bdata, size))
			{
				trace.fail("file-list-update-failed");
				return data;
			}
		}

		res = ClipboardSetData(clipboard->_system, localFormatId, sdldata.get(),
		                       static_cast<uint32_t>(size));
	}

	if (!res)
	{
		trace.fail("clipboard-store-failed");
		return data;
	}

	uint32_t ptrlen = 0;
	auto ptr = static_cast<BYTE*>(ClipboardGetData(clipboard->_system, formatId, &ptrlen));
	data = std::shared_ptr<BYTE>(ptr, free);

	if (!data)
	{
		trace.fail("conversion-failed");
		return data;
	}

	if (fileFormatId == formatId)
	{
		BYTE* ddata = nullptr;
		UINT32 dsize = 0;
		const UINT32 flags = cliprdr_file_context_remote_get_flags(clipboard->_file);
		const UINT32 error = cliprdr_serialize_file_list_ex(
		    flags, reinterpret_cast<const FILEDESCRIPTORW*>(data.get()),
		    ptrlen / sizeof(FILEDESCRIPTORW), &ddata, &dsize);
		data.reset();
		auto tmp = std::shared_ptr<BYTE>(ddata, free);
		if (error)
		{
			trace.fail("file-serialize-failed");
			return data;
		}

		data = tmp;
		len = dsize;
	}
	else
		len = ptrlen;
	trace.ok("sent", len);
	return data;
}

UINT sdlClip::ReceiveFormatDataRequest(CliprdrClientContext* context,
                                       const CLIPRDR_FORMAT_DATA_REQUEST* formatDataRequest)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(formatDataRequest);

	auto clipboard = static_cast<sdlClip*>(
	    cliprdr_file_context_get_context(static_cast<CliprdrFileContext*>(context->custom)));
	WINPR_ASSERT(clipboard);

	uint32_t len = 0;
	auto rc = ReceiveFormatDataRequestHandle(clipboard, formatDataRequest, len);
	return clipboard->SendDataResponse(rc.get(), len);
}

UINT sdlClip::ReceiveFormatDataResponse(CliprdrClientContext* context,
                                        const CLIPRDR_FORMAT_DATA_RESPONSE* formatDataResponse)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(formatDataResponse);

	const UINT32 size = formatDataResponse->common.dataLen;
	const BYTE* data = formatDataResponse->requestedFormatData;

	auto clipboard = static_cast<sdlClip*>(
	    cliprdr_file_context_get_context(static_cast<CliprdrFileContext*>(context->custom)));
	WINPR_ASSERT(clipboard);

	ClipboardLockGuard systemlock(clipboard->_system);
	std::scoped_lock lock(clipboard->_lock);
	if (clipboard->_request_queue.empty())
	{
		WLog_Print(clipboard->_log, WLOG_ERROR, "no pending format request");
		return ERROR_INTERNAL_ERROR;
	}

	do
	{
		UINT32 srcFormatId = 0;
		auto& request = clipboard->_request_queue.front();
		bool success = (formatDataResponse->common.msgFlags & CB_RESPONSE_OK) &&
		               !(formatDataResponse->common.msgFlags & CB_RESPONSE_FAIL);
		request.setSuccess(success);

		if (!success)
		{
			WLog_Print(clipboard->_log, WLOG_WARN,
			           "clipboard data request for format %" PRIu32
			           " [%s], mime %s failed (msgFlags=0x%04" PRIx32 ", pending=%" PRIuz
			           ", generation=%" PRIu64 ")",
			           request.format(), request.formatstr().c_str(), request.mime().c_str(),
			           static_cast<uint32_t>(formatDataResponse->common.msgFlags),
			           clipboard->_request_queue.size(), clipboard->_serverFormatGeneration.load());
			break;
		}

		switch (request.format())
		{
			case CF_TEXT:
			case CF_OEMTEXT:
			case CF_UNICODETEXT:
				srcFormatId = request.format();
				break;

			case CF_DIB:
			case CF_DIBV5:
				srcFormatId = request.format();
				break;

			default:
			{
				auto name = clipboard->getServerFormat(request.format());
				if (!name.empty())
				{
					if (name == s_type_FileGroupDescriptorW)
					{
						srcFormatId =
						    ClipboardGetFormatId(clipboard->_system, s_type_FileGroupDescriptorW);

						if (cliprdr_file_context_has_local_support(clipboard->_file))
						{
							if (!cliprdr_file_context_update_server_data(
							        clipboard->_file, clipboard->_system, data, size))
							{
								WLog_Print(clipboard->_log, WLOG_WARN,
								           "File clipboard failed to update");
							}
						}
					}
					else
					{
						srcFormatId = ClipboardGetFormatId(clipboard->_system, name.c_str());
					}
				}
			}
			break;
		}

		if (!ClipboardSetData(clipboard->_system, srcFormatId, data, size))
		{
			WLog_Print(clipboard->_log, WLOG_ERROR, "error when setting clipboard data");
			return ERROR_INTERNAL_ERROR;
		}
		clipboard->_systemHoldsServerData = true;
		WLog_Print(clipboard->_log, WLOG_DEBUG, "updated clipboard data %s [0x%08" PRIx32 "]",
		           ClipboardGetFormatName(clipboard->_system, srcFormatId), srcFormatId);
	} while (false);

	if (!SetEvent(clipboard->_event))
	{
		WLog_Print(clipboard->_log, WLOG_ERROR, "error when setting clipboard event");
		return ERROR_INTERNAL_ERROR;
	}

	return CHANNEL_RC_OK;
}

const void* sdlClip::ClipDataCb(void* userdata, const char* mime_type, size_t* size)
{
	auto clip = static_cast<sdlClip*>(userdata);
	WINPR_ASSERT(clip);
	WINPR_ASSERT(size);
	WINPR_ASSERT(mime_type);

	*size = 0;
	uint32_t len = 0;

	if (mime_is_text(mime_type))
		mime_type = "text/plain";

	/* Declared first so it is destroyed (and logs its summary line) after
	 * every lock below has been released. */
	TransferTrace trace(clip->_log, "server->local", mime_type);

	uint32_t formatID = 0;
	{
		ClipboardLockGuard systemlock(clip->_system);
		std::scoped_lock lock(clip->_lock);
		trace.generation(clip->_serverFormatGeneration);

		/* check if we already used this mime type */
		auto cache = clip->_cache_data.find(mime_type);
		if (cache != clip->_cache_data.end())
		{
			*size = cache->second.size;
			trace.ok("cache-hit", *size);
			return cache->second.ptr.get();
		}

		formatID = clip->serverIdForMime(mime_type);
		trace.format(formatID, ClipboardGetFormatName(clip->_system, formatID));

		/* Can we convert the data from existing formats in the clipboard?
		 * Only worth asking when _system holds server data for the current
		 * format list. Otherwise it is either empty (WinPR logs an ERROR
		 * "No synthesizer for format CF_RAW" on every paste) or holds
		 * leftover local data from an earlier Linux->server copy, which
		 * would be returned here as if it were the server's clipboard. */
		if (clip->_systemHoldsServerData)
		{
			uint32_t fsize = 0;
			auto mimeFormatID = ClipboardRegisterFormat(clip->_system, mime_type);
			auto fptr = ClipboardGetData(clip->_system, mimeFormatID, &fsize);
			if (fptr)
			{
				auto ptr = std::shared_ptr<void>(fptr, free);
				clip->_cache_data.insert({ mime_type, { fsize, ptr } });

				auto fcache = clip->_cache_data.find(mime_type);
				if (fcache != clip->_cache_data.end())
				{
					*size = fcache->second.size;
					trace.ok("converted-from-held-data", *size);
					return fcache->second.ptr.get();
				}
			}
		}

		if (formatID == 0)
		{
			/* No server format backs this mime type (e.g. a text/plain
			 * request when the server's clipboard only has a file) --
			 * skip the request entirely rather than asking the server for
			 * a format it never advertised and always fails. */
			trace.fail("no-server-format");
			return nullptr;
		}

		/* The server just refused this exact format a moment ago (a
		 * consumer re-asking for sibling mimes after a failed paste): don't
		 * repeat the whole retry sequence. See coffee_clip_policy.hpp. */
		if (clip->_failureMemo.recentlyFailed(formatID, clip->_serverFormatGeneration,
		                                      coffee::clip::Clock::now()))
		{
			trace.fail("skipped-recently-refused");
			return nullptr;
		}
	}

	/* Windows Explorer can rebroadcast a file's ServerFormatList many times
	 * in quick succession right after a copy (confirmed live: 12 broadcasts
	 * in 24s for one file). A request whose round trip lands mid-churn
	 * routinely comes back CB_RESPONSE_FAIL even though nothing is actually
	 * wrong -- the very next attempt after the churn settles succeeds
	 * cleanly. Text and image requests fail the same way (CF_UNICODETEXT
	 * CB_RESPONSE_FAIL shows up in the journal), so every mime gets the same
	 * bounded retry, but only while no newer format list has superseded the
	 * one this request targets, so a genuinely stale request isn't retried
	 * pointlessly. */
	const int maxAttempts = coffee::clip::kMaxDataRequestAttempts;

	for (int attempt = 1; attempt <= maxAttempts; attempt++)
	{
		const uint64_t generation = clip->_serverFormatGeneration;
		trace.attempts(attempt);

		{
			ClipboardLockGuard systemlock(clip->_system);
			std::scoped_lock lock(clip->_lock);
			WLog_Print(clip->_log, WLOG_DEBUG, "requesting format %s [%s 0x%08" PRIx32 "]",
			           mime_type, ClipboardGetFormatName(clip->_system, formatID), formatID);
			if (clip->SendDataRequest(formatID, mime_type))
			{
				trace.fail("send-failed");
				return nullptr;
			}
		}

		HANDLE hdl[2] = { freerdp_abort_event(clip->_sdl->context()), clip->_event };
		DWORD status = WaitForMultipleObjects(ARRAYSIZE(hdl), hdl, FALSE, 10 * 1000);

		if (status != WAIT_OBJECT_0 + 1)
		{
			std::scoped_lock lock(clip->_lock);
			clip->_request_queue.pop();

			if (status == WAIT_TIMEOUT)
			{
				WLog_Print(clip->_log, WLOG_ERROR,
				           "no reply in 10 seconds, returning empty content");
				trace.fail("timeout-no-reply");
			}
			else if (status == WAIT_OBJECT_0)
				trace.fail("aborted-session-ending");
			else
				trace.fail("wait-failed");

			return nullptr;
		}

		ClipboardLockGuard systemlock(clip->_system);
		std::scoped_lock lock(clip->_lock);
		auto request = clip->_request_queue.front();
		clip->_request_queue.pop();

		if (clip->_request_queue.empty())
			std::ignore = ResetEvent(clip->_event);

		if (request.success())
		{
			auto rformatID = ClipboardRegisterFormat(clip->_system, mime_type);
			auto data = ClipboardGetData(clip->_system, rformatID, &len);
			if (!data)
			{
				WLog_Print(clip->_log, WLOG_ERROR, "error retrieving clipboard data");
				trace.fail("server-ok-but-conversion-failed");
				return nullptr;
			}

			auto ptr = std::shared_ptr<void>(data, free);
			clip->_cache_data.insert({ mime_type, { len, ptr } });
			*size = len;
			trace.ok("fetched", len);
			return ptr.get();
		}

		const bool superseded = (generation != clip->_serverFormatGeneration);
		if (!coffee::clip::shouldRetryDataRequest(attempt, maxAttempts, superseded))
		{
			if (superseded)
			{
				trace.fail("refused-and-superseded-by-new-format-list");
			}
			else
			{
				trace.fail("refused-by-server-gave-up");
				clip->_failureMemo.record(formatID, generation, coffee::clip::Clock::now());
			}
			return nullptr;
		}

		WLog_Print(clip->_log, WLOG_DEBUG,
		           "clipboard data request for %s failed, retrying (%d/%d)", mime_type,
		           attempt + 1, maxAttempts);

		/* Plain Sleep() here doesn't observe disconnect at all, letting the
		 * loop wake up and retry against a channel that's already torn
		 * down. Wait on the abort event instead so a disconnect mid-retry
		 * ends the loop immediately. */
		if (WaitForSingleObject(freerdp_abort_event(clip->_sdl->context()),
		                        static_cast<DWORD>(coffee::clip::kRetryDelay.count())) ==
		    WAIT_OBJECT_0)
		{
			trace.fail("aborted-session-ending");
			return nullptr;
		}
	}

	return nullptr;
}

void sdlClip::ClipCleanCb(void* userdata)
{
	auto clip = static_cast<sdlClip*>(userdata);
	WINPR_ASSERT(clip);
	ClipboardLockGuard give_me_a_name(clip->_system);
	std::scoped_lock lock(clip->_lock);
	ClipboardEmpty(clip->_system);
	clip->_systemHoldsServerData = false;
}

bool sdlClip::mime_is_file(const std::string& mime)
{
	if (strncmp(s_mime_uri_list, mime.c_str(), sizeof(s_mime_uri_list)) == 0)
		return true;
	if (strncmp(s_mime_gnome_copied_files, mime.c_str(), sizeof(s_mime_gnome_copied_files)) == 0)
		return true;
	if (strncmp(s_mime_mate_copied_files, mime.c_str(), sizeof(s_mime_mate_copied_files)) == 0)
		return true;
	return false;
}

bool sdlClip::mime_is_text(const std::string& mime)
{
	for (const auto& tmime : s_mime_text())
	{
		assert(tmime != nullptr);
		if (mime == tmime)
			return true;
	}

	return false;
}

bool sdlClip::mime_is_image(const std::string& mime)
{
	for (const auto& imime : s_mime_image())
	{
		assert(imime != nullptr);
		if (mime == imime)
			return true;
	}

	return false;
}

bool sdlClip::mime_is_bmp(const std::string& mime)
{
	for (const auto& imime : s_mime_bitmap())
	{
		assert(imime != nullptr);
		if (mime == imime)
			return true;
	}

	return false;
}

bool sdlClip::mime_is_html(const std::string& mime)
{
	return mime.compare(s_mime_html) == 0;
}

ClipRequest::ClipRequest(UINT32 format, const std::string& mime)
    : _format(format), _mime(mime), _success(false)
{
}

uint32_t ClipRequest::format() const
{
	return _format;
}

std::string ClipRequest::formatstr() const
{
	return ClipboardGetFormatIdString(_format);
}

std::string ClipRequest::mime() const
{
	return _mime;
}

bool ClipRequest::success() const
{
	return _success;
}

void ClipRequest::setSuccess(bool status)
{
	_success = status;
}

CliprdrFormat::CliprdrFormat(uint32_t formatID, const char* formatName) : _formatID(formatID)
{
	if (formatName)
		_formatName = formatName;
}

uint32_t CliprdrFormat::formatId() const
{
	return _formatID;
}

const char* CliprdrFormat::formatName() const
{
	if (_formatName.empty())
		return nullptr;
	return _formatName.c_str();
}
