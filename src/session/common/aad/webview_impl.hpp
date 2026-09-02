/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Popup browser for AAD authentication
 *
 * Copyright 2023 Isaac Klein <fifthdegree@protonmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>

/** `password`: resolved plaintext from a profile's 1Password reference, or
 *  empty. Threaded through to coffee-rdp-auth so it can best-effort
 *  autofill Microsoft's own password field -- empty means no reference is
 *  configured (or `op read` failed), in which case this is a no-op and the
 *  login proceeds exactly as it always has. See
 *  coffee_rdp_auth.cpp's tryAutofill() for the fill/fallback logic itself.
 *
 *  `opItemRef`: the profile's raw op://vault/item/field reference,
 *  unresolved, or empty. Not secret -- passed through so coffee-rdp-auth
 *  can resolve the MFA/verification-code step's own OTP itself, right when
 *  that field actually appears rather than up front (a TOTP code expires
 *  in ~30s, so resolving it as early as `password` would risk submitting a
 *  stale one). See coffee_rdp_auth.cpp's resolveOtpCode(). */
[[nodiscard]] bool webview_impl_run(const std::string& title, const std::string& url,
                                    const std::string& password, const std::string& opItemRef,
                                    std::string& code);
