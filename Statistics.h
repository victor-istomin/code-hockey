#pragma once
#include "Utils.h"
#include <memory>

class Statistics;
class Statistics
{
public:
	enum Side
	{
		eUNKNOWN = 0, 
		eLEFT_SIDE,
		eRIGHT_SIDE,
	};

private:
	Range m_subsituteRange;
	Side  m_mySide;

	static std::unique_ptr<Statistics> m_instance;

	Statistics(const Range& subsituteRange, Side mySide) : m_subsituteRange(subsituteRange), m_mySide(mySide) {}
	
public:
	
	static Statistics*  instance()                    { return m_instance.get(); }
	static void init(const Range& subsituteRange, Statistics::Side mySide);

	Side         getMySide()            const { return m_mySide; }
	const Range& getSubstitutionRange() const { return m_subsituteRange; }
};


