#include "stdafx.h"
#include "resource.h"
#include "AuraSense_i.h"

#include "AURALightingSDK.h"
#include "ProcessorUsage.h"
#include "Smoother.h"
#include "tinyxml2.h"

#include "powerbase.h"
#include "powrprof.h"

#include <vector>
#include <atomic>

using namespace ATL;

int BaseRate		= 30;	// affects base pulse rate when cpu usage == 0, higher values increase base pulse rate, 10-120 are good values
int ScaleRate		= 6000;	// affects overall pulse rate at both ends of cpu usage (0-256), higher values slow down pulse rate
int MinBright		= 8;	// minimum brightness, 0 to MaxBright
int MaxBright		= 248;	// maximum brightness, MinBright to 256
int ColorSmooth		= 60;	// average of last x cpu polls
int PulseSmooth		= 10;	// average of last x cpu polls
int FrameTime		= 33;	// rough time in milliseconds between frames, lower values use more cpu time, higher values are not as smooth

struct LED { BYTE r, b, g; };

LED Color0		= { 0,0,255 }; // color when cpu usage is 0 %
LED Color100	= { 255,0,0 }; // color when cpu usage is 100 %

EnumerateMbControllerFunc EnumerateMbController;
SetMbModeFunc SetMbMode;
SetMbColorFunc SetMbColor;
GetMbLedCountFunc GetMbLedCount;

// SetMbMode mode optins
const int SetMbMode_DEFAULT_EC			= 0; // default hardware control
const int SetMbMode_SOFTWARE_CONTROL	= 1; // software override

CComCriticalSection g_critical;

MbLightControl* g_mbLightCtrl = nullptr;

HPOWERNOTIFY g_power_notify_handle = NULL;

std::atomic<bool> g_running = true;
std::atomic<bool> g_waiting = false;


void LightShow();

class CAuraSenseModule : public ATL::CAtlServiceModuleT< CAuraSenseModule, IDS_SERVICENAME >
{
public:
	DECLARE_LIBID(LIBID_AuraSenseLib)
	DECLARE_REGISTRY_APPID_RESOURCEID(IDR_AURASENSE, "{11440fb2-6ae6-48d0-8a36-fefa3c055694}")

	void OnStop()
	{
		PowerUnregisterSuspendResumeNotification(g_power_notify_handle);

		g_critical.Lock();
		SetMbMode(g_mbLightCtrl[0], SetMbMode_DEFAULT_EC);
		g_critical.Unlock();

		g_running = false;
		g_waiting = true;

		CAtlServiceModuleT::OnStop();
	}

	HRESULT Run(_In_ int nShowCmd = SW_HIDE)
	{
		CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) LightShow, 0, 0, 0);

		return CAtlServiceModuleT::Run(nShowCmd);
	}
};

CAuraSenseModule _AtlModule;

extern "C" int WINAPI _tWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
	LPTSTR /*lpCmdLine*/, int nShowCmd)
{
	return _AtlModule.WinMain(nShowCmd);
}


double EaseInOutSine(double t)
{
	return 0.5 * (1 + sin(3.1415926 * (t - 0.5)));
}

double Cycle(double t)
{
	t *= 2;
	if (t > 1)
		t =  2 - t;
	return t;
}

int AddWrap(int a, int b, int wrap)
{
	return (a + b) % wrap;
}

LED InterpolateColor(const LED& col1, const LED& col2, const int brightness, const int slider)
{
	LED result;
	result.r = (col1.r + ((col2.r - col1.r) * slider / 256)) * brightness / 256;
	result.g = (col1.g + ((col2.g - col1.g) * slider / 256)) * brightness / 256;
	result.b = (col1.b + ((col2.b - col1.b) * slider / 256)) * brightness / 256;
	return result;
}

ULONG CALLBACK DeviceCallback(PVOID Context, ULONG Type, PVOID Setting)
{
	if (Type == PBT_APMSUSPEND)
	{
		g_waiting = true;
	}
	if (Type == PBT_APMRESUMESUSPEND)
	{
		// mode reverts to hardware default after sleeping, so kick it back to software control
		g_critical.Lock();
		SetMbMode(g_mbLightCtrl[0], SetMbMode_SOFTWARE_CONTROL);
		g_critical.Unlock();

		g_waiting = false;
	}
	return ERROR_SUCCESS;
}

