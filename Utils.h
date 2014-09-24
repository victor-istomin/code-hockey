#pragma once 
#define PI 3.14159265358979323846

struct Point
{
	double x;
	double y;

	Point(double xp = 0, double yp = 0) : x(xp), y(yp)   {}
};

struct FirePosition
{
	Point m_pos;
	int   m_distance; 
	int   m_enemyPenalty;    //! TODO - penalty value, indicating near opponent

	FirePosition(const Point& p = Point(), int distance = 0x0FFFFFFF, int penalty = 0x0FFFFFFF) 
		: m_pos(p), m_distance(distance), m_enemyPenalty(penalty) 
	{}
};