#include "win32/wmi.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>
#include <wbemidl.h>

#include <algorithm>
#include <functional>
#include <vector>

#include "win32/common.hpp"

namespace waybar::win32 {

namespace {

std::wstring wmi_error(HRESULT hr) {
  wchar_t buffer[512] = {};
  const DWORD len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, static_cast<DWORD>(hr), 0, buffer,
                                   static_cast<DWORD>(std::size(buffer)), nullptr);
  std::wstring text = len > 0 ? std::wstring(buffer, len) : std::wstring();
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
  return text;
}

void ensure_security() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;
  // 允许 WMI 访问（多数进程已由系统设置好；失败不致命）
  CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                       RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
}

bool with_wmi(const std::function<bool(IWbemServices*)>& callback) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool com_initialized = SUCCEEDED(init);
  if (!com_initialized && init != RPC_E_CHANGED_MODE) {
    return false;
  }

  bool result = false;
  IWbemLocator* locator = nullptr;
  IWbemServices* services = nullptr;

  HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWbemLocator, reinterpret_cast<void**>(&locator));
  if (SUCCEEDED(hr) && locator != nullptr) {
    ensure_security();
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    SysFreeString(ns);

    if (SUCCEEDED(hr) && services != nullptr) {
      hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                             RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                             EOAC_NONE);
      if (SUCCEEDED(hr)) {
        try {
          result = callback(services);
        } catch (...) {
          result = false;
        }
      }
    }
  }

  if (services != nullptr) services->Release();
  if (locator != nullptr) locator->Release();
  if (com_initialized) CoUninitialize();
  return result;
}

bool exec_query(IWbemServices* services, const std::wstring& query,
                const std::function<void(IWbemClassObject*)>& visitor) {
  BSTR language = SysAllocString(L"WQL");
  BSTR text = SysAllocString(query.c_str());
  IEnumWbemClassObject* enumerator = nullptr;
  const HRESULT hr = services->ExecQuery(
      language, text, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
      &enumerator);

  bool any = false;
  if (SUCCEEDED(hr) && enumerator != nullptr) {
    for (;;) {
      IWbemClassObject* object = nullptr;
      ULONG returned = 0;
      const HRESULT next = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
      if (next != S_OK || returned == 0 || object == nullptr) break;
      if (visitor) visitor(object);
      any = true;
      object->Release();
    }
    enumerator->Release();
  } else {
    spdlog::debug("[wmi] query failed: {} ({})", to_utf8(query), to_utf8(wmi_error(hr)));
  }

  SysFreeString(text);
  SysFreeString(language);
  return any;
}

bool get_variant(IWbemClassObject* object, const wchar_t* property, VARIANT& value) {
  VariantInit(&value);
  if (object->Get(property, 0, &value, nullptr, nullptr) != S_OK) {
    VariantClear(&value);
    return false;
  }
  return true;
}

}  // namespace

bool wmi_query_u32(const std::wstring& query, const wchar_t* property, uint32_t& out) {
  uint32_t result = 0;
  bool found = false;

  with_wmi([&](IWbemServices* services) {
    return exec_query(services, query, [&](IWbemClassObject* object) {
      VARIANT value;
      if (get_variant(object, property, value)) {
        if (value.vt == VT_I4 || value.vt == VT_UI4 || value.vt == VT_UINT) {
          result = static_cast<uint32_t>(value.lVal);
          found = true;
        } else if (value.vt == VT_UI1) {
          result = value.bVal;
          found = true;
        } else if (value.vt == VT_BSTR) {
          result = static_cast<uint32_t>(_wtoi(value.bstrVal));
          found = true;
        }
        VariantClear(&value);
      }
    });
  });

  if (found) out = result;
  return found;
}

bool wmi_query_string(const std::wstring& query, const wchar_t* property, std::string& out) {
  std::string result;
  bool found = false;

  with_wmi([&](IWbemServices* services) {
    return exec_query(services, query, [&](IWbemClassObject* object) {
      VARIANT value;
      if (get_variant(object, property, value)) {
        if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
          result = to_utf8(value.bstrVal);
          found = true;
        }
        VariantClear(&value);
      }
    });
  });

  if (found) out = result;
  return found;
}

bool wmi_brightness_query(uint32_t& percent) {
  return wmi_query_u32(L"SELECT CurrentBrightness FROM WmiMonitorBrightness", L"CurrentBrightness",
                       percent);
}

bool wmi_brightness_set(uint8_t percent) {
  return with_wmi([&](IWbemServices* services) -> bool {
    bool invoked = false;

    exec_query(services,
               L"SELECT InstanceName FROM WmiMonitorBrightnessMethods", [&](IWbemClassObject* obj) {
                 if (invoked) return;

                 VARIANT path_value;
                 if (!get_variant(obj, L"__PATH", path_value)) return;
                 BSTR instance_path = SysAllocString(path_value.bstrVal);
                 VariantClear(&path_value);

                 IWbemClassObject* class_object = nullptr;
                 BSTR class_name = SysAllocString(L"WmiMonitorBrightnessMethods");
                 if (FAILED(services->GetObject(class_name, 0, nullptr, &class_object, nullptr))) {
                   SysFreeString(class_name);
                   SysFreeString(instance_path);
                   return;
                 }
                 SysFreeString(class_name);

                 IWbemClassObject* method = nullptr;
                 BSTR method_name = SysAllocString(L"WmiSetBrightness");
                 IWbemClassObject* in_params = nullptr;
                 if (SUCCEEDED(class_object->GetMethod(method_name, 0, &in_params, nullptr))) {
                   IWbemClassObject* in_instance = nullptr;
                   if (SUCCEEDED(in_params->SpawnInstance(0, &in_instance))) {
                     VARIANT timeout;
                     VariantInit(&timeout);
                     timeout.vt = VT_I4;
                     timeout.lVal = 0;
                     in_instance->Put(L"Timeout", 0, &timeout, 0);

                     VARIANT brightness;
                     VariantInit(&brightness);
                     brightness.vt = VT_UI1;
                     brightness.bVal = percent;
                     in_instance->Put(L"Brightness", 0, &brightness, 0);

                     IWbemClassObject* out_params = nullptr;
                     const HRESULT hr = services->ExecMethod(
                         instance_path, method_name, 0, nullptr, in_instance, &out_params, nullptr);
                     invoked = SUCCEEDED(hr);
                     if (out_params != nullptr) out_params->Release();
                     in_instance->Release();
                   }
                   in_params->Release();
                 }
                 SysFreeString(method_name);
                 class_object->Release();
                 SysFreeString(instance_path);
               });

    return invoked;
  });
}

bool wmi_temperature_query(double& celsius) {
  double best = 0.0;
  bool found = false;

  with_wmi([&](IWbemServices* services) {
    return exec_query(services, L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature",
                      [&](IWbemClassObject* object) {
                        VARIANT value;
                        if (get_variant(object, L"CurrentTemperature", value)) {
                          if (value.vt == VT_I4 || value.vt == VT_UI4) {
                            // 单位 0.1 开尔文
                            const double kelvin_tenths = static_cast<double>(value.lVal);
                            const double temp_c = kelvin_tenths / 10.0 - 273.15;
                            if (temp_c > -50.0 && temp_c < 200.0) {
                              best = std::max(best, temp_c);
                              found = true;
                            }
                          }
                          VariantClear(&value);
                        }
                      });
  });

  if (found) celsius = best;
  return found;
}

}  // namespace waybar::win32
