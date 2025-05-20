#include <Windows.h>
#include <chrono>
#include <thread>
#include <stdexcept>

#include "../Bameware Base Shared/Headers/Rendering/Renderer.h"
#include "../Bameware Base External/Headers/MemoryManager.h"
#include "../Bameware Base Internal/Headers/Hooks/VMTHookManager.h"
#include "../Bameware Base Shared/Headers/FileParser.h"
#include "../Bameware Base Shared/Headers/Pipes/PipeClient.h"

#include "thirdparty/uni_imgui/imguii/imgui.h"
#include "thirdparty/uni_imgui/imgui_impl_dx11.h"
#include "thirdparty/uni_imgui/imgui_impl_win32.h"


#include "thirdparty/instux/fakelag.h"
#include "thirdparty/instux/SDK/fake_usercmd.h"
#include "thirdparty/minhook-master/include/MinHook.h"


#ifdef _WIN64
#pragma comment(lib, "Bameware Base External_x64.lib")
#pragma comment(lib, "Bameware Base Internal_x64.lib")
#pragma comment(lib, "Bameware Base Shared_x64.lib")
#else
#pragma comment(lib, "Bameware Base External_x86.lib")
#pragma comment(lib, "Bameware Base Internal_x86.lib")
#pragma comment(lib, "Bameware Base Shared_x86.lib")
#endif

template <typename T = void*>
inline T GetVFunc(const void* thisptr, std::size_t nIndex)
{
	return (*static_cast<T* const*>(thisptr))[nIndex];
}

constexpr uintptr_t relative_to_absolute(uintptr_t address, int offset, int instruction_size = 7)
{
	auto instruction = address + offset;
	int relative_address = *(int*)(instruction);
	return address + instruction_size + relative_address;
}

template<typename T>
T* get_vmt_from_instruction(uintptr_t address, size_t offset = 0)
{
	uintptr_t step = 3;
	uintptr_t instruction = address + offset;

	uintptr_t real_address = relative_to_absolute(instruction, step);
	return *(T**)(real_address);
}

std::uintptr_t FindPattern(const char* szModuleName, const char* szPattern)
{
	static auto PatternByte = [](const char* szPattern) -> std::vector<int>
		{
			std::vector<int> vecBytes{ };
			char* chStart = (char*)szPattern;
			char* chEnd = (char*)szPattern + strlen(szPattern);

			// convert pattern into bytes
			for (char* chCurrent = chStart; chCurrent < chEnd; ++chCurrent)
			{
				// check is current byte a wildcard
				if (*chCurrent == '?')
				{
					++chCurrent;

					// check is next byte is also wildcard
					if (*chCurrent == '?')
						++chCurrent;

					// ignore that
					vecBytes.push_back(-1);
				}
				else
					// convert byte to hex
					vecBytes.push_back(strtoul(chCurrent, &chCurrent, 16));
			}

			return vecBytes;
		};

	const HMODULE hModule = GetModuleHandle(szModuleName);
	if (hModule == nullptr)
	{
		return 0U;
	}

	auto pDosHeader = (PIMAGE_DOS_HEADER)hModule;
	auto pNtHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)hModule + pDosHeader->e_lfanew);

	DWORD dwSizeOfImage = pNtHeaders->OptionalHeader.SizeOfImage;
	std::vector<int> vecBytes = PatternByte(szPattern);
	std::uint8_t* puBytes = (std::uint8_t*)(hModule);

	std::size_t uSize = vecBytes.size();
	int* pBytes = vecBytes.data();

	// check for bytes sequence match
	for (unsigned long i = 0UL; i < dwSizeOfImage - uSize; ++i)
	{
		bool bByteFound = true;

		for (unsigned long j = 0UL; j < uSize; ++j)
		{
			// check if doenst match or byte isnt a wildcard
			if (puBytes[i + j] != pBytes[j] && pBytes[j] != -1)
			{
				bByteFound = false;
				break;
			}
		}

		// return valid address
		if (bByteFound)
			return (std::uintptr_t)(&puBytes[i]);
	}

#if DEBUG_CONSOLE && _DEBUG
	L::PushConsoleColor(FOREGROUND_RED);
	L::Print(fmt::format(XorStr("[error] failed get pattern: [{}] [{}]"), szModuleName, szPattern));
	L::PopConsoleColor();
#endif

	return 0U;
}

static bool   rebindOpen = false;
static char   keyNameBuf[64] = "End";  // default name

using tCreateMove = bool(__thiscall*)(void*, float, CUserCmd*);
static tCreateMove oCreateMove = nullptr;

bool __fastcall hkCreateMove(void* cl, float inputSampleTime, CUserCmd* cmd)
{
	return oCreateMove(cl, inputSampleTime, cmd);
};

using tCLMove = void(__cdecl*)(float, bool);
static tCLMove oCLMove = nullptr;
// speedhack
int speedhak;
void __cdecl hkCLMove(float inputSampleTime, bool pp)
{

	oCLMove(inputSampleTime, pp);
	for (int i = 0; i <speedhak; i++)
	 oCLMove(inputSampleTime, pp);
};

