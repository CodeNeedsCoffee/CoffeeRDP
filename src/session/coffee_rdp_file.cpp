#include "coffee_rdp_file.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace
{
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}
} // namespace

std::optional<std::string> coffee_rdp_file_scan_key(const std::string& path,
                                                     const std::string& keyName)
{
	std::ifstream in(path);
	if (!in.is_open())
		return std::nullopt;

	const std::string wantName = lower(keyName);

	std::string line;
	while (std::getline(in, line))
	{
		// Matches FreeRDP's own .rdp line format (client/common/file.c
		// parse_line()): "name:type:value", type is a single character.
		auto d1 = line.find(':');
		if (d1 == std::string::npos)
			continue;
		auto d2 = line.find(':', d1 + 1);
		if (d2 == std::string::npos || d2 - d1 != 2)
			continue;

		std::string name = lower(line.substr(0, d1));
		char type = line[d1 + 1];
		std::string value = line.substr(d2 + 1);
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
			value.pop_back();

		if (type == 's' && name == wantName)
			return value;
	}
	return std::nullopt;
}
