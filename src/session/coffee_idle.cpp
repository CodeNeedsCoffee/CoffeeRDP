#include "coffee_idle.hpp"
#include "coffee_rdp_file.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace
{
const std::unordered_map<std::string, std::string>& aliases()
{
	static const std::unordered_map<std::string, std::string> table = {
		{ "alt", "Left Alt" },        { "alt_l", "Left Alt" },      { "leftalt", "Left Alt" },
		{ "alt_r", "Right Alt" },     { "rightalt", "Right Alt" },
		{ "win", "Left GUI" },        { "win_l", "Left GUI" },      { "lwin", "Left GUI" },
		{ "super", "Left GUI" },      { "meta", "Left GUI" },       { "leftgui", "Left GUI" },
		{ "win_r", "Right GUI" },     { "rwin", "Right GUI" },      { "rightgui", "Right GUI" },
		{ "ctrl", "Left Ctrl" },      { "ctrl_l", "Left Ctrl" },    { "leftctrl", "Left Ctrl" },
		{ "ctrl_r", "Right Ctrl" },   { "rightctrl", "Right Ctrl" },
		{ "shift", "Left Shift" },    { "shift_l", "Left Shift" },  { "leftshift", "Left Shift" },
		{ "shift_r", "Right Shift" }, { "rightshift", "Right Shift" },
		{ "tab", "Tab" },
		{ "esc", "Escape" },          { "escape", "Escape" },
		{ "del", "Delete" },          { "delete", "Delete" },
	};
	return table;
}

std::string trim_lower(const std::string& s)
{
	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
		begin++;
	while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
		end--;

	std::string out = s.substr(begin, end - begin);
	std::transform(out.begin(), out.end(), out.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}
} // namespace

std::vector<std::string> coffee_idle_parse_combo(const std::string& combo)
{
	std::vector<std::string> result;

	size_t start = 0;
	while (start <= combo.size())
	{
		size_t plus = combo.find('+', start);
		std::string raw =
		    (plus == std::string::npos) ? combo.substr(start) : combo.substr(start, plus - start);

		std::string key = trim_lower(raw);
		if (!key.empty())
		{
			auto& table = aliases();
			auto it = table.find(key);
			result.push_back(it != table.end() ? it->second : raw);
		}

		if (plus == std::string::npos)
			break;
		start = plus + 1;
	}

	return result;
}

std::optional<std::string> coffee_idle_scan_rdp_file_time(const std::string& path)
{
	return coffee_rdp_file_scan_key(path, "idle keypress time");
}

std::optional<std::string> coffee_idle_scan_rdp_file_combo(const std::string& path)
{
	return coffee_rdp_file_scan_key(path, "idle keypress combo");
}

void CoffeeIdleTimer::configure(unsigned intervalSeconds)
{
	_intervalSeconds = intervalSeconds;
	_haveLast = false;
}

void CoffeeIdleTimer::noteInput(std::chrono::steady_clock::time_point now)
{
	_last = now;
	_haveLast = true;
}

bool CoffeeIdleTimer::due(std::chrono::steady_clock::time_point now)
{
	if (!enabled())
		return false;

	if (!_haveLast)
	{
		_last = now;
		_haveLast = true;
		return false;
	}

	if (now - _last < std::chrono::seconds(_intervalSeconds))
		return false;

	_last = now;
	return true;
}
