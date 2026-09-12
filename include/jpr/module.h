// Module framework.
//
// Adding a module is one file and one macro:
//
//     class MyModule : public jpr::Module { ... };
//     JPR_REGISTER_MODULE(MyModule);
//
// Registration happens during static initialisation, before mod_init runs, so
// there is no central list to keep in sync. Config binding, enable/disable
// handling and event dispatch all come from the base class.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "jpr/config.h"
#include "jpr/events.h"

namespace jpr {

class Module {
public:
    virtual ~Module() = default;

    // Stable identifier; also the key under "modules" in jpr.json.
    virtual const char* id() const = 0;

    // One line shown in the generated reference file.
    virtual const char* description() const { return ""; }

    // Describe every setting, including "enabled" — the base class does not
    // add one implicitly so a module can document its own wording.
    virtual void settings(SettingSet& set) = 0;

    // Called after settings() whenever the config was (re)loaded, including
    // the first time. `enabledChanged` says whether this call flipped the
    // module between on and off.
    virtual void onConfigured(bool enabledChanged) { (void)enabledChanged; }

    virtual void onTick(const TickContext& tick) { (void)tick; }
    virtual void onLocalPlayerHurt(const HurtEvent& event) { (void)event; }

    // Override and return true when the module needs damage events. Installing
    // the damage hook costs a signature scan, so it only happens when some
    // enabled module actually asks for it.
    virtual bool wantsHurtEvents() const { return false; }

    // True while the module should act. Modules must check this themselves;
    // dispatch does not filter, because a module may need a tick to finish
    // cleaning up after being switched off.
    bool enabled() const { return enabled_; }

protected:
    bool enabled_ = false;
};

class ModuleManager {
public:
    static ModuleManager& instance();

    void add(std::unique_ptr<Module> module);

    // Applies the loaded config to every module and regenerates the settings
    // reference file.
    void applyConfig();

    void dispatchTick(const TickContext& tick);
    void dispatchLocalPlayerHurt(const HurtEvent& event);

    // True when at least one enabled module wants hurt events, so the game
    // hooks know whether they are worth installing.
    bool anyWantsHurtEvents() const;

    const std::vector<std::unique_ptr<Module>>& modules() const { return modules_; }

private:
    ModuleManager() = default;

    std::vector<std::unique_ptr<Module>> modules_;
};

template <typename T>
struct ModuleRegistrar {
    ModuleRegistrar() { ModuleManager::instance().add(std::unique_ptr<Module>(new T())); }
};

#define JPR_REGISTER_MODULE(Type) static ::jpr::ModuleRegistrar<Type> jpr_registrar_##Type

}  // namespace jpr
