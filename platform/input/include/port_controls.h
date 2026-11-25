#ifndef PLATFORM_PORT_CONTROLS_H
#define PLATFORM_PORT_CONTROLS_H

#ifdef __cplusplus
#include <controls.h>

namespace port_controls {

engine::controls::Device PortDevice(int port);

engine::controls::ButtonLabel ButtonLabel(int port, engine::controls::GcInput input);

engine::controls::Device LastUsedDevice(int port);
uint32_t DeviceChanges();

}

extern "C" {
#endif

void PortInput_UpdateControls(void);

union SDL_Event;
void PortInput_ControlsHandleEvent(const union SDL_Event* event);

void PortInput_GcAdapterHints(void);
void PortInput_GcAdapterLog(void);
void PortInput_GcAdapterTick(void);

#ifdef __cplusplus
}
#endif

#endif
