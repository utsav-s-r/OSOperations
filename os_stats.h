#ifndef ZENITH_OS_STATS_H
#define ZENITH_OS_STATS_H

double get_temperature(void);
double get_cpu_load(void);
void get_mem_stats(double *totalMB, double *availMB);

#ifndef _WIN32
typedef long NSProcessInfoThermalState;

#define THERMAL_NOMINAL 0
#define THERMAL_FAIR 1
#define THERMAL_SERIOUS 2
#define THERMAL_CRITICAL 3

NSProcessInfoThermalState get_thermal_state(void);
#endif

#endif // ZENITH_OS_STATS_H
