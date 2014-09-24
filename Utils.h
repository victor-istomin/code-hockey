#pragma once 
#define PI 3.14159265358979323846

struct Point
{
	double x;
	double y;

	Point(double xp = 0, double yp = 0) : x(xp), y(yp)   {}
};

struct Range
{
	Point m_topLeft;
	Point m_rightBottom;
	
	Range(const Point& tl, const Point& rb) : m_topLeft(tl), m_rightBottom(rb) {}
	
	bool isPointInside(const Point& p) const { return isXInside(p.x) && isYInside(p.y); }
	bool isXInside(double x)           const { return x >= m_topLeft.x && x < m_rightBottom.x; }
	bool isYInside(double y)           const { return y >= m_topLeft.y && y < m_rightBottom.y; }
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

