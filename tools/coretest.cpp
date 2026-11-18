// Checks the pure parts of the core: patterns, and groups and twins. Usage: make -C tools coretest && tools/coretest
#include <cstdio>
#include "core/Groups.hpp"

using namespace puppeteer;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Clause parse(const std::string& text) {
	Clause c;
	std::string error;
	CHECK(Clause::parse(text, c, error), "'%s' should parse: %s", text.c_str(), error.c_str());
	return c;
}

static void patterns() {
	Clause c = parse("Fundamental/VCF@row:Cutoff");
	CHECK(c.plugin == "Fundamental" && c.model == "VCF" && c.scope == Scope::Row && c.name == "Cutoff", "row clause");
	c = parse("VCA@Voice*:light:#2-3");
	CHECK(c.scope == Scope::Named && c.groups == "Voice*" && c.light && c.first == 2 && c.last == 3, "named group, lights");
	c = parse("VCO@\"row\":Freq");
	CHECK(c.scope == Scope::Named && c.groups == "row", "a group called row");
	c = parse("$lead:Freq*");
	CHECK(c.tag == "lead" && c.name == "Freq*", "tag clause");
	CHECK(parse("Fundamental/VCO:Freq").scope == Scope::All, "no scope is all");

	for (const char* text : {"Fundamental/VCF@row:Cutoff", "VCA@Voice*:light:#2-3", "VCO@\"row\":Freq", "$lead:#4", "VCO:Freq", "VCO@column:#0"})
		CHECK(parse(text).str() == text, "'%s' round trips as '%s'", text, parse(text).str().c_str());

	Clause bad;
	std::string error;
	for (const char* text : {"VCO", "VCO@:Freq", "$:Freq", "/VCO:Freq", "VCO:#x", "VCO@\"\":Freq"})
		CHECK(!Clause::parse(text, bad, error), "'%s' should not parse", text);
	CHECK(Clause::parseAll("VCO:Freq, VCF@row:Cut*", error).size() == 2, "two clauses");
}

static void groups() {
	// Row 0: voice A = VCO VCA VCA, a gap, voice B = VCO VCA VCF VCA. Row 1: VCO under voice A's VCO, VCA under B's.
	Layout l = Layout::of({
		{1, 0, 0, 10, "VCO"}, {2, 0, 10, 13, "VCA"}, {3, 0, 13, 16, "VCA"},
		{4, 0, 20, 30, "VCO"}, {5, 0, 30, 33, "VCA"}, {6, 0, 33, 40, "VCF"}, {7, 0, 40, 43, "VCA"},
		{8, 1, 2, 12, "VCO"}, {9, 1, 30, 33, "VCA"},
	});
	CHECK(l.groups.size() == 4, "touching runs are groups, got %d", (int) l.groups.size());
	CHECK(l.ordinal[2] == 1 && l.ordinal[6] == 1 && l.ordinal[5] == 0, "ordinals count per model within a group");

	auto ids = [&](std::vector<int> is) {
		std::string s;
		for (int i : is)
			s += std::to_string(l.boxes[i].id);
		return s;
	};
	CHECK(ids(l.twins(2, Scope::Row)) == "37", "second VCA pairs with second VCA, past the VCF: %s", ids(l.twins(2, Scope::Row)).c_str());
	CHECK(ids(l.twins(0, Scope::Row)) == "14", "VCO twins on the row: %s", ids(l.twins(0, Scope::Row)).c_str());
	CHECK(ids(l.twins(0, Scope::All)) == "148", "VCO twins in the rack: %s", ids(l.twins(0, Scope::All)).c_str());
	CHECK(ids(l.twins(0, Scope::Column)) == "18", "VCO twins in the column: %s", ids(l.twins(0, Scope::Column)).c_str());
	CHECK(ids(l.twins(4, Scope::Column)) == "59", "column twins need only overlap: %s", ids(l.twins(4, Scope::Column)).c_str());
	CHECK(l.admits(Scope::Group, "", 3, 6) && !l.admits(Scope::Group, "", 3, 2), "group scope");
	CHECK(!l.admits(Scope::Row, "", -1, 0) && l.admits(Scope::All, "", -1, 0), "no anchor admits only all");
	CHECK(!l.admits(Scope::Named, "*", 0, 1), "implicit groups have no name");
}

int main() {
	patterns();
	groups();
	std::printf(failures ? "%d failed\n" : "all passed\n", failures);
	return failures != 0;
}
