#include "Statistics.h"
#include <cassert>
#include <string>
#include <ctime>
#include <fstream>

std::unique_ptr<Statistics> Statistics::m_instance;
void Statistics::init(const Range& subsituteRange, Statistics::Side mySide, const std::string& playerName)
{
	assert(!m_instance && "Can't reinitialize");
	m_instance.reset(new Statistics(subsituteRange, mySide, playerName));
}

Statistics::~Statistics()
{
#ifdef _DEBUG
	// unload statistics to file
	const std::string filename = "stats_" + std::to_string(time(nullptr)) + "_" + std::to_string(clock()) + ".log";

	std::ofstream out = std::ofstream(filename, std::ios::trunc);
	out << "Finished. Side: " << (m_mySide == eLEFT_SIDE ? "left" : "right") << std::endl
		<< "Player: " << m_player.toString() << std::endl;
#endif
}

std::string PlayerStatistics::toString()
{
#ifdef _DEBUG
	return m_name + "; score: " + std::to_string(m_goalsMade) + "; got: " + std::to_string(m_goalsGot) 
	              + "; attacks: " + std::to_string(m_attacksCount) + "(" + std::to_string(m_goalsMade * 100.0 / m_attacksCount)
				  + "%); crashed: " + std::string(m_isCrashed ? "true" : "false");
#else
	return std::string();
#endif
}
