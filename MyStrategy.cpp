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

	const Point net = getOpponentNet();

	double angleToNet = m_self->getAngleTo(net.x, net.y);
	m_move->setTurn(angleToNet);
	m_move->setSpeedUp(1.0);

	if (abs(angleToNet) < STRIKE_ANGLE) 
	{
		m_move->setAction(ActionType::SWING);
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
	for (const Hockeyist& hockeist: m_world->getHockeyists())
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

