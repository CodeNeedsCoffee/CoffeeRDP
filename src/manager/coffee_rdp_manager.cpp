/**
 * coffeerdp: the CoffeeRDP connection manager (PLAN.md Phase 7, step 7.1).
 *
 * GTK4 + libadwaita, its own process. Per §4's architecture, this cannot
 * share a process with `coffee-rdp-session` (SDL3, and GTK3/GTK4 can't
 * coexist with it) -- connecting spawns the session client as a detached
 * subprocess, exactly the way it's already run by hand today.
 *
 * The profile model and its on-disk store live in coffee_profiles.hpp,
 * deliberately GTK-free so they can be unit tested without a display
 * server; this file is only the UI on top of them.
 */

#include <adwaita.h>
#include <gtk/gtk.h>

#include <memory>
#include <string>
#include <vector>

#include "coffee_profiles.hpp"
#include "coffee_rdp_document.hpp"

namespace
{

/* Path the session binary is looked up at. Installed builds get it from
 * PATH; a development build usually hasn't been installed, so fall back to
 * the sibling binary in the same build tree as this one (see
 * sessionBinaryPath()) rather than failing with "command not found". */
constexpr const char* kSessionBinaryName = "coffee-rdp-session";

/* Also the hicolor icon theme name the logo installs under (see
 * src/manager/CMakeLists.txt and
 * data/com.codeneedscoffee.coffeerdp.manager.desktop's Icon= line) --
 * GNOME/Wayland convention is that these match, so a single constant keeps
 * them from drifting apart. */
constexpr const char* kApplicationId = "com.codeneedscoffee.coffeerdp.manager";

struct AppState
{
	CoffeeProfileStore store;
	std::string storePath;

	AdwApplicationWindow* window = nullptr;
	GtkListBox* list = nullptr;
	AdwViewStack* stack = nullptr;
	AdwToastOverlay* toasts = nullptr;
};

/* ---------------------------------------------------------------- helpers */

void showToast(AppState* st, const std::string& text)
{
	if (!st->toasts)
		return;
	adw_toast_overlay_add_toast(st->toasts, adw_toast_new(text.c_str()));
}

void showError(AppState* st, const std::string& text)
{
	auto* dialog = adw_alert_dialog_new("Error", text.c_str());
	adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", "_OK");
	adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(st->window));
}

void persist(AppState* st)
{
	if (!st->store.save(st->storePath))
		showError(st, "Could not save profiles to:\n" + st->storePath);
}

/** Resolves the session binary: PATH first (installed case), then the
 *  sibling path inside this build tree (development case). Returns an
 *  empty string if neither exists, so the caller can report it rather than
 *  silently spawning nothing. */
std::string sessionBinaryPath()
{
	if (char* found = g_find_program_in_path(kSessionBinaryName))
	{
		std::string path = found;
		g_free(found);
		return path;
	}

	/* Development fallback: .../build/src/manager/coffeerdp ->
	 * .../build/src/session/coffee-rdp-session */
	char* self = g_file_read_link("/proc/self/exe", nullptr);
	if (self)
	{
		gchar* dir = g_path_get_dirname(self);
		gchar* sibling =
		    g_build_filename(dir, "..", "session", kSessionBinaryName, nullptr);
		std::string candidate = sibling;
		g_free(sibling);
		g_free(dir);
		g_free(self);

		if (g_file_test(candidate.c_str(), G_FILE_TEST_IS_EXECUTABLE))
			return candidate;
	}

	return "";
}

/** Registers the source tree's resources/icon/ (which mirrors a real
 *  hicolor icon theme's layout -- hicolor/<size>x<size>/apps/) as an extra
 *  icon-theme search path, so a development build run straight out of
 *  build/ can still show the app's own logo without needing `ninja
 *  install` first. Installed builds don't need this at all: `ninja install`
 *  (see src/manager/CMakeLists.txt) puts the same PNGs under
 *  $prefix/share/icons/hicolor/, which GtkIconTheme already searches by
 *  default -- this is purely a development convenience, same reasoning as
 *  sessionBinaryPath()'s build-tree fallback above. */
void registerDevIconThemePath(GtkWidget* forWidget)
{
	char* self = g_file_read_link("/proc/self/exe", nullptr);
	if (!self)
		return;

	/* .../build/src/manager/coffeerdp -> .../resources/icon */
	gchar* dir = g_path_get_dirname(self);
	gchar* candidate = g_build_filename(dir, "..", "..", "..", "resources", "icon", nullptr);
	g_free(dir);
	g_free(self);

	if (g_file_test(candidate, G_FILE_TEST_IS_DIR))
	{
		auto* theme = gtk_icon_theme_get_for_display(gtk_widget_get_display(forWidget));
		gtk_icon_theme_add_search_path(theme, candidate);
	}
	g_free(candidate);
}

