// SPDX-License-Identifier: Apache-2.0
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <future>
#include <iostream>
#include <memory>

#define UserAgent L"pac-probe/0.0.1"

template<> struct std::default_delete<HINTERNET> {
	typedef HINTERNET pointer;
	void operator()(const HINTERNET h) const {
		WinHttpCloseHandle(h);
	}
};

template<> struct std::default_delete<WINHTTP_PROXY_SETTINGS_EX> {
	void operator()(WINHTTP_PROXY_SETTINGS_EX* p) const {
		WinHttpFreeProxySettingsEx(WinHttpProxySettingsTypeWsl, p);
	}
};

typedef std::promise<DWORD> Promise;

static void CALLBACK proxy_settings_callback(HINTERNET, DWORD_PTR ctx, DWORD status, LPVOID, DWORD)
{
	reinterpret_cast<Promise*>(ctx)->set_value(status);
}

std::unique_ptr<WINHTTP_PROXY_SETTINGS_EX> GetProxySetings()
{
	HINTERNET hWinHttp = WinHttpOpen(
		UserAgent,
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		WINHTTP_FLAG_ASYNC); // require async operation flag
	if (hWinHttp == nullptr) {
		return nullptr;
	}
	auto uhWinHttp = std::unique_ptr<HINTERNET>(hWinHttp);

	HINTERNET hResolver;
	if (WinHttpCreateProxyResolver(uhWinHttp.get(), &hResolver) != ERROR_SUCCESS) {
		return nullptr;
	}
	auto uhResolver = std::unique_ptr<HINTERNET>(hResolver);

	if (WinHttpSetStatusCallback(
		uhResolver.get(),
		proxy_settings_callback,
		WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS,
		NULL) == WINHTTP_INVALID_STATUS_CALLBACK) {
		return nullptr;
	}

	Promise promise;
	WinHttpGetProxySettingsEx(
		uhResolver.get(),
		WinHttpProxySettingsTypeWsl,
		nullptr,
		reinterpret_cast<DWORD_PTR>(&promise));

	// wait
	auto status = promise.get_future().get();
	if (status != WINHTTP_CALLBACK_STATUS_GETPROXYSETTINGS_COMPLETE) {
		return nullptr;
	}

	// get result
	WINHTTP_PROXY_SETTINGS_EX settings;
	if (WinHttpGetProxySettingsResultEx(uhResolver.get(), &settings) != ERROR_SUCCESS) {
		return nullptr;
	}

	return std::make_unique<WINHTTP_PROXY_SETTINGS_EX>(settings);
}

int wmain(int, wchar_t const*[])
{
	auto settings = GetProxySetings();
	if (settings) {
		auto pacurl = settings->pcwszAutoconfigUrl;
		if (pacurl != nullptr) {
			std::wcout << settings->pcwszAutoconfigUrl << std::endl;
		}
	}
	return 0;
}
