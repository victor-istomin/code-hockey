#pragma once 
#define PI 3.14159265358979323846
#include <algorithm>
#include <cmath>
#include <string>

#ifdef _DEBUG
#  define USE_LOG
#endif

#ifdef USE_LOG
#	include <windows.h>
    inline void debugPrint(const std::string& s)		            {::OutputDebugStringA((s+"\n").c_str());}
    template <typename T> std::string toString(const T& var)        {return std::to_string(var);}

#else
#	define debugPrint(x) ;
    template <typename T> std::string toString(const T& var)        {static const std::string fake; return fake;}
#endif // USE_LOG

inline double toDegrees(double radian)                          { return radian * 180.0 / PI; }
inline double toVectorSpeed(double vx, double vy)               { return std::sqrt(vx * vx + vy * vy); };

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
	int   m_penalty;    //! TODO - penalty value, indicating near opponent

	FirePosition(const Point& p = Point(), int distance = 0x0FFFFFFF, int penalty = 0x0FFFFFFF) 
		: m_pos(p), m_distance(distance), m_penalty(penalty) 
	{}
};

enum class PreferredFire
{
	eUNKNOWN = 0,
	eUP,
	eDOWN,
};

template <typename Container, typename Predicate>
typename Container::const_iterator find_if(const Container& c, const Predicate& p)
{
	return std::find_if(std::begin(c), std::end(c), p);
}

template <typename Container, typename Predicate>
typename Container::const_pointer find_unit(const Container& c, const Predicate& p)
{
	auto found = find_if(c, p);
	return found != std::end(c) ? &(*found) : nullptr;
}