void connectToProfile(AppState* st, const CoffeeProfile& profile)
{
	const auto binary = sessionBinaryPath();
	if (binary.empty())
	{
		showError(st, std::string("Could not find ") + kSessionBinaryName +
		                  " on PATH or in this build tree.");
		return;
	}

	const auto args = CoffeeProfileStore::sessionArgs(profile);

	std::vector<char*> argv;
	argv.push_back(const_cast<char*>(binary.c_str()));
	for (const auto& a : args)
		argv.push_back(const_cast<char*>(a.c_str()));
	argv.push_back(nullptr);

	/* Hand the session a startup/activation token so the compositor lets its
	 * window take focus from us. Without one, the session window is just
	 * "some background process mapped a window": Wayland refuses the
	 * activation outright, and GNOME turns the refusal into a persistent
	 * `"FreeRDP: <host>" is ready` notification instead of focusing it. The
	 * token is the whole difference between "launched by the user" and
	 * "popped up on its own" as far as Mutter is concerned.
	 *
	 * GAppInfo is what the token gets attributed to; GIO reads its name out
	 * of there for the startup sequence, and warns if it's NULL. */
	GdkAppLaunchContext* launchCtx = nullptr;
	std::string token;
	if (GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(st->window)))
	{
		launchCtx = gdk_display_get_app_launch_context(display);
		GAppInfo* info = g_app_info_create_from_commandline(
		    binary.c_str(), "CoffeeRDP session", G_APP_INFO_CREATE_NONE, nullptr);
		if (gchar* id = g_app_launch_context_get_startup_notify_id(G_APP_LAUNCH_CONTEXT(launchCtx),
		                                                           info, nullptr))
		{
			token = id;
			g_free(id);
		}
		if (info)
			g_object_unref(info);
	}

	/* Both names, because which one is read depends on the backend the
	 * session ends up on: XDG_ACTIVATION_TOKEN is what SDL's Wayland driver
	 * looks for, DESKTOP_STARTUP_ID is the X11 equivalent (--display-backend:x11
	 * is still a supported escape hatch, see sdl_freerdp.cpp). */
	gchar** envp = g_get_environ();
	if (!token.empty())
	{
		envp = g_environ_setenv(envp, "XDG_ACTIVATION_TOKEN", token.c_str(), TRUE);
		envp = g_environ_setenv(envp, "DESKTOP_STARTUP_ID", token.c_str(), TRUE);
	}

	GError* error = nullptr;
	/* DO_NOT_REAP_CHILD is deliberately absent: we never wait on the
	 * session, so letting GLib reap it avoids leaving zombies behind for
	 * as long as the manager stays open. */
	const gboolean ok = g_spawn_async(nullptr, argv.data(), envp,
	                                  static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH), nullptr,
	                                  nullptr, nullptr, &error);
	g_strfreev(envp);

	if (!ok)
	{
		/* Tell the compositor the sequence it was told to expect is never
		 * arriving -- otherwise X11 keeps showing a launch spinner until the
		 * startup sequence times out on its own. */
		if (launchCtx && !token.empty())
			g_app_launch_context_launch_failed(G_APP_LAUNCH_CONTEXT(launchCtx), token.c_str());
		if (launchCtx)
			g_object_unref(launchCtx);

		const std::string msg = error && error->message ? error->message : "unknown error";
		showError(st, "Could not launch the session:\n" + msg);
		if (error)
			g_error_free(error);
		return;
	}

	if (launchCtx)
		g_object_unref(launchCtx);

	showToast(st, "Connecting to " + profile.name + "…");
}

/* ------------------------------------------------------------ edit dialog */

struct EditDialog
{
	AppState* st = nullptr;
	/* Empty for "add new"; the existing profile's name for "edit". */
	std::string originalName;

	AdwDialog* dialog = nullptr;
	GtkWidget* name = nullptr;
	GtkWidget* host = nullptr;
	GtkWidget* port = nullptr;
	GtkWidget* username = nullptr;
	GtkWidget* domain = nullptr;
	GtkWidget* aadAuth = nullptr;
	GtkWidget* disableShortcuts = nullptr;
	GtkWidget* ignoreCertificateErrors = nullptr;
	GtkWidget* quality = nullptr;
	GtkWidget* multimon = nullptr;
	GtkWidget* fullscreen = nullptr;
	GtkWidget* idleTime = nullptr;
	GtkWidget* idleCombo = nullptr;
	GtkWidget* rdpFile = nullptr;
	GtkWidget* rdpFileStatus = nullptr;

	/* Last .rdp path whose contents were pulled into the form. Guards the
	 * "changed" handler on the path entry so an existing file is loaded
	 * exactly once per path, instead of re-clobbering fields the user is
	 * mid-way through editing on every keystroke. */
	std::string loadedRdpPath;
	/* Set while a load is repopulating widgets, so the widgets' own
	 * "changed" signals don't recurse back into the loader. */
	bool loading = false;
};

void refreshList(AppState* st);
void onExportClicked(GtkButton*, gpointer data);

const char* kQualityOptions[] = { "", "speed", "balanced", "quality", "best", "auto", nullptr };

std::string entryText(GtkWidget* row)
{
	const char* t = gtk_editable_get_text(GTK_EDITABLE(row));
	return t ? t : "";
}

int qualityIndex(const std::string& quality)
{
	for (int i = 0; kQualityOptions[i]; i++)
	{
		if (quality == kQualityOptions[i])
			return i;
	}
	return 0;
}

/** Pushes a profile's values into the dialog's widgets. Used both when the
 *  dialog opens and when a linked .rdp file is loaded. */
