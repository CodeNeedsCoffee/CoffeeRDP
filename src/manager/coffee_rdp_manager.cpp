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

#include <string>
#include <vector>

#include "coffee_profiles.hpp"

namespace
{

/* Path the session binary is looked up at. Installed builds get it from
 * PATH; a development build usually hasn't been installed, so fall back to
 * the sibling binary in the same build tree as this one (see
 * sessionBinaryPath()) rather than failing with "command not found". */
constexpr const char* kSessionBinaryName = "coffee-rdp-session";

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

	GError* error = nullptr;
	/* DO_NOT_REAP_CHILD is deliberately absent: we never wait on the
	 * session, so letting GLib reap it avoids leaving zombies behind for
	 * as long as the manager stays open. */
	const gboolean ok = g_spawn_async(nullptr, argv.data(), nullptr,
	                                  static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH), nullptr,
	                                  nullptr, nullptr, &error);
	if (!ok)
	{
		const std::string msg = error && error->message ? error->message : "unknown error";
		showError(st, "Could not launch the session:\n" + msg);
		if (error)
			g_error_free(error);
		return;
	}

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
	GtkWidget* quality = nullptr;
	GtkWidget* multimon = nullptr;
	GtkWidget* fullscreen = nullptr;
	GtkWidget* idleTime = nullptr;
	GtkWidget* idleCombo = nullptr;
	GtkWidget* rdpFile = nullptr;
};

void refreshList(AppState* st);

const char* kQualityOptions[] = { "", "speed", "balanced", "quality", "best", "auto", nullptr };

std::string entryText(GtkWidget* row)
{
	const char* t = gtk_editable_get_text(GTK_EDITABLE(row));
	return t ? t : "";
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
	p.port = static_cast<unsigned>(
	    gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ed->port)));
	p.username = entryText(ed->username);
	p.domain = entryText(ed->domain);

	const guint qsel = adw_combo_row_get_selected(ADW_COMBO_ROW(ed->quality));
	p.quality = kQualityOptions[qsel] ? kQualityOptions[qsel] : "";

	p.multimon = adw_switch_row_get_active(ADW_SWITCH_ROW(ed->multimon));
	p.fullscreen = adw_switch_row_get_active(ADW_SWITCH_ROW(ed->fullscreen));
	p.idleKeepAliveSeconds = static_cast<unsigned>(
	    gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ed->idleTime)));
	p.idleKeepAliveCombo = entryText(ed->idleCombo);
	p.rdpFile = entryText(ed->rdpFile);

	std::string error;
	const bool ok = ed->originalName.empty() ? st->store.add(p, error)
	                                         : st->store.update(ed->originalName, p, error);
	if (!ok)
	{
		showError(st, error);
		return;
	}

	persist(st);
	refreshList(st);
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

void presentEditDialog(AppState* st, const CoffeeProfile* existing)
{
	auto* ed = g_new0(EditDialog, 1);
	new (ed) EditDialog();
	ed->st = st;
	ed->originalName = existing ? existing->name : "";

	CoffeeProfile seed;
	if (existing)
		seed = *existing;

	auto* dialog = adw_dialog_new();
	ed->dialog = dialog;
	adw_dialog_set_title(dialog, existing ? "Edit Profile" : "New Profile");
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
	adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(advGroup), "Advanced");
	adw_preferences_group_set_description(
	    ADW_PREFERENCES_GROUP(advGroup),
	    "When set, the .rdp file supplies the connection settings above.");
	ed->rdpFile = addEntryRow(advGroup, ".rdp file", seed.rdpFile);
	adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(advGroup));

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

void onActivate(GtkApplication* app, gpointer data)
{
	auto* st = static_cast<AppState*>(data);

	auto* window = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
	st->window = window;
	gtk_window_set_title(GTK_WINDOW(window), "CoffeeRDP");
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 520);

	auto* toolbar = adw_toolbar_view_new();
	auto* header = adw_header_bar_new();

	auto* add = gtk_button_new_from_icon_name("list-add-symbolic");
	gtk_widget_set_tooltip_text(add, "New profile");
	g_signal_connect(add, "clicked", G_CALLBACK(onAddClicked), st);
	adw_header_bar_pack_start(ADW_HEADER_BAR(header), add);

	adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

	auto* stack = adw_view_stack_new();
	st->stack = ADW_VIEW_STACK(stack);

	/* Empty state */
	auto* status = adw_status_page_new();
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

	auto* app = adw_application_new("com.coffeerdp.manager", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(onActivate), &st);

	const int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
