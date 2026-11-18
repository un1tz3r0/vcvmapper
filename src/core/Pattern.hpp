#pragma once
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

// Target patterns, independent of Rack. A pattern is comma-separated clauses of the form
//   [plugin/]model[@scope]:[light:]item   or   $tag:[light:]item
// where plugin and model are globs over slugs or names, scope says which instances may match (see Scope), and item
// is #N or #N-M (Rack's param or light ids) or a glob over the param or light name. Globs use * and ?, and ignore case.
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

/** Which instances a clause may match, relative to its anchor: the module that owns the binding. */
enum class Scope {
	All,    // anywhere in the rack
	Row,    // on the anchor's row
	Column, // overlapping the anchor's horizontal span, on any row
	Group,  // in the anchor's group
	Cable,  // at the far end of the owner's selecting jacks
	Named,  // in groups whose name matches `Clause::groups`
};
inline const char* const SCOPES[] = {"all", "row", "column", "group", "cable"};

struct Clause {
	std::string plugin = "*", model, name;
	std::string tag;    // set for a $tag clause, which names one module and has no plugin, model or scope
	Scope scope = Scope::All;
	std::string groups; // the glob for Scope::Named
	bool light = false;
	int first = -1, last = -1; // id range when the item is #N or #N-M, else -1

	bool matchesId(int id) const {
		return first >= 0 ? id >= first && id <= last : false;
	}
	bool matchesItem(int id, const std::string& itemName) const {
		return first >= 0 ? matchesId(id) : !itemName.empty() && glob(name, itemName);
	}

	std::string item() const {
		std::string s = light ? "light:" : "";
		if (first < 0)
			return s + name;
		s += "#" + std::to_string(first);
		return first == last ? s : s + "-" + std::to_string(last);
	}
	std::string scopeText() const {
		if (scope != Scope::Named)
			return SCOPES[int(scope)];
		for (const char* keyword : SCOPES)
			if (glob(keyword, groups))
				return "\"" + groups + "\"";
		return groups;
	}
	/** The clause as text that parses back to it. */
	std::string str() const {
		if (!tag.empty())
			return "$" + tag + ":" + item();
		std::string s = plugin == "*" ? model : plugin + "/" + model;
		if (scope != Scope::All)
			s += "@" + scopeText();
		return s + ":" + item();
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
		if (item.size() > 6 && glob("light:*", item)) {
			out.light = true;
			item = trim(item.substr(6));
		}
		if (!module.empty() && module[0] == '$') {
			out.tag = trim(module.substr(1));
			if (out.tag.empty()) {
				error = "'" + text + "' needs a tag name after $";
				return false;
			}
		}
		else {
			size_t at = module.find('@');
			if (at != std::string::npos) {
				if (!parseScope(trim(module.substr(at + 1)), out)) {
					error = "'" + module.substr(at) + "' isn't a scope or group name";
					return false;
				}
				module = trim(module.substr(0, at));
			}
			size_t slash = module.find('/');
			if (slash != std::string::npos) {
				out.plugin = trim(module.substr(0, slash));
				module = trim(module.substr(slash + 1));
			}
			out.model = module;
			if (out.plugin.empty() || out.model.empty()) {
				error = "'" + text + "' has an empty part";
				return false;
			}
		}
		if (item.empty()) {
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

	/** A keyword, or a group glob; a group named like a keyword is written in quotes. */
	static bool parseScope(const std::string& text, Clause& out) {
		if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
			out.scope = Scope::Named;
			out.groups = text.substr(1, text.size() - 2);
			return !out.groups.empty();
		}
		for (int i = 0; i < int(std::size(SCOPES)); i++)
			if (glob(SCOPES[i], text) && text.find_first_of("*?") == std::string::npos) {
				out.scope = Scope(i);
				return true;
			}
		out.scope = Scope::Named;
		out.groups = text;
		return !text.empty() && text.find('"') == std::string::npos;
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

	static std::string join(const std::vector<Clause>& clauses) {
		std::string s;
		for (const Clause& c : clauses)
			s += (s.empty() ? "" : ", ") + c.str();
		return s;
	}
};

} // namespace puppeteer