void populateFields(EditDialog* ed, const CoffeeProfile& p)
{
	ed->loading = true;
	gtk_editable_set_text(GTK_EDITABLE(ed->host), p.host.c_str());
	adw_spin_row_set_value(ADW_SPIN_ROW(ed->port), p.port);
	gtk_editable_set_text(GTK_EDITABLE(ed->username), p.username.c_str());
	gtk_editable_set_text(GTK_EDITABLE(ed->domain), p.domain.c_str());
	adw_switch_row_set_active(ADW_SWITCH_ROW(ed->aadAuth), p.aadAuth);
	adw_switch_row_set_active(ADW_SWITCH_ROW(ed->disableShortcuts), p.disableShortcuts);
	adw_switch_row_set_active(ADW_SWITCH_ROW(ed->ignoreCertificateErrors), p.ignoreCertificateErrors);
	adw_combo_row_set_selected(ADW_COMBO_ROW(ed->quality),
	                           static_cast<guint>(qualityIndex(p.quality)));
	adw_switch_row_set_active(ADW_SWITCH_ROW(ed->multimon), p.multimon);
	adw_switch_row_set_active(ADW_SWITCH_ROW(ed->fullscreen), p.fullscreen);
	adw_spin_row_set_value(ADW_SPIN_ROW(ed->idleTime), p.idleKeepAliveSeconds);
	gtk_editable_set_text(GTK_EDITABLE(ed->idleCombo), p.idleKeepAliveCombo.c_str());
	ed->loading = false;
}

void setRdpStatus(EditDialog* ed, const std::string& text)
{
	if (ed->rdpFileStatus)
		gtk_label_set_text(GTK_LABEL(ed->rdpFileStatus), text.c_str());
}

/** When the .rdp path points at a readable file, pull its settings into the
 *  form so the fields show what the file actually contains -- and so that
 *  saving writes back the file's own values plus the user's edits, rather
 *  than overwriting the file with whatever stale defaults the form held. */
void onRdpPathChanged(GtkEditable*, gpointer data)
{
	auto* ed = static_cast<EditDialog*>(data);
	if (ed->loading)
		return;

	const auto path = entryText(ed->rdpFile);
	if (path == ed->loadedRdpPath)
		return;

	if (path.empty())
	{
		ed->loadedRdpPath.clear();
		setRdpStatus(ed, "Settings above are used directly.");
		return;
	}

	if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
	{
		/* Almost certainly a half-typed path; don't touch the fields, and
		 * don't treat it as an error yet -- saving will create the file. */
		ed->loadedRdpPath.clear();
		setRdpStatus(ed, "File does not exist yet — it will be created on save.");
		return;
	}

	CoffeeRdpDocument doc;
	if (!doc.load(path))
	{
		ed->loadedRdpPath.clear();
		setRdpStatus(ed, "Could not read this file.");
		return;
	}

	CoffeeProfile loaded;
	coffee_rdp_document_to_profile(doc, loaded);
	populateFields(ed, loaded);
	ed->loadedRdpPath = path;
	setRdpStatus(ed, "Loaded from file. Saving writes changes back to it.");
}

void onEditSave(GtkButton*, gpointer data)
{
	auto* ed = static_cast<EditDialog*>(data);
	auto* st = ed->st;

	CoffeeProfile p;
	/* Preserve any keys a newer build wrote that this one doesn't model --
	 * editing a profile shouldn't quietly drop them (see
	 * coffee_profiles.hpp). */
	if (!ed->originalName.empty())
	{
		if (const auto* existing = st->store.find(ed->originalName))
			p.unknownKeys = existing->unknownKeys;
	}

	p.name = entryText(ed->name);
	p.host = entryText(ed->host);
	p.port = static_cast<unsigned>(adw_spin_row_get_value(ADW_SPIN_ROW(ed->port)));
	p.username = entryText(ed->username);
	p.domain = entryText(ed->domain);
	p.aadAuth = adw_switch_row_get_active(ADW_SWITCH_ROW(ed->aadAuth));
	p.disableShortcuts = adw_switch_row_get_active(ADW_SWITCH_ROW(ed->disableShortcuts));
	p.ignoreCertificateErrors =
	    adw_switch_row_get_active(ADW_SWITCH_ROW(ed->ignoreCertificateErrors));

	const guint qsel = adw_combo_row_get_selected(ADW_COMBO_ROW(ed->quality));
	p.quality = kQualityOptions[qsel] ? kQualityOptions[qsel] : "";

	p.multimon = adw_switch_row_get_active(ADW_SWITCH_ROW(ed->multimon));
	p.fullscreen = adw_switch_row_get_active(ADW_SWITCH_ROW(ed->fullscreen));
	p.idleKeepAliveSeconds = static_cast<unsigned>(adw_spin_row_get_value(ADW_SPIN_ROW(ed->idleTime)));
	p.idleKeepAliveCombo = entryText(ed->idleCombo);
	p.rdpFile = entryText(ed->rdpFile);

	std::string error;
	if (!CoffeeProfileStore::validate(p, error))
	{
		showError(st, error);
		return;
	}

	/* Linked to a .rdp file: write the connection settings back into it,
	 * preserving every key CoffeeRDP doesn't model (see
	 * coffee_rdp_document.hpp -- enablerdsaadauth in particular). Done
	 * before touching the profile store so a failed write doesn't leave the
	 * store claiming settings the file never received. */
	if (!p.rdpFile.empty())
	{
		CoffeeRdpDocument doc;
		if (!doc.load(p.rdpFile))
		{
			showError(st, "Could not read the .rdp file:\n" + p.rdpFile);
			return;
		}
		coffee_rdp_document_from_profile(p, doc);
		if (!doc.save(p.rdpFile))
		{
			showError(st, "Could not write the .rdp file:\n" + p.rdpFile);
			return;
		}
	}

	const bool ok = ed->originalName.empty() ? st->store.add(p, error)
	                                         : st->store.update(ed->originalName, p, error);
	if (!ok)
	{
		showError(st, error);
		return;
	}

	persist(st);
	refreshList(st);
	if (!p.rdpFile.empty())
		showToast(st, "Saved, and updated " + p.rdpFile);
	adw_dialog_close(ed->dialog);
}

