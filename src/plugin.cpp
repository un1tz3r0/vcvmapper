#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelPin);
	p->addModel(modelMarionette);
	p->addModel(modelTroupe);
	p->addModel(modelWing);
	p->addModel(modelTag);
	p->addModel(modelTether);
	p->addModel(modelStage);
}
