#pragma once
#include <rack.hpp>
#include <functional>

// Context menu widgets for settings that live outside params.
namespace kit {
using namespace rack;

/** A menu slider over any value, in its own units. The slider's travel maps to min..max through `power`, so
values near min get more room when power > 1. */
struct FnQuantity : Quantity {
	std::string label, unit;
	float min = 0.f, max = 1.f, def = 0.f, power = 1.f;
	int precision = 2;
	std::function<float()> get;
	std::function<void(float)> set;

	float position(float x) {
		return std::pow(math::clamp((x - min) / (max - min), 0.f, 1.f), 1.f / power);
	}
	float getValue() override {
		return position(get());
	}
	void setValue(float p) override {
		set(min + (max - min) * std::pow(math::clamp(p, 0.f, 1.f), power));
	}
	float getDefaultValue() override {
		return position(def);
	}
	std::string getLabel() override {
		return label;
	}
	std::string getUnit() override {
		return unit;
	}
	std::string getDisplayValueString() override {
		return string::f("%.*f", precision, get());
	}
};

struct OwnedSlider : ui::Slider {
	~OwnedSlider() {
		delete quantity;
	}
};

inline ui::Slider* createSlider(std::string label, std::string unit, float min, float max, float def, int precision,
	std::function<float()> get, std::function<void(float)> set, float power = 1.f) {
	auto* q = new FnQuantity;
	q->label = std::move(label);
	q->unit = std::move(unit);
	q->min = min, q->max = max, q->def = def, q->power = power, q->precision = precision;
	q->get = std::move(get);
	q->set = std::move(set);
	auto* slider = new OwnedSlider;
	slider->quantity = q;
	slider->box.size.x = 220.f;
	return slider;
}

/** A text field that hands its text over on Enter and closes the menu. */
struct TextEntry : ui::TextField {
	std::function<void(const std::string&)> commit;

	TextEntry() {
		box.size.x = 220.f;
	}
	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			commit(text);
			if (auto* overlay = getAncestorOfType<ui::MenuOverlay>())
				overlay->requestDelete();
			e.consume(this);
			return;
		}
		TextField::onSelectKey(e);
	}
};

} // namespace kit
