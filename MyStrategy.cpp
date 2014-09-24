#include "MyStrategy.h"
#define _USE_MATH_DEFINES

#include <cmath>
#include <cstdlib>
#include <algorithm>

using namespace model;

const double MyStrategy::STRIKE_ANGLE = PI / 180;

void MyStrategy::move(const Hockeyist& self, const World& world, const Game& game, Move& move) 
{
	update(&self, &world, &game, &move);

	TActionPtr action = getCurrentAction();
	(this->*action)();

	update(nullptr, nullptr, nullptr, nullptr);
}

MyStrategy::MyStrategy() 
	: m_self(nullptr)
	, m_world(nullptr)
	, m_game(nullptr)
	, m_move(nullptr)
{ 
}

MyStrategy::~MyStrategy()
{
}


void MyStrategy::attackPuck()
{
	const Point puckPos = getEstimatedPuckPos();

	m_move->setSpeedUp(1.0);
	m_move->setTurn(m_self->getAngleTo(puckPos.x, puckPos.y));
	m_move->setAction(m_self->getState() == SWINGING ? ActionType::CANCEL_STRIKE : ActionType::TAKE_PUCK);
}


void MyStrategy::attackNet()
{
	if(m_self->getState() == HockeyistState::SWINGING)
	{
		m_move->setAction(ActionType::STRIKE);
		return;
	}

	const Point net       = getOpponentNet();
	const Point firePoint = getFirePoint();

	double angleToNet       = m_self->getAngleTo(net.x, net.y);
	double angleToFirePoint = m_self->getAngleTo(firePoint.x, firePoint.y);
	double distanceToFire   = m_self->getDistanceTo(firePoint.x, firePoint.y);

	if (distanceToFire < m_self->getRadius() + m_game->getStickLength()) // start aiming a bit before fire point
	{
		// swing and fire

		m_move->setTurn(angleToNet);
		m_move->setSpeedUp(1.0);

		if (abs(angleToNet) < STRIKE_ANGLE) 
		{
			m_move->setAction(ActionType::SWING);
		}
	}
	else
	{
		// move to fire point. TODO: obstacles?
		m_move->setTurn(angleToFirePoint);
		m_move->setSpeedUp(1.0);
	}
}

MyStrategy::TActionPtr MyStrategy::getCurrentAction()
{
	if (m_world->getPuck().getOwnerPlayerId() == m_world->getMyPlayer().getId())
	{
		if (m_world->getPuck().getOwnerHockeyistId() == m_self->getId())
		{
			return &MyStrategy::attackNet;
		}
		else
		{
			return &MyStrategy::defendTeammate;
		}
	}

	return &MyStrategy::attackPuck;
}

Point MyStrategy::getOpponentNet() const
{
	const Player& opponent = m_world->getOpponentPlayer();

	double netX = (opponent.getNetBack() + opponent.getNetFront()) / 2;
	double netY = (opponent.getNetBottom() + opponent.getNetTop()) / 2;
	netY += (m_self->getY() < netY ? 0.5 : -0.5) * m_game->getGoalNetHeight();  // attack far corner

	Point net = Point(netX, netY);
	return net;
}

void MyStrategy::defendTeammate()
{
	const Hockeyist* nearest = getNearestOpponent();
	if ( nearest == nullptr )
		return;

	double angleToNearest = m_self->getAngleTo(*nearest); 
	bool   isCanStrike    = m_self->getDistanceTo(*nearest) < m_game->getStickLength() 
		                 && m_self->getRemainingCooldownTicks() == 0
		                 && angleToNearest < (m_game->getStickSector() / 2.0);

	if (m_self->getState() == HockeyistState::SWINGING)
	{
		if (isCanStrike)
			m_move->setAction(ActionType::STRIKE);
		else
			m_move->setAction(ActionType::CANCEL_STRIKE);

		return;
	}
	else
	{
		if (isCanStrike)
		{
			m_move->setAction(ActionType::SWING);
		}
		else
		{
			m_move->setSpeedUp(1.0);
			m_move->setTurn(angleToNearest);
		}
	}
}

 const Hockeyist* MyStrategy::getNearestOpponent() const
{
	const Hockeyist* nearest = nullptr;
	for (const Hockeyist& hockeist: getHockeyists())
	{
		if (hockeist.isTeammate() || hockeist.getState() == HockeyistState::RESTING)
			continue;

		if (nearest == nullptr 
			|| m_self->getDistanceTo(hockeist.getX(), hockeist.getY()) < m_self->getDistanceTo(nearest->getX(), nearest->getY()))
		{
			nearest = &hockeist;
		}
	}

	return nearest;
}

