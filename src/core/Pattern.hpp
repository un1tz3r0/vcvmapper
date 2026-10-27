#pragma once
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

// Target patterns, independent of Rack. A pattern is comma-separated clauses of the form
//   [plugin/]model:[light:]item
// where plugin and model are globs over slugs or names, and item is #N or #N-M (Rack's param or light ids)
// or a glob over the param or light name. Globs use * and ?, and ignore case.
namespace puppeteer {

inline bool glob(const std::string& pattern, const std::string& text) {
	auto same = [](char a, char b) { return std::tolower((unsigned char) a) == std::tolower((unsigned char) b); };
	size_t p = 0, t = 0, star = std::string::npos, resume = 0;
	while (t < text.size()) {
		if (p < pattern.size() && (pattern[p] == '?' || (pattern[p] != '*' && same(pattern[p], text[t])))) {
			p++, t++;
		}
		else if (p < pattern.size() && pattern[p] == '*') {
			star = p++;
			resume = t;
		}
		else if (star != std::string::npos) {
			p = star + 1;
			t = ++resume;
		}
		else
			return false;
	}
	while (p < pattern.size() && pattern[p] == '*')
		p++;
	return p == pattern.size();
}

inline std::string trim(const std::string& s) {
	size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

struct Clause {
	std::string plugin = "*", model, name;
	bool light = false;
	int first = -1, last = -1; // id range when the item is #N or #N-M, else -1

	bool matchesId(int id) const {
		return first >= 0 ? id >= first && id <= last : false;
	}

	/** False with `error` set when the text isn't a clause. */
	static bool parse(std::string text, Clause& out, std::string& error) {
		out = Clause();
		text = trim(text);
		size_t colon = text.find(':');
		if (colon == std::string::npos) {
			error = "'" + text + "' needs model:param";
			return false;
		}
		std::string module = trim(text.substr(0, colon)), item = trim(text.substr(colon + 1));
		size_t slash = module.find('/');
		if (slash != std::string::npos) {
			out.plugin = trim(module.substr(0, slash));
			module = trim(module.substr(slash + 1));
		}
		out.model = module;
		if (item.size() > 6 && glob("light:*", item)) {
			out.light = true;
			item = trim(item.substr(6));
		}
		if (out.plugin.empty() || out.model.empty() || item.empty()) {
			error = "'" + text + "' has an empty part";
			return false;
		}
		if (item[0] != '#') {
			out.name = item;
			return true;
		}
		char* end = nullptr;
		out.first = out.last = (int) std::strtol(item.c_str() + 1, &end, 10);
		if (*end == '-')
			out.last = (int) std::strtol(end + 1, &end, 10);
		if (end == item.c_str() + 1 || *end || out.first < 0 || out.last < out.first) {
			error = "'" + item + "' should be #N or #N-M";
			return false;
		}
		return true;
	}

	/** Every clause in a comma-separated pattern, stopping at the first bad one. */
	static std::vector<Clause> parseAll(const std::string& pattern, std::string& error) {
		std::vector<Clause> clauses;
		error.clear();
		size_t start = 0;
		while (start <= pattern.size()) {
			size_t comma = pattern.find(',', start);
			std::string part = trim(pattern.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
			if (!part.empty()) {
				Clause c;
				if (!parse(part, c, error))
					return {};
				clauses.push_back(c);
			}
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
		return clauses;
	}
};

} // namespace puppeteer