GtkWidget* addEntryRow(GtkWidget* group, const char* title, const std::string& value)
{
	GtkWidget* row = adw_entry_row_new();
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
	gtk_editable_set_text(GTK_EDITABLE(row), value.c_str());
	adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
	return row;
}

GtkWidget* addSpinRow(GtkWidget* group, const char* title, const char* subtitle, double value,
                      double min, double max)
{
	GtkWidget* row = adw_spin_row_new_with_range(min, max, 1);
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
	if (subtitle)
		adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
	adw_spin_row_set_value(ADW_SPIN_ROW(row), value);
	adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
	return row;
}

GtkWidget* addSwitchRow(GtkWidget* group, const char* title, const char* subtitle, bool active)
{
	GtkWidget* row = adw_switch_row_new();
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
	if (subtitle)
		adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
	adw_switch_row_set_active(ADW_SWITCH_ROW(row), active);
	adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
	return row;
}

/** `existing`: editing a profile already in the store (originalName is set,
 *  Save calls update()). `importSeed`: prefills the form from a just-loaded
 *  .rdp file's contents (e.g. name from its filename) without linking to it
 *  or committing anything -- Save still calls add(), same as a blank "New
 *  Profile". Passing both is not meaningful; callers pass at most one. */
void presentEditDialog(AppState* st, const CoffeeProfile* existing,
                       const CoffeeProfile* importSeed = nullptr)
{
	auto* ed = g_new0(EditDialog, 1);
	new (ed) EditDialog();
	ed->st = st;
	ed->originalName = existing ? existing->name : "";

	CoffeeProfile seed;
	if (existing)
		seed = *existing;
	else if (importSeed)
		seed = *importSeed;

	auto* dialog = adw_dialog_new();
	ed->dialog = dialog;
	adw_dialog_set_title(dialog, existing ? "Edit Profile" : (importSeed ? "Import Profile" : "New Profile"));
	adw_dialog_set_content_width(dialog, 480);
	adw_dialog_set_content_height(dialog, 640);

	auto* toolbar = adw_toolbar_view_new();
	auto* header = adw_header_bar_new();
	adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(header), FALSE);

	auto* cancel = gtk_button_new_with_label("Cancel");
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(adw_dialog_close), dialog);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), cancel);

	auto* save = gtk_button_new_with_label("Save");
	gtk_widget_add_css_class(save, "suggested-action");
	g_signal_connect(save, "clicked", G_CALLBACK(onEditSave), ed);
	adw_header_bar_pack_end(ADW_HEADER_BAR(header), save);

	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

	auto* page = adw_preferences_page_new();

	auto* connGroup = adw_preferences_group_new();
	adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(connGroup), "Connection");
	ed->name = addEntryRow(connGroup, "Name", seed.name);
	ed->host = addEntryRow(connGroup, "Host", seed.host);
	ed->port = addSpinRow(connGroup, "Port", nullptr, seed.port, 1, 65535);
	ed->username = addEntryRow(connGroup, "Username", seed.username);
	ed->domain = addEntryRow(connGroup, "Domain", seed.domain);
	ed->aadAuth = addSwitchRow(connGroup, "Entra ID sign-in",
	                           "Azure AD authentication (enablerdsaadauth)", seed.aadAuth);
	ed->disableShortcuts = addSwitchRow(
	    connGroup, "Disable local shortcuts",
	    "Right Shift + key combos (minimize, fullscreen, etc.) inside the session. Can still "
	    "be re-enabled mid-session from the floatbar's dropdown.",
	    seed.disableShortcuts);
	ed->ignoreCertificateErrors = addSwitchRow(
	    connGroup, "Always trust this server's certificate",
	    "Skips TLS certificate checks entirely -- useful for AVD/Entra-joined hosts, whose "
	    "certificate rotates roughly daily and otherwise triggers a \"certificate has "
	    "changed\" prompt on nearly every connection. Also disables detection of a real "
	    "man-in-the-middle attack, so only enable this for hosts you trust.",
	    seed.ignoreCertificateErrors);
	adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(connGroup));

	auto* displayGroup = adw_preferences_group_new();
	adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(displayGroup), "Display");

	auto* qualityRow = adw_combo_row_new();
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(qualityRow), "Quality");
	adw_action_row_set_subtitle(ADW_ACTION_ROW(qualityRow), "Connection quality preset");
	const char* qualityLabels[] = { "Default", "Speed",  "Balanced",
		                            "Quality", "Best",   "Auto", nullptr };
	auto* qualityModel = gtk_string_list_new(qualityLabels);
	adw_combo_row_set_model(ADW_COMBO_ROW(qualityRow), G_LIST_MODEL(qualityModel));
	guint qsel = 0;
	for (guint i = 0; kQualityOptions[i]; i++)
	{
		if (seed.quality == kQualityOptions[i])
		{
			qsel = i;
			break;
		}
	}
	adw_combo_row_set_selected(ADW_COMBO_ROW(qualityRow), qsel);
	adw_preferences_group_add(ADW_PREFERENCES_GROUP(displayGroup), qualityRow);
	ed->quality = qualityRow;

	ed->multimon = addSwitchRow(displayGroup, "Use all monitors", nullptr, seed.multimon);
	ed->fullscreen =
	    addSwitchRow(displayGroup, "Fullscreen", "Ignored when using all monitors", seed.fullscreen);
	adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(displayGroup));

	auto* idleGroup = adw_preferences_group_new();
	adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(idleGroup), "Idle keep-alive");
	ed->idleTime = addSpinRow(idleGroup, "Idle seconds", "0 disables the keep-alive",
	                          seed.idleKeepAliveSeconds, 0, 3600);
	ed->idleCombo = addEntryRow(idleGroup, "Key combo", seed.idleKeepAliveCombo);
	adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(idleGroup));

	auto* advGroup = adw_preferences_group_new();
	adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(advGroup), "Linked .rdp file");
	adw_preferences_group_set_description(
	    ADW_PREFERENCES_GROUP(advGroup),
	    "When set, the settings above are read from this file, and saving writes them "
	    "back to it. Keys CoffeeRDP doesn't manage are left untouched.");
	ed->rdpFile = addEntryRow(advGroup, "File path", seed.rdpFile);

	ed->rdpFileStatus = gtk_label_new("");
	gtk_label_set_wrap(GTK_LABEL(ed->rdpFileStatus), TRUE);
	gtk_label_set_xalign(GTK_LABEL(ed->rdpFileStatus), 0.0f);
	gtk_widget_add_css_class(ed->rdpFileStatus, "dim-label");
	gtk_widget_add_css_class(ed->rdpFileStatus, "caption");
	gtk_widget_set_margin_top(ed->rdpFileStatus, 6);
	adw_preferences_group_add(ADW_PREFERENCES_GROUP(advGroup), ed->rdpFileStatus);

	adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(advGroup));

	/* Connected after every widget exists, so the handler can safely
	 * repopulate the whole form. */
	g_signal_connect(ed->rdpFile, "changed", G_CALLBACK(onRdpPathChanged), ed);

	/* Opening an already-linked profile: show what the file currently
	 * contains rather than the possibly-stale copy in profiles.ini, so the
	 * file stays the source of truth for a linked profile. */
	if (!seed.rdpFile.empty())
		onRdpPathChanged(GTK_EDITABLE(ed->rdpFile), ed);
	else
		setRdpStatus(ed, "Settings above are used directly.");

	adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), page);
	adw_dialog_set_child(dialog, toolbar);

	/* Tie the heap-allocated EditDialog's lifetime to the widget's. */
	g_object_set_data_full(G_OBJECT(dialog), "coffee-edit-dialog", ed,
	                       [](gpointer p)
	                       {
		                       auto* e = static_cast<EditDialog*>(p);
		                       e->~EditDialog();
		                       g_free(e);
	                       });

	adw_dialog_present(dialog, GTK_WIDGET(st->window));
}

