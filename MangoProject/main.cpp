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


#ifdef _WIN64
#pragma comment(lib, "Bameware Base External_x64.lib")
#pragma comment(lib, "Bameware Base Internal_x64.lib")
#pragma comment(lib, "Bameware Base Shared_x64.lib")
#else
#pragma comment(lib, "Bameware Base External_x86.lib")
#pragma comment(lib, "Bameware Base Internal_x86.lib")
#pragma comment(lib, "Bameware Base Shared_x86.lib")
#endif

static WNDPROC originalWndProc = nullptr;

int Start()
{
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
			/*
			originalWndProc = (WNDPROC)SetWindowLongPtr(
				game_window,
				GWLP_WNDPROC,
				(LONG_PTR)+[](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
					if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
						return true;
					return CallWindowProc(originalWndProc, hWnd, msg, wParam, lParam);
				}
			);
			*/
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
			// � add more ImGui widgets here �
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

		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)Start, NULL, NULL, NULL);
		break;
	}
	}

	return TRUE;
}