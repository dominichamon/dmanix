#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

namespace timer {

void Initialize(uint32_t frequency);
void Shutdown();

typedef void (*Callback)(uint32_t tick);
void RegisterCallback(Callback callback);

}

#endif  // TIMER_H