static WNDPROC originalWndProc = nullptr;

LRESULT CALLBACK GameWindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return TRUE;
	return CallWindowProc(originalWndProc, hWnd, msg, wParam, lParam);
}
// asteya was here
int Start()
{
	printf("Start() has begun!\n");
	const auto GetGamePID = []() -> uint32_t
	{
		while (true)
		{
			auto processes = BAMEWARE::MemoryManager<uint64_t>::GetProcesses("insurgency_x64.exe");

			for (const auto& process : processes)
			{
				for (const auto& mod : process.m_modules)
				{
					if (mod.m_module_name == "client.dll")
						return process.m_process_id;
				}
			}
			Sleep(1000);
		}
	};

	/// initialize memory manager
	BAMEWARE::MemoryManager<uint64_t> memory_manager;
	if (!memory_manager.Initialize(GetGamePID()))
	{
		std::cout << "Failed to initialize memory manager" << std::endl;
		memory_manager.Release();
		getchar();
		return 0;
	}

	/// initialize renderer
	if (!BAMEWARE::g_renderer.Initialize("femboyz", BAMEWARE::Vector2DI({ 1280, 720 }), true, 1000.0f, 1.f))
	{
		std::cout << "Failed to initialize renderer" << std::endl;
		BAMEWARE::g_renderer.Release();
		memory_manager.Release();
		getchar();
		return 0;
	}

	/// get game window
	const auto game_window = FindWindow(nullptr, "INSURGENCY");
	if (!game_window)
	{
		std::cout << "Failed to find window" << std::endl;
		BAMEWARE::g_renderer.Release();
		memory_manager.Release();
		getchar();
		return 0;
	}

	bool imguiInit = false;
	bool showMenu = false;
	bool espEnabled = false;

	///client.dll + 916CE4
	const auto client_dll = memory_manager.GetModuleAddress("client.dll");
	const auto engine_dll = memory_manager.GetModuleAddress("engine.dll");

	const auto client_entity_list = memory_manager.ReadMemory<uintptr_t>(engine_dll, 0x61DD50);

	const auto CreateInterface = [](const char* module_name, const char* interface_name) -> void*
	{
		const auto func = reinterpret_cast<void*(*)(const char*, int*)>(GetProcAddress(GetModuleHandleA(module_name), "CreateInterface"));

		void* found_interface = nullptr;
		char possible_name[1024];
		for (int i = 1; i < 100; i++)
		{
			sprintf(possible_name, "%s0%i", interface_name, i);
			found_interface = func(possible_name, nullptr);
			if (found_interface)
				break;

			sprintf(possible_name, "%s00%i", interface_name, i);
			found_interface = func(possible_name, nullptr);
			if (found_interface)
				break;
		}
		return found_interface;
	};

	const auto RecreatedGetClientEntity = [client_entity_list](int index) -> uintptr_t
	{
		return *reinterpret_cast<uintptr_t*>(0x20 * (index - 0x2001i64) + uintptr_t(client_entity_list));
	};
	
	uintptr_t clientBase = 0;
	while (!(clientBase = memory_manager.GetModuleAddress("client.dll")))
		Sleep(100);

	// grab real CreateInterface
	using CreateInterfaceFn = void* (*)(const char*, int*);
	CreateInterfaceFn iface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(GetModuleHandleA("engine.dll"), "CreateInterface"));
	if (!iface) { printf("No CreateInterface in engine.dll\n"); return 0; }

	//void* client = CreateInterface("client.dll", "VClient");

	//auto ass = relative_to_absolute((uintptr_t)FindPattern("client.dll", "48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8D 05"), 3, 7);
	//void* clientnode = *(void**)ass;


	MH_Initialize();
	MH_CreateHook((void*)FindPattern("client.dll", "40 53 48 83 EC ? 83 C9 ? 0F 29 74 24"), hkCreateMove, (void**)&oCreateMove);
	MH_CreateHook((void*)FindPattern("engine.dll", "48 89 5C 24 ? 57 48 83 EC ? 0F 29 74 24 ? 0F B6 DA"), hkCLMove, (void**)&oCLMove);
	MH_EnableHook(MH_ALL_HOOKS);
	
	// engine.dll + 0x61DD50

	unsigned int frames = 0; float last_frame_update = 0.f;
	while (BAMEWARE::g_renderer.NextFrame(BAMEWARE::ColorRGBA(0, 0, 0, 0)))
	{
		BAMEWARE::g_renderer.SetDimensions(game_window);
		/// engine.dll+4FCF4C, engine.dll+7399D4
		const auto view_matrix = memory_manager.ReadMemory<BAMEWARE::Matrix4x4>(engine_dll, 0x7399D4);
		/// client.dll+90FBA0 | player dead or in spec turn off esp
		const auto local_player = memory_manager.ReadMemory<uintptr_t>(client_dll, 0x90FBA0);
		//memory_manager.ReadMemory<int>(local_player, 0x12C) <= 0 // local player health
		bool isAlive = false;
		if (local_player)
		{
			int health = memory_manager.ReadMemory<int>(local_player, 0x12C);
			isAlive = (health > 0);
		}

		if (!imguiInit)
		{
			ImGui::CreateContext();
			ImGui::StyleColorsDark();

			ImGui_ImplWin32_Init(BAMEWARE::g_renderer.GetWindowHandle());
			ImGui_ImplDX11_Init(BAMEWARE::g_renderer.GetDevice(),
			BAMEWARE::g_renderer.GetDeviceContext());

			//Chams::Initialize(BAMEWARE::g_renderer.GetDevice(), BAMEWARE::g_renderer.GetDeviceContext());
			
			originalWndProc = (WNDPROC)SetWindowLongPtr(
				game_window,
				GWLP_WNDPROC,
				(LONG_PTR)GameWindowProc
			);
			
			imguiInit = true;
		}

		if (GetAsyncKeyState(VK_INSERT) & 1)
			showMenu = !showMenu;

		// Start a new ImGui frame *every frame*
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();


		if (showMenu)
		{
			ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(800, 600));

			ImGui::Begin("Mango Menu");
			ImGui::Checkbox("Enable ESP", &espEnabled);
			ImGui::SliderInt("speedhak", &speedhak, 0, 10);
			ImGui::End();
		}

		// esp shit i think
		const auto local_team = memory_manager.ReadMemory<int>(local_player, 0x120);
		if (espEnabled && isAlive) {
			for (int i = 0; i < 64; i++)
			{
				const auto entity = RecreatedGetClientEntity(i);
				if (!entity || memory_manager.ReadMemory<int>(entity, 0x12C) <= 0 || local_team == memory_manager.ReadMemory<int>(entity, 0x120))
					continue;

				const auto origin = memory_manager.ReadMemory<BAMEWARE::Vector3DF>(entity, 0x164);
				if (BAMEWARE::Vector2DI screen_top, screen_bottom;
					BAMEWARE::g_renderer.WorldToScreen(screen_bottom, origin, view_matrix) &&
					BAMEWARE::g_renderer.WorldToScreen(screen_top, origin + BAMEWARE::Vector3DF({ 0, 0, 80.f }), view_matrix))
				{
					const int box_width = (screen_bottom[1] - screen_top[1]) * 0.25f;

					BAMEWARE::RenderVertex_t vertices[4];

					vertices[0].m_position = BAMEWARE::Vector2DI({ screen_top[0] - box_width, screen_top[1] });
					vertices[1].m_position = BAMEWARE::Vector2DI({ screen_top[0] + box_width, screen_top[1] });
					vertices[2].m_position = BAMEWARE::Vector2DI({ screen_bottom[0] + box_width, screen_bottom[1] });
					vertices[3].m_position = BAMEWARE::Vector2DI({ screen_bottom[0] - box_width, screen_bottom[1] });

					const float hue = fmod(GetTickCount() * 0.001f, 1.f);
					vertices[0].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(hue, 1.f, 1.f)).GetFloatVec();
					vertices[1].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(fmod(hue + 0.25f, 1.f), 1.f, 1.f)).GetFloatVec();
					vertices[2].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(fmod(hue + 0.5f, 1.f), 1.f, 1.f)).GetFloatVec();
					vertices[3].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(fmod(hue + 0.75f, 1.f), 1.f, 1.f)).GetFloatVec();

					BAMEWARE::g_renderer.RenderQuad(vertices[0], vertices[1], vertices[2], vertices[3], false);

					vertices[0].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(fmod(hue + 0.5f, 1.f), 1.f, 1.f, 50)).GetFloatVec();
					vertices[1].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(fmod(hue + 0.75f, 1.f), 1.f, 1.f, 50)).GetFloatVec();
					vertices[2].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(fmod(hue, 1.f), 1.f, 1.f, 50)).GetFloatVec();
					vertices[3].m_color = BAMEWARE::ColorRGBA(BAMEWARE::ColorHSBA(fmod(hue + 0.25f, 1.f), 1.f, 1.f, 50)).GetFloatVec();

					BAMEWARE::g_renderer.RenderQuad(vertices[0], vertices[1], vertices[2], vertices[3], true);
				}
			}
		}

		// 6) Render ImGui on top of the overlay
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		BAMEWARE::g_renderer.EndFrame();
	}

	BAMEWARE::g_renderer.Release();
	memory_manager.Release();

	/*
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext(ImGui::GetCurrentContext());
	*/
	MH_RemoveHook(MH_ALL_HOOKS);
	MH_Uninitialize();

	return 0;
}


BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
	{
		AllocConsole();
		auto hMenu = GetSystemMenu(GetConsoleWindow(), FALSE);
		if (hMenu)
			DeleteMenu(hMenu, SC_CLOSE, MF_BYCOMMAND);

		SetConsoleTitle("Console:");
		freopen("CONIN$", "r", stdin);
		freopen("CONOUT$", "w", stdout);
		setvbuf(stdout, nullptr, _IONBF, 0);

		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Start, NULL, NULL, NULL);
		break;
	}
	}

	return TRUE;
}