/* -------------------------------------------------------------- main list */

struct RowContext
{
	AppState* st = nullptr;
	std::string name;
};

void freeRowContext(gpointer data, GClosure*)
{
	auto* ctx = static_cast<RowContext*>(data);
	ctx->~RowContext();
	g_free(ctx);
}

RowContext* newRowContext(AppState* st, const std::string& name)
{
	auto* ctx = g_new0(RowContext, 1);
	new (ctx) RowContext();
	ctx->st = st;
	ctx->name = name;
	return ctx;
}

void onConnectClicked(GtkButton*, gpointer data)
{
	auto* ctx = static_cast<RowContext*>(data);
	if (const auto* p = ctx->st->store.find(ctx->name))
		connectToProfile(ctx->st, *p);
}

void onEditClicked(GtkButton*, gpointer data)
{
	auto* ctx = static_cast<RowContext*>(data);
	if (const auto* p = ctx->st->store.find(ctx->name))
		presentEditDialog(ctx->st, p);
}

void onDeleteConfirmed(AdwAlertDialog*, const char* response, gpointer data)
{
	auto* ctx = static_cast<RowContext*>(data);
	if (g_strcmp0(response, "delete") != 0)
		return;

	if (ctx->st->store.remove(ctx->name))
	{
		persist(ctx->st);
		refreshList(ctx->st);
		showToast(ctx->st, "Deleted " + ctx->name);
	}
}

void onDeleteClicked(GtkButton*, gpointer data)
{
	auto* ctx = static_cast<RowContext*>(data);

	auto* dialog = adw_alert_dialog_new("Delete profile?",
	                                    ("\"" + ctx->name + "\" will be removed.").c_str());
	adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "delete",
	                               "_Delete", nullptr);
	adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "delete",
	                                         ADW_RESPONSE_DESTRUCTIVE);
	adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
	g_signal_connect(dialog, "response", G_CALLBACK(onDeleteConfirmed), ctx);
	adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(ctx->st->window));
}

void onRowActivated(GtkListBox*, GtkListBoxRow* row, gpointer data)
{
	auto* st = static_cast<AppState*>(data);
	const char* name =
	    static_cast<const char*>(g_object_get_data(G_OBJECT(row), "coffee-profile-name"));
	if (!name)
		return;
	if (const auto* p = st->store.find(name))
		connectToProfile(st, *p);
}

std::string profileSubtitle(const CoffeeProfile& p)
{
	if (!p.rdpFile.empty())
		return p.rdpFile;

	std::string s = p.host;
	if (p.port != 3389)
		s += ":" + std::to_string(p.port);
	if (!p.username.empty())
		s = p.username + "@" + s;
	return s;
}

