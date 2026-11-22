#include "plugin.hpp"
#include "core/BindingUi.hpp"
#include "core/Resolve.hpp"
#include "kit/OledDisplay.hpp"
#include "kit/PanelArt.hpp"
#include "groups/Layout.hpp"
#include "groups/View.hpp"

using namespace puppeteer;
using namespace tag;

struct Tag : Module, Marker {
	std::string name; // UI thread
	int accent = 0;

	Tag() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configSwitch(SIDE_PARAM, 0.f, 1.f, 0.f, "Names the module on its", {"Left", "Right"});
		configLight(SIDE_LIGHT, "Names the module on its right");
	}

	bool right() const {
		return params[SIDE_PARAM].value > 0.5f;
	}
	Role role() const override {
		return right() ? Role::TagRight : Role::TagLeft;
	}
	std::string label() const override {
		return name;
	}

	void process(const ProcessArgs& args) override {
		lights[SIDE_LIGHT].setBrightness(right());
	}

	json_t* dataToJson() override {
		return json_pack("{s:s, s:i}", "name", name.c_str(), "accent", accent);
	}

	void dataFromJson(json_t* rootJ) override {
		if (const char* n = json_string_value(json_object_get(rootJ, "name")))
			name = cleanName(n);
		if (json_t* j = json_object_get(rootJ, "accent"))
			accent = clamp((int) json_integer_value(j), 0, (int) std::size(kit::Theme::ACCENTS) - 1);
	}
};


struct TagScreen : kit::OledDisplay {
	Tag* module = nullptr;
	ViewState state;

	void step() override {
		theme = kit::Theme::accent(module ? module->accent : 0);
		state = ViewState();
		if (module) {
			state.name = module->name;
			state.right = module->right();
			state.found = (state.right ? module->rightExpander : module->leftExpander).module;
		}
		else
			state.name = "TAG";
		OledDisplay::step();
	}

	void paintScene(kit::Painter& painter) override {
		paint(painter, theme, state);
	}

	void onButton(const ButtonEvent& e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		appendNameEntry(createMenu(), &module->name, "lead");
		e.consume(this);
	}
};


struct TagWidget : ModuleWidget {
	TagWidget(Tag* module) {
		setModule(module);
		const kit::Panel panel = tag::panel();
		kit::addPanel(this, panel);

		const kit::Panel::Rect& glass = panel.screen;
		auto* screen = createWidget<TagScreen>(mm2px(Vec(glass.min.x, glass.min.y)));
		screen->box.size = mm2px(Vec(glass.max.x - glass.min.x, glass.max.y - glass.min.y));
		screen->module = module;
		addChild(screen);
	}

	void appendContextMenu(Menu* menu) override {
		Tag* m = getModule<Tag>();
		menu->addChild(new MenuSeparator);
		appendNameEntry(menu, &m->name, "lead");
		menu->addChild(createMenuLabel("Paths reach the module it names as $name:control"));
		menu->addChild(createIndexPtrSubmenuItem("Screen colour", std::vector<std::string>(std::begin(kit::Theme::ACCENTS), std::end(kit::Theme::ACCENTS)), &m->accent));
	}
};


Model* modelTag = createModel<Tag, TagWidget>("Tag");
