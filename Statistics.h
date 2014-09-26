#pragma once
#include "Utils.h"
#include <memory>
#include <string>
#include <functional>
#include <stack>

struct PlayerStatistics
{
	const std::string m_name;
	unsigned m_goalsMade;
	unsigned m_goalsGot;
	unsigned m_attacksCount;
	bool     m_isCrashed;

	PlayerStatistics(const std::string& n) : m_name(n), m_goalsMade(0), m_goalsGot(0), m_attacksCount(0), m_isCrashed(0)		{}
	void update(unsigned score, unsigned got, bool isCrashed)                     {m_goalsMade = score; m_goalsGot = got; m_isCrashed = isCrashed; }
	void attack()                                                                 {++m_attacksCount;}

	std::string toString();

private:
	PlayerStatistics(const PlayerStatistics&);            //!< denied
	PlayerStatistics& operator=(const PlayerStatistics&); //!< denied
};

struct PuckStatistics
{
	bool      m_isJustReset;
	bool      m_isFirstCatch;
	long long m_lastPlayerId;

	PuckStatistics() : m_isFirstCatch(true), m_isJustReset(true), m_lastPlayerId(-1)    {}
	void reset()                                                                        {*this = PuckStatistics();}
};

class Statistics
{
public:
	enum Side
	{
		eUNKNOWN = 0, 
		eLEFT_SIDE,
		eRIGHT_SIDE,
	};

	typedef long long              TId;
	typedef std::function<void()>  TPuckLooseCallback;

private:
	Range               m_subsituteRange;
	Side                m_mySide;
	PlayerStatistics    m_player;
	PuckStatistics      m_puck;

	std::stack<TPuckLooseCallback>     m_plCallbacks;
	static std::unique_ptr<Statistics> m_instance;

	Statistics(const Range& subsituteRange, Side mySide, const std::string& playerName ) 
		: m_subsituteRange(subsituteRange), m_mySide(mySide), m_player(playerName), m_puck()
	{}

	Statistics(const Statistics&); //!< denied
	
public:

	~Statistics();
	
	static Statistics*  instance()                    { return m_instance.get(); }
	static void init(const Range& subsituteRange, Statistics::Side mySide, const std::string& playerName);

	Side         getMySide()            const { return m_mySide; }
	const Range& getSubstitutionRange() const { return m_subsituteRange; }

	PlayerStatistics& getPlayer()             {return m_player;}
	PuckStatistics&   getPuck()               {return m_puck;}

	void registerOnPuckLoose(TPuckLooseCallback&& c)	{m_plCallbacks.push(std::move(c));}
	void onPuckLoose();
};


