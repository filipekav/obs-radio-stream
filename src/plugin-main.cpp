#include <obs-module.h>
#include <obs-frontend-api.h>
#include "radio-output.hpp"
#include "radio-ui.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-radio-stream", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
    return obs_module_text("PluginDescription");
}

bool obs_module_load(void) {
    obs_register_output(&radio_output_info);

    RadioDock* dock = new RadioDock(nullptr);
    obs_frontend_add_dock_by_id("obs_radio_stream_dock", obs_module_text("DockTitle"), dock);

    return true;
}

void obs_module_unload(void) {
}
