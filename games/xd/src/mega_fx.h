#ifndef XD_MEGA_FX_H
#define XD_MEGA_FX_H

namespace xd::megafx {

bool Enabled();
void Begin(void* fop);
void Charge(void* fop, int primal);
void Burst(void* fop);
void Reset();

}

#endif
