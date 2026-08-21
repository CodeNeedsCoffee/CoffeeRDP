#include "coffee_auth_ipc.hpp"

#include <glib.h>

std::string authIpcSocketPath()
{
	const char* runtimeDir = g_get_user_runtime_dir();
	return std::string(runtimeDir) + "/coffeerdp/auth.sock";
}

bool ensureAuthIpcDir()
{
	const char* runtimeDir = g_get_user_runtime_dir();
	std::string dir = std::string(runtimeDir) + "/coffeerdp";
	return g_mkdir_with_parents(dir.c_str(), 0700) == 0;
}
