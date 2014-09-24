#include "Statistics.h"
#include <cassert>

std::unique_ptr<Statistics> Statistics::m_instance;
void Statistics::init(const Range& subsituteRange, Statistics::Side mySide)
{
	assert(!m_instance && "Can't reinitialize");
	m_instance.reset(new Statistics(subsituteRange, mySide));
}



