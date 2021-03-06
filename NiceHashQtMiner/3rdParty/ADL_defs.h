#ifndef ADL_DEFS_H
#define ADL_DEFS_H


namespace ATI {
	namespace ADL {

typedef int (*ADL_MAIN_CONTROL_CREATE) (ADL_MAIN_MALLOC_CALLBACK, int);
typedef int (*ADL2_MAIN_CONTROL_CREATE) (ADL_MAIN_MALLOC_CALLBACK, int, ADL_CONTEXT_HANDLE);
typedef int (*ADL_MAIN_CONTROL_DESTROY) ();
typedef int (*ADL2_MAIN_CONTROL_DESTROY) (ADL_CONTEXT_HANDLE);
typedef int (*ADL_ADAPTER_NUMBEROFADAPTERS_GET) (int*);
typedef int (*ADL_ADAPTER_ADAPTERINFO_GET) (LPAdapterInfo, int);
typedef int (*ADL2_ADAPTER_ADAPTERINFO_GET) (ADL_CONTEXT_HANDLE, LPAdapterInfo, int);
typedef int (*ADL_ADAPTER_ACTIVE_GET) (int, int*);


typedef int (*ADL_OVERDRIVE5_CURRENTACTIVITY_GET) (int, ADLPMActivity*);
typedef int (*ADL_OVERDRIVE5_TEMPERATURE_GET) (int, int, ADLTemperature*);
typedef int (*ADL_OVERDRIVE5_FANSPEED_GET) (int, int, ADLFanSpeedValue*);

typedef int (*ADL2_OVERDRIVE6_CURRENTPOWER_GET) (ADL_CONTEXT_HANDLE, int, int, int*);
	}
}

#endif
