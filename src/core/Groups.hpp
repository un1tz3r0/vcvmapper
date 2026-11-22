#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "Pattern.hpp"

// Where modules sit relative to each other, independent of Rack: groups, ordinals and twins, which scopes are made of.
namespace puppeteer {

/** What a module does to the groups around it. */
enum class Role {
	Plain,
	Header,   // opens a named group that runs to an End, a gap or the next Header
	End,      // closes the group it is in
	TagLeft,  // names the module touching its left side
	TagRight, // names the module touching its right side
};

/** One module's place in the rack, in grid units. */
struct Box {
	int64_t id;
	int row, x0, x1;   // x1 is one past the last HP
	std::string model; // plugin and model slugs, as one key
	Role role = Role::Plain;
	std::string name;  // a Header's group name or a tag's name
};

/** A snapshot of the rack as groups. Boxes keep the caller's order, which must be by row, then left to right. */
struct Layout {
	struct Group {
		int row, x0, x1;
		std::string name; // empty for an implicit group
	};

	std::vector<Box> boxes;
	std::vector<int> group;   // per box
	std::vector<int> ordinal; // per box: how many of its model come before it in its group
	std::vector<Group> groups;

	/** Explicit groups run from a Header to an End, a gap or the next Header; any other run of touching modules is an
	implicit group. */
	static Layout of(std::vector<Box> boxes) {
		Layout l;
		l.boxes = std::move(boxes);
		for (size_t i = 0; i < l.boxes.size(); i++) {
			const Box& b = l.boxes[i];
			const Box* prev = i ? &l.boxes[i - 1] : nullptr;
			bool touching = prev && b.row == prev->row && b.x0 == prev->x1;
			if (!touching || b.role == Role::Header || prev->role == Role::End)
				l.groups.push_back({b.row, b.x0, b.x1, b.role == Role::Header ? b.name : ""});
			l.groups.back().x1 = b.x1;
			l.group.push_back(int(l.groups.size()) - 1);
			int n = 0;
			for (size_t j = i; j-- > 0 && l.group[j] == l.group[i];)
				n += l.boxes[j].model == b.model;
			l.ordinal.push_back(n);
		}
		return l;
	}

	int find(int64_t id) const {
		for (size_t i = 0; i < boxes.size(); i++)
			if (boxes[i].id == id)
				return int(i);
		return -1;
	}

	/** The modules named by tags matching `name`: each tag's touching neighbour on the side it points to. */
	std::vector<int> tagged(const std::string& name) const {
		std::vector<int> out;
		for (int i = 0; i < int(boxes.size()); i++) {
			const Box& b = boxes[i];
			if ((b.role != Role::TagLeft && b.role != Role::TagRight) || b.name.empty() || !glob(name, b.name))
				continue;
			int j = b.role == Role::TagLeft ? i - 1 : i + 1;
			if (j >= 0 && j < int(boxes.size()) && boxes[j].row == b.row
				&& (j < i ? boxes[j].x1 == b.x0 : boxes[j].x0 == b.x1))
				out.push_back(j);
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
		return out;
	}

	/** Whether box `i` is in `scope` around box `anchor`. Only All admits anything without an anchor. */
	bool admits(Scope scope, const std::string& names, int anchor, int i) const {
		if (i < 0)
			return false;
		if (scope == Scope::All)
			return true;
		if (scope == Scope::Named)
			return !groups[group[i]].name.empty() && glob(names, groups[group[i]].name);
		if (anchor < 0)
			return false;
		const Box &a = boxes[anchor], &b = boxes[i];
		switch (scope) {
			case Scope::Row: return a.row == b.row;
			case Scope::Column: return b.x0 < a.x1 && a.x0 < b.x1;
			case Scope::Group: return group[anchor] == group[i];
			default: return false;
		}
	}

	/** The anchor and its twins in `scope`, in rack order. A twin has the anchor's model and, except across a column,
	its ordinal within its group, so the second VCA of one voice pairs with the second VCA of every other. */
	std::vector<int> twins(int anchor, Scope scope, const std::string& names = "") const {
		std::vector<int> out;
		for (int i = 0; i < int(boxes.size()); i++) {
			if (i == anchor || (boxes[i].model == boxes[anchor].model && admits(scope, names, anchor, i)
				&& (scope == Scope::Column || ordinal[i] == ordinal[anchor])))
				out.push_back(i);
		}
		return out;
	}
};

} // namespace puppeteer
