#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include "radio-output.hpp"
#include "radio-ui.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-radio-stream", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
    return obs_module_text("PluginDescription");
}

static void on_frontend_event(enum obs_frontend_event event, void *private_data) {
    (void)private_data;
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        QMainWindow* mainWindow = (QMainWindow*)obs_frontend_get_main_window();
        if (mainWindow) {
            RadioDock* dock = new RadioDock(mainWindow);
            obs_frontend_add_dock_by_id("obs_radio_stream_dock", obs_module_text("DockTitle"), dock);
        }
    }
}

bool obs_module_load(void) {
    obs_register_output(&radio_output_info);
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    return true;
}

void obs_module_unload(void) {
    obs_frontend_remove_event_callback(on_frontend_event, nullptr);
}
