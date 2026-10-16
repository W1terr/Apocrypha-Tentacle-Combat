#include "Settings.h"

#include <fstream>
#include <map>

namespace Settings
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/TentacleCombat.ini";

		Values values;

		std::string Trim(std::string a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return a_text.substr(first, last - first + 1);
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		using Table = std::map<std::string, std::string>;  // "section.key" (lower case) -> value

		Table Read()
		{
			Table         table;
			std::ifstream file(kPath);
			std::string   line;
			std::string   section;
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#') {
					continue;
				}
				if (line.front() == '[' && line.back() == ']') {
					section = Lower(Trim(line.substr(1, line.size() - 2)));
					continue;
				}
				const auto equals = line.find('=');
				if (equals == std::string::npos) {
					continue;
				}
				std::string value = Trim(line.substr(equals + 1));
				if (const auto comment = value.find(';'); comment != std::string::npos) {
					value = Trim(value.substr(0, comment));
				}
				table[section + "." + Lower(Trim(line.substr(0, equals)))] = value;
			}
			return table;
		}

		void Number(const Table& a_table, const char* a_key, float& a_value, float a_min, float a_max)
		{
			if (const auto it = a_table.find(a_key); it != a_table.end()) {
				try {
					a_value = std::clamp(std::stof(it->second), a_min, a_max);
				} catch (...) {
					logs::warn("INI: {} = '{}' is not a number", a_key, it->second);
				}
			}
		}

		void Flag(const Table& a_table, const char* a_key, bool& a_value)
		{
			if (const auto it = a_table.find(a_key); it != a_table.end()) {
				const auto v = Lower(it->second);
				a_value = v == "1" || v == "true" || v == "yes" || v == "on";
			}
		}
	}

	void Load()
	{
		const Table table = Read();
		Number(table, "tentacles.fengagedistance", values.engageDistance, 100.0f, 2000.0f);
		Number(table, "tentacles.fretreatdistance", values.retreatDistance, 200.0f, 4000.0f);
		values.retreatDistance = std::max(values.retreatDistance, values.engageDistance + 100.0f);
		Number(table, "tentacles.fturnlimit", values.turnLimit, 0.0f, 180.0f);
		Number(table, "tentacles.fstrikepausemin", values.strikePauseMin, 0.0f, 10.0f);
		Number(table, "tentacles.fstrikepausemax", values.strikePauseMax, 0.0f, 10.0f);
		values.strikePauseMax = std::max(values.strikePauseMax, values.strikePauseMin);
		Number(table, "tentacles.fdamagemult", values.damageMult, 0.0f, 10.0f);
		Flag(table, "tentacles.bdisease", values.disease);
		Flag(table, "debug.bverboselog", values.verboseLog);
		logs::info("Settings: engage {:.0f}, retreat {:.0f}, turn {:.0f} deg, pause {:.1f}-{:.1f} s, damage x{:.2f}, disease {}",
			values.engageDistance, values.retreatDistance, values.turnLimit, values.strikePauseMin, values.strikePauseMax,
			values.damageMult, values.disease);
	}

	const Values& Get() { return values; }
}
