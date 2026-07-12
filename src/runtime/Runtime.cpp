#include "Runtime.h"

#include <atomic>

#include "filesystem/Filesystem.h"
#include "hook/Hook.h"
#include "input/Input.h"
#include "log/Log.h"
#include "mcc/mcc.h"
#include "render/Render.h"

namespace AlphaRing::Runtime {
    namespace {
        std::atomic<State> state {State::Dormant};

        struct InitializedComponents {
            bool log = false;
            bool hooks = false;
            bool filesystem = false;
            bool input = false;
            bool render = false;
        } initialized;

        void UnwindInitialization() {
            if (initialized.render)
                Render::Shutdown();
            if (initialized.input)
                Input::Shutdown();
            if (initialized.filesystem)
                Filesystem::Shutdown();
            if (initialized.hooks)
                Hook::Shutdown();
            if (initialized.log)
                Log::Shutdown();
            initialized = {};
        }

        bool Fail(const char* component) {
            if (initialized.log)
                LOG_ERROR("Initialization failed in {}", component);
            else
                OutputDebugStringA("AlphaRing: initialization failed before logging was available\n");

            state.store(State::Failed, std::memory_order_release);
            UnwindInitialization();
            return false;
        }
    }

    DWORD WINAPI Bootstrap(void*) {
        State expected = State::Dormant;
        if (!state.compare_exchange_strong(expected, State::Starting, std::memory_order_acq_rel))
            return 1;

        initialized.filesystem = Filesystem::Init();
        if (!initialized.filesystem)
            return Fail("filesystem") ? 0 : 1;

        initialized.log = Log::Init();
        if (!initialized.log)
            return Fail("logging") ? 0 : 1;

        initialized.hooks = Hook::Initialize();
        if (!initialized.hooks)
            return Fail("hook compatibility") ? 0 : 1;

        initialized.input = Input::Init();
        if (!initialized.input)
            return Fail("input") ? 0 : 1;

        initialized.render = Render::Initialize();
        if (!initialized.render)
            return Fail("renderer") ? 0 : 1;

        if (!MCC::Initialize())
            return Fail("MCC integration") ? 0 : 1;

        state.store(State::Running, std::memory_order_release);
        LOG_INFO("AlphaRing initialization complete");
        return 0;
    }

    void RequestStop() {
        State current = state.load(std::memory_order_acquire);
        while (current != State::Stopped && current != State::Stopping && current != State::Dormant) {
            if (state.compare_exchange_weak(current, State::Stopping, std::memory_order_acq_rel))
                return;
        }
    }

    bool Shutdown() {
        const State current = state.exchange(State::Stopping, std::memory_order_acq_rel);
        if (current == State::Dormant || current == State::Stopped)
            return true;

        UnwindInitialization();
        state.store(State::Stopped, std::memory_order_release);
        return true;
    }

    State GetState() {
        return state.load(std::memory_order_acquire);
    }
}