void refreshList(AppState* st)
{
	/* Rebuild wholesale: the list is a handful of rows and only changes on
	 * an explicit add/edit/delete, so incremental updates would be more
	 * bookkeeping than they're worth here. */
	GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(st->list));
	while (child)
	{
		GtkWidget* next = gtk_widget_get_next_sibling(child);
		gtk_list_box_remove(st->list, child);
		child = next;
	}

	const auto& profiles = st->store.profiles();
	adw_view_stack_set_visible_child_name(st->stack, profiles.empty() ? "empty" : "list");

	for (const auto& p : profiles)
	{
		GtkWidget* row = adw_action_row_new();
		adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), p.name.c_str());
		adw_action_row_set_subtitle(ADW_ACTION_ROW(row), profileSubtitle(p).c_str());
		adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), nullptr);
		g_object_set_data_full(G_OBJECT(row), "coffee-profile-name", g_strdup(p.name.c_str()),
		                       g_free);

		GtkWidget* connect = gtk_button_new_with_label("Connect");
		gtk_widget_set_valign(connect, GTK_ALIGN_CENTER);
		gtk_widget_add_css_class(connect, "suggested-action");
		g_signal_connect_data(connect, "clicked", G_CALLBACK(onConnectClicked),
		                      newRowContext(st, p.name), freeRowContext, G_CONNECT_DEFAULT);
		adw_action_row_add_suffix(ADW_ACTION_ROW(row), connect);

		GtkWidget* exportBtn = gtk_button_new_from_icon_name("document-save-as-symbolic");
		gtk_widget_set_valign(exportBtn, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text(exportBtn, "Export to .rdp…");
		gtk_widget_add_css_class(exportBtn, "flat");
		g_signal_connect_data(exportBtn, "clicked", G_CALLBACK(onExportClicked),
		                      newRowContext(st, p.name), freeRowContext, G_CONNECT_DEFAULT);
		adw_action_row_add_suffix(ADW_ACTION_ROW(row), exportBtn);

		GtkWidget* edit = gtk_button_new_from_icon_name("document-edit-symbolic");
		gtk_widget_set_valign(edit, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text(edit, "Edit");
		gtk_widget_add_css_class(edit, "flat");
		g_signal_connect_data(edit, "clicked", G_CALLBACK(onEditClicked),
		                      newRowContext(st, p.name), freeRowContext, G_CONNECT_DEFAULT);
		adw_action_row_add_suffix(ADW_ACTION_ROW(row), edit);

		GtkWidget* del = gtk_button_new_from_icon_name("user-trash-symbolic");
		gtk_widget_set_valign(del, GTK_ALIGN_CENTER);
		gtk_widget_set_tooltip_text(del, "Delete");
		gtk_widget_add_css_class(del, "flat");
		g_signal_connect_data(del, "clicked", G_CALLBACK(onDeleteClicked),
		                      newRowContext(st, p.name), freeRowContext, G_CONNECT_DEFAULT);
		adw_action_row_add_suffix(ADW_ACTION_ROW(row), del);

		gtk_list_box_append(st->list, row);
	}
}

void onAddClicked(GtkButton*, gpointer data)
{
	presentEditDialog(static_cast<AppState*>(data), nullptr);
}

/* ------------------------------------------------------- import / export */

std::string baseNameWithoutExt(const std::string& path)
{
	gchar* base = g_path_get_basename(path.c_str());
	std::string name = base ? base : path;
	g_free(base);

	const auto dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		name = name.substr(0, dot);
	return name;
}

/** Appends " (2)", " (3)", ... until the name is free, so importing two
 *  files that both happen to be named e.g. "work.rdp" doesn't fail with a
 *  duplicate-name error the user never asked to resolve. */
std::string suggestProfileName(AppState* st, const std::string& base)
{
	if (!st->store.find(base))
		return base;
	for (int i = 2; i < 1000; i++)
	{
		auto candidate = base + " (" + std::to_string(i) + ")";
		if (!st->store.find(candidate))
			return candidate;
	}
	return base; // pathological; validate()/add() will still catch the collision
}

GtkFileDialog* newRdpFileDialog(const char* title)
{
	auto* dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, title);

	auto* filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "RDP Files");
	gtk_file_filter_add_suffix(filter, "rdp");
	auto* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	g_list_store_append(filters, filter);
	gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
	g_object_unref(filters);
	g_object_unref(filter);

	return dialog;
}

bool userDismissedDialog(GError* error)
{
	return error && g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED);
}

/** Import, whether from the toolbar button or a drop: loads a .rdp and
 *  returns a profile seeded from it and linked back to the file it came
 *  from, with a name derived from the filename and de-duplicated against the
 *  store. Doesn't touch the store itself; callers decide whether to present
 *  it for review or commit it directly.
 *
 *  The link is the point of an import: the file stays the source of truth, so
 *  editing the profile writes changes back to it, and keys CoffeeRDP doesn't
 *  model (gateway settings, redirection flags) keep applying instead of being
 *  silently left behind at the moment of import. */
bool loadRdpAsProfile(AppState* st, const std::string& path, CoffeeProfile& out,
                      std::string& error)
{
	CoffeeRdpDocument doc;
	if (!doc.load(path))
	{
		error = "Could not read:\n" + path;
		return false;
	}
	coffee_rdp_document_to_profile(doc, out);
	out.name = suggestProfileName(st, baseNameWithoutExt(path));
	out.rdpFile = path;
	return true;
}

