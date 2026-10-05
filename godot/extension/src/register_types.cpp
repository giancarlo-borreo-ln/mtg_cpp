// GDExtension entry point (Phase 2). Registers every Mtgcpp* class with Godot's
// ClassDB at the SCENE initialization level, so the classes are available to
// GDScript as soon as the extension loads.

#include "mtg_cpp_bindings.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

namespace {

void initialize_mtg_cpp_module(godot::ModuleInitializationLevel p_level) {
  if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
  GDREGISTER_CLASS(MtgcppCard);
  GDREGISTER_CLASS(MtgcppDeck);
  GDREGISTER_CLASS(MtgcppDataPaths);
  GDREGISTER_CLASS(MtgcppCardDatabase);
  GDREGISTER_CLASS(MtgcppDeckRepository);
  GDREGISTER_CLASS(MtgcppBoardState);
  GDREGISTER_CLASS(MtgcppProfiles);
  GDREGISTER_CLASS(MtgcppProfileStore);
  GDREGISTER_CLASS(MtgcppDeckEditor);
  GDREGISTER_CLASS(MtgcppImportPreview);
  GDREGISTER_CLASS(MtgcppArena);
  GDREGISTER_CLASS(MtgcppSession);
}

void uninitialize_mtg_cpp_module(godot::ModuleInitializationLevel p_level) {
  if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
}

} // namespace

extern "C" {

GDExtensionBool GDE_EXPORT mtg_cpp_godot_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                              GDExtensionClassLibraryPtr p_library,
                                              GDExtensionInitialization *r_initialization) {
  godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
  init_obj.register_initializer(initialize_mtg_cpp_module);
  init_obj.register_terminator(uninitialize_mtg_cpp_module);
  init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
  return init_obj.init();
}

} // extern "C"
