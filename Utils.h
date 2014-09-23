#pragma once 
#define PI 3.14159265358979323846

struct Point
{
	double x;
	double y;

	Point(double xp = 0, double yp = 0) : x(xp), y(yp)   {}
};