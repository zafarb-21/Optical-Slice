#ifndef OPTICAL_SLICE_APP_H
#define OPTICAL_SLICE_APP_H

#include "optical_slice_types.h"

void OpticalSlice_Init(void);
void OpticalSlice_Run(void);
const optical_slice_frame_t *OpticalSlice_GetLatestFrame(void);

#endif /* OPTICAL_SLICE_APP_H */
