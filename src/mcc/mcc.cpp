#include "mcc.h"

#include <offset_mcc.h>
#include "hook/BuildManifest.h"

#include "CGameManager.h"
#include "CGameGlobal.h"

#include "mcc/module/Module.h"
#include "mcc/network/Network.h"
#include "mcc/splitscreen/Splitscreen.h"
#include "mcc/settings/Settings.h"

namespace MCC {
    static bool* bIsInGame;
    static float (__fastcall* deltaTime)(long long qpc);

    float DeltaTime(__int64 a1) {
        return deltaTime ? deltaTime(a1) : 0.0f;
    }

    bool IsInGame() {
        return bIsInGame && *bIsInGame;
    }

    bool Initialize() {
        const auto& offsets = AlphaRing::Compatibility::CurrentBuild().mcc;
        bool result;
        CGameEngine** ppGameEngine = nullptr;
        CGameManager* game_manager = nullptr;
        CDeviceManager** device_manager = nullptr;

        if (!AlphaRing::Hook::Offset({
            {offsets.game_engine, 0, (void**)&ppGameEngine},
            {offsets.game_manager, 0, (void**)&game_manager},
            {offsets.device_manager, 0, (void**)&device_manager},
            {offsets.delta_time, 0, (void**)&deltaTime},
            {offsets.is_in_game, 0, (void**)&bIsInGame},
            {offsets.game_global, 0, (void**)&g_ppGameGlobal},
        })) {
            LOG_ERROR("MCC: failed to resolve the versioned offset manifest");
            return false;
        }

        if (!ppGameEngine || !game_manager || !device_manager || !deltaTime || !bIsInGame || !g_ppGameGlobal) {
            LOG_ERROR("MCC: one or more required runtime pointers are unavailable");
            return false;
        }

        result = CGameEngine::Initialize(ppGameEngine);

        if (!result) {
            LOG_ERROR("MCC: failed to initialize GameEngine");
            return false;
        }

        result = CGameManager::Initialize(game_manager);

        if (!result || !GameManager()) {
            LOG_ERROR("MCC: failed to initialize GameManager");
            return false;
        }

        result = CDeviceManager::Initialize(device_manager);

        if (!result) {
            LOG_ERROR("MCC: failed to initialize DeviceManager");
            return false;
        }

        if (!Module::Initialize())
        {
            LOG_ERROR("MCC: failed to initialize Module");
            return false;
        }

        if (!Splitscreen::Initialize())
        {
            LOG_ERROR("MCC: failed to initialize Splitscreen");
            return false;
        }

        MCC::Settings::Splitscreen::Load();
        MCC::Settings::Profile::CaptureFromRuntime();
        bool profileLoad = MCC::Settings::Profile::Load();
        if(profileLoad) {
            MCC::Settings::Profile::ApplyToRuntime();
            // MCC::Settings::Profile::Initialize(game_manager);
        }
        MCC::Settings::Splitscreen::ApplyToRuntime();

		////Ask user if they want to enable network
  //      if (MessageBox(nullptr, "Would you like to enable network?", "Network", MB_YESNO) == IDYES)
  //      {
  //          if (!Network::Initialize())
  //          {
  //              MessageBox(nullptr, "MCC: failed to initialize Network", "Error", MB_OK);
  //              return false;
  //          }
  //      }

        return true;
    }
}