void LoadConfiguration()
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLError err = doc.LoadFile("AuraSense.xml");
	if (tinyxml2::XML_SUCCESS != err) {
		_AtlModule.LogEvent(_T("Could not load config.xml, error %d, using defaults"), err);
		return;
	}

	tinyxml2::XMLElement* root = doc.FirstChildElement( "Settings" );
	if (!root) {
		_AtlModule.LogEvent(_T("Could not find Settings root in AuraSense.xml"));
		return;
	}

	tinyxml2::XMLElement* el;
	int tmp;

	el = root->FirstChildElement( "BaseRate" );
	if (el)
		el->QueryIntAttribute( "value", &BaseRate );

	el = root->FirstChildElement("ScaleRate");
	if (el)
		el->QueryIntAttribute("value", &ScaleRate);

	el = root->FirstChildElement("MinBright");
	if (el)
		el->QueryIntAttribute("value", &MinBright);

	el = root->FirstChildElement("MaxBright");
	if (el)
		el->QueryIntAttribute("value", &MaxBright);

	el = root->FirstChildElement("ColorSmooth");
	if (el)
		el->QueryIntAttribute("value", &ColorSmooth);

	el = root->FirstChildElement("PulseSmooth");
	if (el)
		el->QueryIntAttribute("value", &PulseSmooth);

	el = root->FirstChildElement("FrameTime");
	if (el)
		el->QueryIntAttribute("value", &FrameTime);

	el = root->FirstChildElement("Color0");
	if (el) {
		el->QueryIntAttribute("r", &tmp);
		Color0.r = tmp;
		el->QueryIntAttribute("g", &tmp);
		Color0.g = tmp;
		el->QueryIntAttribute("b", &tmp);
		Color0.b = tmp;
	}
	el = root->FirstChildElement("Color100");
	if (el) {
		el->QueryIntAttribute("r", &tmp);
		Color100.r = tmp;
		el->QueryIntAttribute("g", &tmp);
		Color100.g = tmp;
		el->QueryIntAttribute("b", &tmp);
		Color100.b = tmp;
	}
}

void LightShow() {

	HMODULE hLib = LoadLibrary( _T("AURA_SDK.dll") );

	if (hLib == nullptr) {
		_AtlModule.LogEvent( _T("Failed to load AURA_SDK.dll error=0x%x"), GetLastError() );
		return;
	}

	LoadConfiguration();

	(FARPROC&)EnumerateMbController = GetProcAddress(hLib, "EnumerateMbController");
	(FARPROC&)SetMbMode = GetProcAddress(hLib, "SetMbMode");
	(FARPROC&)SetMbColor = GetProcAddress(hLib, "SetMbColor");
	(FARPROC&)GetMbLedCount = GetProcAddress(hLib, "GetMbLedCount");

	g_critical.Init();

	DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params;
	params.Callback = DeviceCallback;
	params.Context = 0;

	PowerRegisterSuspendResumeNotification (DEVICE_NOTIFY_CALLBACK, &params, &g_power_notify_handle);

	const DWORD controller_count = EnumerateMbController(NULL, 0);

	g_mbLightCtrl = new MbLightControl[controller_count];
	EnumerateMbController(g_mbLightCtrl, controller_count);

	SetMbMode(g_mbLightCtrl[0], SetMbMode_SOFTWARE_CONTROL);

	const DWORD led_count = GetMbLedCount(g_mbLightCtrl[0]);

	LED* led_color = new LED[led_count];
	ZeroMemory(led_color, led_count * sizeof(LED) );

	int index = 0;
	DWORD last_time = GetTickCount();

	ProcessorUsage cpu_usage;
	Smoother color_avg(ColorSmooth);
	Smoother pulse_avg(PulseSmooth);

	while (g_running) {

		if (!g_waiting)
		{
			const int cpu = cpu_usage.GetUsage();
			const int cpu_color = color_avg.AddValue(cpu);
			const int cpu_pulse = pulse_avg.AddValue(cpu);

			// Sleep fluctuates despite constant time, but GetTickCount is better so scale based on actual time
			const DWORD now_time = GetTickCount();
			const DWORD actual = now_time - last_time;
			last_time = now_time;

			index = AddWrap(index, (BaseRate + cpu_pulse) * actual / FrameTime, ScaleRate);

			double intensity = Cycle(EaseInOutSine(index / (double)ScaleRate));
			intensity = MinBright + (intensity * (MaxBright - MinBright));

			LED col = InterpolateColor(Color0, Color100, (int)intensity, cpu_color);
			for (size_t i = 0; i < led_count; ++i)
				led_color[i] = col;

			// is the Aura SDK thread safe?, I don't know, let's be safe and sync with other threads
			g_critical.Lock();
			SetMbColor(g_mbLightCtrl[0], (BYTE*)led_color, led_count * sizeof(LED));
			g_critical.Unlock();
		}
		Sleep(FrameTime); // sleep thread so as not to hog cpu
	}

	delete[] led_color;
	delete[] g_mbLightCtrl;

	FreeLibrary(hLib);
}