void onImportFileChosen(GObject* source, GAsyncResult* res, gpointer data)
{
	auto* st = static_cast<AppState*>(data);
	GError* error = nullptr;
	GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
	if (!file)
	{
		if (!userDismissedDialog(error))
			showError(st, std::string("Could not open file:\n") +
			                  (error && error->message ? error->message : "unknown error"));
		if (error)
			g_error_free(error);
		return;
	}

	gchar* pathC = g_file_get_path(file);
	const std::string path = pathC ? pathC : "";
	g_free(pathC);
	g_object_unref(file);

	CoffeeProfile seed;
	std::string loadError;
	if (!loadRdpAsProfile(st, path, seed, loadError))
	{
		showError(st, loadError);
		return;
	}

	/* Review, not a direct add: an explicit "Import…" click is a deliberate
	 * single-file action, so it gets the same review-before-commit treatment
	 * as "New Profile" -- the user can rename it, tweak a field, or clear the
	 * link to the file before anything is written. Contrast with the
	 * drag-and-drop path below, which is meant for quick bulk import and
	 * commits directly. */
	presentEditDialog(st, nullptr, &seed);
}

void onImportClicked(GtkButton*, gpointer data)
{
	auto* st = static_cast<AppState*>(data);
	auto* dialog = newRdpFileDialog("Import .rdp File");
	gtk_file_dialog_open(dialog, GTK_WINDOW(st->window), nullptr, onImportFileChosen, st);
	g_object_unref(dialog);
}

struct ExportContext
{
	AppState* st = nullptr;
	std::string profileName;
};

void onExportFileChosen(GObject* source, GAsyncResult* res, gpointer data)
{
	std::unique_ptr<ExportContext> ctx(static_cast<ExportContext*>(data));
	GError* error = nullptr;
	GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, &error);
	if (!file)
	{
		if (!userDismissedDialog(error))
			showError(ctx->st, std::string("Could not export:\n") +
			                        (error && error->message ? error->message : "unknown error"));
		if (error)
			g_error_free(error);
		return;
	}

	gchar* pathC = g_file_get_path(file);
	const std::string path = pathC ? pathC : "";
	g_free(pathC);
	g_object_unref(file);

	const auto* p = ctx->st->store.find(ctx->profileName);
	if (!p)
	{
		showError(ctx->st, "\"" + ctx->profileName + "\" no longer exists.");
		return;
	}

	/* Load first (a no-op empty document if the path doesn't exist yet, see
	 * coffee_rdp_document.hpp) so exporting onto an existing file merges
	 * into it -- preserving whatever that file already has, exactly like
	 * editing a linked profile does -- rather than overwriting it outright. */
	CoffeeRdpDocument doc;
	if (!doc.load(path))
	{
		showError(ctx->st, "Could not read:\n" + path);
		return;
	}
	coffee_rdp_document_from_profile(*p, doc);
	if (!doc.save(path))
	{
		showError(ctx->st, "Could not write:\n" + path);
		return;
	}

	showToast(ctx->st, "Exported \"" + ctx->profileName + "\" to " + path);
}

void onExportClicked(GtkButton*, gpointer data)
{
	auto* rowCtx = static_cast<RowContext*>(data);
	auto* exportCtx = new ExportContext{ rowCtx->st, rowCtx->name };

	auto* dialog = newRdpFileDialog("Export Profile");
	gtk_file_dialog_set_initial_name(dialog, (rowCtx->name + ".rdp").c_str());
	gtk_file_dialog_save(dialog, GTK_WINDOW(rowCtx->st->window), nullptr, onExportFileChosen,
	                     exportCtx);
	g_object_unref(dialog);
}

/** Drag-and-drop onto the main window: each dropped .rdp is imported
 *  directly and saved, no review dialog -- deliberately different from the
 *  toolbar Import button. A drop is inherently a multi-file gesture (a
 *  file manager selection), and prompting once per file would be tedious;
 *  a mis-imported profile is one click to fix via Edit or Delete.
 *
 *  Each profile stays linked to the file it was dropped from (see
 *  loadRdpAsProfile) -- the "Linked .rdp file" path in Edit is filled in,
 *  and the file keeps driving the connection. Unlinking is a matter of
 *  clearing that field. */
gboolean onWindowDrop(GtkDropTarget*, const GValue* value, double, double, gpointer data)
{
	auto* st = static_cast<AppState*>(data);
	if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST))
		return FALSE;

	auto* files = static_cast<GSList*>(g_value_get_boxed(value));
	int imported = 0;
	int skipped = 0;

	for (GSList* it = files; it != nullptr; it = it->next)
	{
		auto* file = G_FILE(it->data);
		gchar* pathC = g_file_get_path(file);
		if (!pathC)
		{
			skipped++;
			continue;
		}
		const std::string path = pathC;
		g_free(pathC);

		if (!g_str_has_suffix(path.c_str(), ".rdp"))
		{
			skipped++;
			continue;
		}

		CoffeeProfile p;
		std::string error;
		if (!loadRdpAsProfile(st, path, p, error) || !st->store.add(p, error))
		{
			skipped++;
			continue;
		}
		imported++;
	}

	if (imported > 0)
	{
		persist(st);
		refreshList(st);
	}

	std::string msg;
	if (imported > 0)
		msg = "Imported " + std::to_string(imported) + " profile" + (imported == 1 ? "" : "s");
	if (skipped > 0)
		msg += (msg.empty() ? "" : ", ") + std::string("skipped ") + std::to_string(skipped) +
		       " file" + (skipped == 1 ? "" : "s");
	if (!msg.empty())
		showToast(st, msg);

	return imported > 0;
}

