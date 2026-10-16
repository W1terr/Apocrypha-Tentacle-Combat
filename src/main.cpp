#include "Forms.h"
#include "Hooks.h"
#include "Settings.h"
#include "Tentacles.h"

namespace
{
	constexpr std::uint32_t kSaveID = 'TCMB';

	void OnSave(SKSE::SerializationInterface* a_intfc) { Tentacles::Save(a_intfc); }

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t type, version, length;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == 'DEAD') {
				Tentacles::Load(a_intfc, version, length);
			}
		}
	}

	void OnRevert(SKSE::SerializationInterface*) { Tentacles::Revert(); }

	bool ready = false;

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Settings::Load();
			if (Forms::Load()) {
				Tentacles::Init();
				Hooks::Install();
				ready = true;
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Tentacles::Reset();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			if (ready) {
				Tentacles::Reset();
				Hooks::WatchPlayer();
			}
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logs::info("Apocrypha Tentacle Combat loading");
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	if (auto* serialization = SKSE::GetSerializationInterface(); serialization) {
		serialization->SetUniqueID(kSaveID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}
	return true;
}
