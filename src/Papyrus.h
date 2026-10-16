#pragma once

// Calls a vanilla Papyrus native (here ObjectReference.ProcessTrapHit) through the script VM.
// The call is queued on the VM and runs a moment later; nothing waits for the result.
// The argument object is not freed here, same as CommonLib's own SendEvent (who owns it is not known).
namespace Papyrus
{
	template <class T, class... Args>
	bool CallMethod(T* a_object, const char* a_function, Args&&... a_args)
	{
		auto* const vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm || !a_object) {
			return false;
		}
		RE::BSScript::Variable self;
		RE::BSScript::PackValue(&self, a_object);  // finds or creates + binds the script object of the form
		auto object = self.GetObject();
		if (!object) {
			logs::warn("Papyrus: no script object for {} ({})", a_function, static_cast<RE::TESForm*>(a_object)->GetFormID());
			return false;
		}
		auto* const args = RE::MakeFunctionArguments(static_cast<std::decay_t<Args>>(a_args)...);
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		const bool ok = vm->DispatchMethodCall(object, a_function, args, callback);
		if (!ok) {
			logs::warn("Papyrus: {} could not be called", a_function);
		}
		return ok;
	}
}
