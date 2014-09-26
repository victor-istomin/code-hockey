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
	const std::string filename = "stats_" + toString(time(nullptr)) + "_" + toString(clock()) + ".log";

	std::ofstream out = std::ofstream(filename, std::ios::trunc);
	out << "Finished. Side: " << (m_mySide == eLEFT_SIDE ? "left" : "right") << std::endl
		<< "Player: " << m_player.toString() << std::endl;
#endif
}

void Statistics::onPuckLoose()
{
	while (!m_plCallbacks.empty())
	{
		TPuckLooseCallback callback = std::move(m_plCallbacks.top());
		m_plCallbacks.pop();
		callback();
	}
}

std::string PlayerStatistics::toString()
{
#ifdef _DEBUG
	return m_name + "; score: " + ::toString(m_goalsMade) + "; got: " + ::toString(m_goalsGot) 
	              + "; attacks: " + ::toString(m_attacksCount) + "(" + ::toString(m_goalsMade * 100.0 / m_attacksCount)
				  + "%); crashed: " + std::string(m_isCrashed ? "true" : "false");
#else
	return std::string();
#endif
}
