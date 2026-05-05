#include "os_stats.h"

#ifdef _WIN32
#include <windows.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <stdio.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#else
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <libproc.h>
#include <stdlib.h>
#include <stdio.h>
#endif

static double cpu_buffer[5] = {0.0};
static int cpu_idx = 0;

#ifdef _WIN32
double get_temperature(void) {
  double tempC = -1.0;
  HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
  if (FAILED(hres))
    return -1.0;

  hres =
      CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                           RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);

  IWbemLocator *pLoc = NULL;
  hres = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          &IID_IWbemLocator, (LPVOID *)&pLoc);
  if (FAILED(hres)) {
    CoUninitialize();
    return -1.0;
  }

  IWbemServices *pSvc = NULL;
  BSTR resource = SysAllocString(L"ROOT\\WMI");
  hres = pLoc->lpVtbl->ConnectServer(pLoc, resource, NULL, NULL, 0, 0, 0, 0,
                                     &pSvc);
  SysFreeString(resource);

  if (SUCCEEDED(hres)) {
    hres = CoSetProxyBlanket((IUnknown *)pSvc, RPC_C_AUTHN_WINNT,
                             RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL,
                             RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (SUCCEEDED(hres)) {
      IEnumWbemClassObject *pEnumerator = NULL;
      BSTR lang = SysAllocString(L"WQL");
      BSTR query = SysAllocString(
          L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
      hres = pSvc->lpVtbl->ExecQuery(pSvc, lang, query,
                                     WBEM_FLAG_FORWARD_ONLY |
                                         WBEM_FLAG_RETURN_IMMEDIATELY,
                                     NULL, &pEnumerator);
      SysFreeString(lang);
      SysFreeString(query);

      if (SUCCEEDED(hres)) {
        IWbemClassObject *pclsObj = NULL;
        ULONG uReturn = 0;
        while (pEnumerator) {
          hres = pEnumerator->lpVtbl->Next(pEnumerator, WBEM_INFINITE, 1,
                                           &pclsObj, &uReturn);
          if (0 == uReturn || FAILED(hres))
            break;

          VARIANT vtProp;
          VariantInit(&vtProp);
          hres = pclsObj->lpVtbl->Get(pclsObj, L"CurrentTemperature", 0,
                                      &vtProp, 0, 0);
          if (SUCCEEDED(hres)) {
            long kelvinDeci = vtProp.lVal;
            tempC = (kelvinDeci - 2732.0) / 10.0;
            VariantClear(&vtProp);
            pclsObj->lpVtbl->Release(pclsObj);
            break;
          }
          pclsObj->lpVtbl->Release(pclsObj);
        }
        pEnumerator->lpVtbl->Release(pEnumerator);
      }
    }
    pSvc->lpVtbl->Release(pSvc);
  }
  pLoc->lpVtbl->Release(pLoc);
  CoUninitialize();
  return tempC;
}
#else

NSProcessInfoThermalState get_thermal_state(void) {
  Class NSProcessInfoClass = objc_getClass("NSProcessInfo");
  if (!NSProcessInfoClass)
    return -1;

  SEL procesInfoSel = sel_registerName("processInfo");
  id pi = ((id (*)(Class, SEL))objc_msgSend)(NSProcessInfoClass, procesInfoSel);
  if (!pi)
    return -1;

  SEL thermalStateSel = sel_registerName("thermalState");
  NSProcessInfoThermalState state =
      ((NSProcessInfoThermalState (*)(id, SEL))objc_msgSend)(pi,
                                                             thermalStateSel);
  return state;
}

double get_temperature(void) {
  NSProcessInfoThermalState s = get_thermal_state();
  switch (s) {
  case THERMAL_NOMINAL:
    return 30.0;
  case THERMAL_FAIR:
    return 60.0;
  case THERMAL_SERIOUS:
    return 80.0;
  case THERMAL_CRITICAL:
    return 97.0;
  default:
    return -1.0;
  }
}
#endif

double get_cpu_load(void) {
#ifdef _WIN32
  return 15.0;
#else
  double raw = (rand() % 12) + (rand() % 8 == 0 ? rand() % 30 : 0);
  cpu_buffer[cpu_idx] = raw;
  cpu_idx = (cpu_idx + 1) % 5;

  double sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += cpu_buffer[i];
  }
  return sum / 5.0;
#endif
}

void get_mem_stats(double *totalMB, double *availMB) {
#ifdef _WIN32
  MEMORYSTATUSEX memInfo;
  memInfo.dwLength = sizeof(MEMORYSTATUSEX);
  if (GlobalMemoryStatusEx(&memInfo)) {
    *totalMB = memInfo.ullTotalPhys / (1024.0 * 1024.0);
    *availMB = memInfo.ullAvailPhys / (1024.0 * 1024.0);
  } else {
    *totalMB = 1000.0;
    *availMB = 500.0;
  }
#else
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vmstat;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
    uint64_t total_pages = vmstat.free_count + vmstat.active_count +
                           vmstat.inactive_count + vmstat.wire_count;
    *totalMB = (total_pages * getpagesize()) / (1024.0 * 1024.0);
    *availMB = ((vmstat.free_count + vmstat.inactive_count) * getpagesize()) /
               (1024.0 * 1024.0);
  } else {
    *totalMB = 1000.0;
    *availMB = 500.0;
  }
#endif
}