void onActivate(GtkApplication* app, gpointer data)
{
	auto* st = static_cast<AppState*>(data);

	auto* window = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
	st->window = window;
	gtk_window_set_title(GTK_WINDOW(window), "CoffeeRDP");
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 520);

	registerDevIconThemePath(GTK_WIDGET(window));
	/* Icon *lookup* on Wayland is themed by name, resolved against
	 * kApplicationId regardless of whether this is an installed build
	 * (found under $prefix/share/icons/hicolor) or a dev build (found via
	 * registerDevIconThemePath() above) -- unlike GTK3/X11, GTK4 has no API
	 * to hand a window an arbitrary raw image as its taskbar/switcher icon
	 * directly, that's controlled by the compositor via the app's
	 * application-id + a matching .desktop Icon= key instead. This call
	 * still matters even so: it's what makes in-process lookups (the empty
	 * state below) resolve by the same name. */
	gtk_window_set_icon_name(GTK_WINDOW(window), kApplicationId);

	/* Drag a .rdp file (or several) in from a file manager to import it --
	 * see onWindowDrop() for why this commits directly rather than opening
	 * the review dialog the toolbar Import button uses. Attached to the
	 * whole window so it works over the empty state too, not just the list. */
	auto* dropTarget = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
	g_signal_connect(dropTarget, "drop", G_CALLBACK(onWindowDrop), st);
	gtk_widget_add_controller(GTK_WIDGET(window), GTK_EVENT_CONTROLLER(dropTarget));

	auto* toolbar = adw_toolbar_view_new();
	auto* header = adw_header_bar_new();

	auto* add = gtk_button_new_from_icon_name("list-add-symbolic");
	gtk_widget_set_tooltip_text(add, "New profile");
	g_signal_connect(add, "clicked", G_CALLBACK(onAddClicked), st);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), add);

	auto* import = gtk_button_new_from_icon_name("document-open-symbolic");
	gtk_widget_set_tooltip_text(import, "Import .rdp file…");
	g_signal_connect(import, "clicked", G_CALLBACK(onImportClicked), st);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), import);

	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

	auto* stack = adw_view_stack_new();
	st->stack = ADW_VIEW_STACK(stack);

	/* Empty state. Uses the app's own full-color logo via a real icon-theme
	 * lookup (kApplicationId, same name gtk_window_set_icon_name() used
	 * above) rather than adw_status_page_set_icon_name(): that call expects
	 * a *symbolic* (single-color, theme-recolored) icon per libadwaita's
	 * design guidelines, and our logo is deliberately full-color -- forcing
	 * it through the symbolic slot would either fail to render or get
	 * flattened to a silhouette. set_paintable() is libadwaita's own
	 * supported way to show a full-color hero graphic here instead. Falls
	 * back to a stock icon if the lookup ever fails (e.g. a packaging bug
	 * left the icon out) rather than showing nothing. */
	auto* status = adw_status_page_new();
	auto* iconTheme = gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(window)));
	if (gtk_icon_theme_has_icon(iconTheme, kApplicationId))
	{
		auto* iconPaintable =
		    gtk_icon_theme_lookup_icon(iconTheme, kApplicationId, nullptr, 128,
		                              gtk_widget_get_scale_factor(GTK_WIDGET(window)),
		                              gtk_widget_get_direction(GTK_WIDGET(window)),
		                              GTK_ICON_LOOKUP_FORCE_REGULAR);
		adw_status_page_set_paintable(ADW_STATUS_PAGE(status), GDK_PAINTABLE(iconPaintable));
		g_object_unref(iconPaintable);
	}
	else
		adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), "network-server-symbolic");
	adw_status_page_set_title(ADW_STATUS_PAGE(status), "No Connections");
	adw_status_page_set_description(ADW_STATUS_PAGE(status),
	                                "Add a connection profile to get started.");
	auto* emptyAdd = gtk_button_new_with_label("New Profile");
	gtk_widget_set_halign(emptyAdd, GTK_ALIGN_CENTER);
	gtk_widget_add_css_class(emptyAdd, "suggested-action");
	gtk_widget_add_css_class(emptyAdd, "pill");
	g_signal_connect(emptyAdd, "clicked", G_CALLBACK(onAddClicked), st);
	adw_status_page_set_child(ADW_STATUS_PAGE(status), emptyAdd);
	adw_view_stack_add_named(ADW_VIEW_STACK(stack), status, "empty");

	/* Profile list */
	auto* scroller = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroller, TRUE);
	auto* clamp = adw_clamp_new();
	gtk_widget_set_margin_top(clamp, 12);
	gtk_widget_set_margin_bottom(clamp, 12);
	gtk_widget_set_margin_start(clamp, 12);
	gtk_widget_set_margin_end(clamp, 12);

	auto* list = gtk_list_box_new();
	st->list = GTK_LIST_BOX(list);
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
	gtk_widget_add_css_class(list, "boxed-list");
	g_signal_connect(list, "row-activated", G_CALLBACK(onRowActivated), st);

	adw_clamp_set_child(ADW_CLAMP(clamp), list);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), clamp);
	adw_view_stack_add_named(ADW_VIEW_STACK(stack), scroller, "list");

	auto* toasts = adw_toast_overlay_new();
	st->toasts = ADW_TOAST_OVERLAY(toasts);
	adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toasts), stack);

	adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), toasts);
	adw_application_window_set_content(window, toolbar);

	refreshList(st);
	gtk_window_present(GTK_WINDOW(window));
}

} // namespace

int main(int argc, char** argv)
{
	AppState st;
	st.storePath = CoffeeProfileStore::defaultPath();
	if (st.storePath.empty())
	{
		g_printerr("Could not determine a config path (is HOME set?)\n");
		return 1;
	}
	if (!st.store.load(st.storePath))
		g_printerr("Warning: could not read %s -- starting with an empty profile list\n",
		           st.storePath.c_str());

	auto* app = adw_application_new(kApplicationId, G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(onActivate), &st);

	const int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