Point MyStrategy::getEstimatedPuckPos() const
{
	const Puck& puck = m_world->getPuck();
	Point result = Point(puck.getX(), puck.getY());

	double distance = m_self->getDistanceTo(puck);
	double xSpeed   = m_self->getSpeedX() - puck.getSpeedX();
	double ySpeed   = m_self->getSpeedY() - puck.getSpeedY();
	
	if(xSpeed < 0 || ySpeed < 0)
	{
		// TODO - implement predict when loosing puck
	}
	else if (distance > m_game->getStickLength() * 2)
	{
		double speed = sqrt(xSpeed*xSpeed + ySpeed*ySpeed);
		double time  = speed > 0.1 ? distance / speed : 0;

		double predictX = puck.getX() + time * puck.getSpeedX() / 2.0;  // TODO: friction
		double predictY = puck.getY() + time * puck.getSpeedY() / 2.0;  // TODO: friction

		if (predictX > 0 && predictX < m_world->getWidth() && predictY > 0 && predictY < m_world->getHeight())
		{
			result = Point(predictX, predictY);
		}
	}

	return result;
}

Point MyStrategy::getFirePoint() const
{
	const auto& hockeyists = getHockeyists();
	bool isGoalkeeperPresent = std::find_if(begin(hockeyists), end(hockeyists), [](const Hockeyist& h){ return !h.isTeammate() && h.getType() == GOALIE;} ) 
		                           != end(hockeyists);

	if (!isGoalkeeperPresent) 
	{
		// if no goalkeeper present - fire from any position
		return Point(m_self->getX(), m_self->getY());
	}

	// find 45 degree line for fire from
	TFirePositions positions = fillFirePositions();

	// sort positions by < (distance+penalty)
	std::sort( begin(positions), end(positions), 
		[](const FirePosition& a, const FirePosition& b) {return a.m_distance + a.m_enemyPenalty < b.m_distance + b.m_enemyPenalty;} );

	Point fire = positions.empty() ? Point(m_self->getX(), m_self->getX()) : positions.front().m_pos;

	// don't go above top or bottom
	fire.y = std::max(fire.y, m_game->getRinkTop() + m_self->getRadius());       
	fire.y = std::min(fire.y, m_game->getRinkBottom() - m_self->getRadius());

	return fire;
}

MyStrategy::TFirePositions MyStrategy::fillFirePositions() const
{
	TFirePositions positions;
	int top        = static_cast<int>(m_game->getRinkTop());
	int bottom     = static_cast<int>(m_game->getRinkBottom());
	int width      = static_cast<int>(m_game->getWorldWidth());
	int unitRadius = static_cast<int>(m_self->getRadius());

	const auto& hockeists = getHockeyists();

	positions.reserve(std::min(bottom - top, width) / unitRadius * 2);

	const double dangerRadius = m_self->getRadius() + m_game->getStickLength();

	Point goal = getOpponentNet();
	int   xDirection = m_world->getMyPlayer().getNetFront() > m_world->getOpponentPlayer().getNetFront() ? 1 /*I'm at right*/ : -1/*I'm at left*/;
	int   yDirection = m_self->getY() > goal.y ? unitRadius : -unitRadius;
	int   yThreshold = yDirection > 0 ? bottom : top;

	for (double y = goal.y + yDirection; abs(yThreshold-y) > abs(yDirection); y += yDirection / 2.0)
	{
		double delta   = abs(y - goal.y);
		double x       = goal.x + delta * xDirection;
		int    penalty = 0;

		// calculate danger penalty: TODO: what if path to (x,y) is blocked?
		for (const Hockeyist& hockeist: hockeists)
		{
			double dangerFactor = 1;
			if(hockeist.isTeammate())
			{
				dangerFactor = hockeist.getId() == m_self->getId() ? -1 : -0.5;
			}
			else
			{
				dangerFactor = hockeist.getType() != HockeyistType::GOALIE ? 1 : 2;
			}

			double danger = dangerRadius / hockeist.getDistanceTo(x, y);
			danger *= danger;  // TODO - is this needed?
			
			penalty += static_cast<int>(danger * dangerFactor * m_game->getStickLength());
		}

		positions.push_back(FirePosition(Point(x, y), static_cast<int>(m_self->getDistanceTo(x, y)), penalty));
	}

	return positions;
}

