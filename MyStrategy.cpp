#include "MyStrategy.h"
#include "Statistics.h"
#define _USE_MATH_DEFINES

#include <cmath>
#include <cassert>
#include <cstdlib>
#include <algorithm>

/**/
#ifdef _DEBUG
#include <windows.h>
#undef min
#undef max
#endif
/**/

using namespace model;

const double MyStrategy::STRIKE_ANGLE        = PI / 180.0;
long long    MyStrategy::m_initialDefenderId = -1;

void MyStrategy::move(const Hockeyist& self, const World& world, const Game& game, Move& move) 
{
	// update service pointers and statistics
	update(&self, &world, &game, &move);
	updateStatistics();
	
	// perform actions
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

// =======================================================================================================

void MyStrategy::attackPuck()
{
	const Point puckPos = getEstimatedPuckPos();

	m_move->setSpeedUp(1.0);
	m_move->setTurn(m_self->getAngleTo(puckPos.x, puckPos.y));
	m_move->setAction(m_self->getState() == SWINGING ? ActionType::CANCEL_STRIKE : ActionType::TAKE_PUCK);
}

void MyStrategy::defendInitial()
{
	// look for defend point between attacker ghost and net corner,
	const Hockeyist* attacker = getPuckOwner();
	const Hockeyist* defender = find_unit(getHockeyists(), [](const Hockeyist& h){ return h.getId() == m_initialDefenderId;});

	if (!attacker)
	{
		// just go back at the round begin
		m_move->setSpeedUp(-0.5);
		return;
	}

	assert(attacker && !attacker->isTeammate() && defender && defender->isTeammate());

	TFirePositions positions = fillDefenderPositions(attacker, defender);
	bool isAlreadyDefending = std::find_if(positions.begin(), positions.end(), 
		[defender](const FirePosition& p) {return defender->getDistanceTo(p.m_pos.x, p.m_pos.y) < defender->getRadius();} ) != positions.end();

	if (isAlreadyDefending)
	{
		// go to attacker and take puck
		const Puck& puck = m_world->getPuck();
		m_move->setTurn(defender->getAngleTo(puck));

		static const double kSpeedupArea  = m_game->getStickLength();
		static const double kSlowdownArea = std::min(defender->getRadius()*2, kSpeedupArea / 2);
		double distanceToPuck = defender->getDistanceTo(puck);

		if (distanceToPuck > kSpeedupArea)
			m_move->setSpeedUp(1.0);
		else if (distanceToPuck < kSlowdownArea)
			m_move->setSpeedUp(-1.0);
		else
			m_move->setSpeedUp(0);

		m_move->setAction(TAKE_PUCK);
	}
	else
	{
		// go to defend point
		std::sort(positions.begin(), positions.end(), [](const FirePosition& a, const FirePosition& b) { return a.m_distance + a.m_penalty < b.m_distance + b.m_penalty;});
		Point pos = positions.empty() ? Point(defender->getX(), defender->getY()) : positions.front().m_pos;

		double angleTo = defender->getAngleTo(pos.x, pos.y);
		bool shouldGoBack = (Statistics::instance()->getMySide() == Statistics::eLEFT_SIDE) ? pos.x < defender->getX() : pos.x > defender->getRadius();
		if(shouldGoBack)
		{
			m_move->setSpeedUp(-1.0);  // go straight backward
			m_move->setAction(TAKE_PUCK);
		}
		else
		{
			m_move->setTurn(angleTo);
			m_move->setSpeedUp(1.0); 
			m_move->setAction(TAKE_PUCK);
		}
	}
}


void MyStrategy::attackNet()
{
	if (m_self->getState() == HockeyistState::SWINGING)
	{
		if(m_self->getRemainingCooldownTicks() == 0)
		{
			m_move->setAction(ActionType::STRIKE);
			Statistics::instance()->getPlayer().attack();

			/**/
			debugPrint( " !! strike: " + std::to_string(m_self->getId())
				+ std::to_string(m_self->getX()) + "," + std::to_string(m_self->getY()) + "; s " 
				+ std::to_string(m_self->getSpeedX()) + ", " + std::to_string(m_self->getSpeedY()));
			/**/
		}
		else
		{
			/**/
			debugPrint(" .. cooldown");
			/**/
		}
		return;
	}

	const Point net       = getNet(m_world->getOpponentPlayer(), *m_self);
	const Point firePoint = getFirePoint();

	// TODO - variable [0, 10, 20] strike time?
	unsigned strikeTime = static_cast<unsigned>(m_game->getSwingActionCooldownTicks() + abs(m_self->getAngleTo(net.x, net.y) / m_game->getHockeyistTurnAngleFactor()));
	const Hockeyist ghost = getGhost(*m_self, strikeTime);

	double angleToNet       = ghost.getAngleTo(net.x, net.y);
	double angleToFirePoint = m_self->getAngleTo(firePoint.x, firePoint.y);
	double distanceToFire   = m_self->getDistanceTo(firePoint.x, firePoint.y);

	if (distanceToFire < m_self->getRadius() + m_game->getStickLength()) // start aiming a bit before fire point
	{

		m_move->setTurn(angleToNet);
		m_move->setSpeedUp(1.0);

		// swing and fire
		if (std::abs(angleToNet) < STRIKE_ANGLE) 
		{
			m_move->setAction(ActionType::SWING);

			/**/
			debugPrint( " ?? strike prediction: " + std::to_string(m_self->getId()) 
				+ "g: " + std::to_string(ghost.getX()) + "," + std::to_string(ghost.getY()) 
				+ "; s: " + std::to_string(m_self->getX()) + "," + std::to_string(m_self->getY())
				+ "; v: " + std::to_string(ghost.getSpeedX()) + ", " + std::to_string(ghost.getSpeedY()) 
				+ "; dt: " + std::to_string(strikeTime));
			/**/
		}
	}
	else
	{
		// move to fire point. TODO: obstacles?
		m_move->setTurn(angleToFirePoint);
		m_move->setSpeedUp(1.0);
	}
}

void MyStrategy::haveRest()
{
	Point exit = getSubstitutionPoint();
	
	static const double substitutionDistance = m_game->getSubstitutionAreaHeight();
	static const double fastRunDistance = substitutionDistance * 3;
	const double        currentDistance = m_self->getDistanceTo(exit.x, exit.y);
	if (currentDistance > substitutionDistance)
	{
		m_move->setTurn(m_self->getAngleTo(exit.x, exit.y));
		m_move->setSpeedUp(currentDistance > fastRunDistance ? 1.0 : 0.2);
	}
	else
	{
		double angleToCamera = m_self->getAngleTo(m_self->getX(), m_game->getRinkBottom());
		if (abs(angleToCamera) > STRIKE_ANGLE)
		{
			m_move->setTurn(angleToCamera);
		}
	}
	
	// TODO - substitute support
}

// =====================================================================================

MyStrategy::TActionPtr MyStrategy::getCurrentAction()
{
	if (isRestTime())
		return &MyStrategy::haveRest;

	// is it time to start initial defend on sub-round begin?
	const PuckStatistics& puckStatistics = Statistics::instance()->getPuck();
	if (puckStatistics.m_isFirstCatch)
	{
		findInitialDefender();
		if (puckStatistics.m_lastPlayerId != m_world->getMyPlayer().getId())
		{
			debugPrint( "Tick: " + std::to_string(m_world->getTick()) + " initial defend needed" );

			if (m_self->getId() == m_initialDefenderId)
				return &MyStrategy::defendInitial;
		}
		else if(m_initialDefenderId != -1)
		{
			debugPrint( "Tick: " + std::to_string(m_world->getTick()) + " initial defend finished" );
			m_initialDefenderId = -1;
		}
	}
	
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

// ======================================================================================

Point MyStrategy::getNet(const Player& player, const Hockeyist& attacker) const
{
	double netX = (player.getNetBack() + player.getNetFront()) / 2;
	double netY = (player.getNetBottom() + player.getNetTop()) / 2;
	netY += (attacker.getY() < netY ? 0.5 : -0.5) * m_game->getGoalNetHeight();  // attack far corner

	return Point(netX, netY);
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
	
	bool isGoalkeeperPresent = find_unit(hockeyists, [](const Hockeyist& h){ return !h.isTeammate() && h.getType() == GOALIE;} ) != nullptr;
	if (!isGoalkeeperPresent) 
	{
		// if no goalkeeper present - fire from any position
		return Point(m_self->getX(), m_self->getY());
	}

	// find 45 degree line for fire from
	TFirePositions positions = fillFirePositions();

	// sort positions by < (distance+penalty)
	std::sort( begin(positions), end(positions), 
		[](const FirePosition& a, const FirePosition& b) {return a.m_distance + a.m_penalty < b.m_distance + b.m_penalty;} );

	Point fire = positions.empty() ? Point(m_self->getX(), m_self->getY()) : positions.front().m_pos;

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
	const Hockeyist* goalkeeper = find_unit(hockeists, [](const Hockeyist& h){ return !h.isTeammate() && h.getType() == GOALIE;});

	positions.reserve(std::min(bottom - top, width) / unitRadius * 2);

	Point goal = getNet(m_world->getOpponentPlayer(), *m_self);
	int    xDirection = m_world->getMyPlayer().getNetFront() > m_world->getOpponentPlayer().getNetFront() ? 1 /*I'm at right*/ : -1/*I'm at left*/;
	int    yDirection = m_self->getY() > goal.y ? 1 : -1;
	double yThreshold = yDirection > 0 ? bottom - unitRadius : top + unitRadius;
    double yMargin    = goalkeeper ? yDirection * unitRadius * 4 : yDirection * unitRadius * 2;   // don't go too close to goalkeeper.

    auto isBottomCrossed = [yThreshold](double y){return y > yThreshold;};
    auto isTopCrossed    = [yThreshold](double y){return y < yThreshold;};

	for (double y = goal.y + yMargin; yDirection > 0 ? !isBottomCrossed(y) : !isTopCrossed(y); y += yDirection * unitRadius / 2.0)
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
				dangerFactor = 1;
			}

			const double dangerRadius = unitRadius + m_game->getStickLength();
			double danger = dangerRadius / hockeist.getDistanceTo(x, y);
			danger *= danger;  // TODO - is this needed?
			
			penalty += static_cast<int>(danger * dangerFactor * m_game->getStickLength());
		}

		positions.push_back(FirePosition(Point(x, y), static_cast<int>(m_self->getDistanceTo(x, y)), penalty));
	}

	return positions;
}


MyStrategy::TFirePositions MyStrategy::fillDefenderPositions(const model::Hockeyist* attacker, const model::Hockeyist* defender) const
{
	TFirePositions positions;
	int top        = static_cast<int>(m_game->getRinkTop());
	int bottom     = static_cast<int>(m_game->getRinkBottom());
	int width      = static_cast<int>(m_game->getWorldWidth());
	int unitRadius = static_cast<int>(m_self->getRadius());

	const auto& hockeists = getHockeyists();
	const Hockeyist* goalkeeper = find_unit(hockeists, [](const Hockeyist& h){ return h.isTeammate() && h.getType() == GOALIE;});
	const Puck*      puck       = &m_world->getPuck();

	const double centerY = (m_game->getRinkTop() + m_game->getRinkBottom()) / 2;
	if (abs(attacker->getY() - centerY) < 5)
	{
		// attacker is not decided yet, just go back to goalie
		return TFirePositions(1, FirePosition(Point((defender->getX() + goalkeeper->getX())/2, defender->getY()), 0, 0));
	}

	positions.reserve(std::min(bottom - top, width) / unitRadius * 2);

	Point goal = getNet(m_world->getMyPlayer(), *attacker);
	int   xDirection = m_world->getMyPlayer().getNetFront() > m_world->getOpponentPlayer().getNetFront() ? -1 /*I'm at right*/ : 1/*I'm at left*/;
	int   yDirection = attacker->getY() > goal.y ? 1 : -1;
	double yMargin    = goalkeeper ? yDirection * unitRadius * 4 : yDirection * unitRadius * 2;   // don't go too close to goalkeeper.
	double yThreshold = yDirection > 0 ? bottom - unitRadius : top + unitRadius;

	auto isBottomCrossed = [yThreshold](double y){return y > yThreshold;};
	auto isTopCrossed    = [yThreshold](double y){return y < yThreshold;};

	for (double y = goal.y + yMargin; yDirection > 0 ? !isBottomCrossed(y) : !isTopCrossed(y); y += yDirection * unitRadius / 2.0)
	{
		double delta   = abs(y - goal.y);
		double x       = goal.x + delta * xDirection;
		int    penalty = static_cast<int>(toDegrees(std::abs(defender->getAngleTo(x, y))) * std::max(0.5, std::abs(defender->getAngularSpeed())) / 2);

		// TODO

		if (isInBetween(Point(x,y), *defender, *attacker, puck->getRadius()))
			continue; 		// don't add secured positions (behind defender)

		positions.push_back(FirePosition(Point(x, y), static_cast<int>(m_self->getDistanceTo(x, y)), penalty));
	}

	return positions;
}


void MyStrategy::updateStatistics()
{
	const Player& me = m_world->getMyPlayer();

	// init statistics
	if (!Statistics::instance())
	{
		Point topLeftRink       = Point(m_game->getRinkLeft(),  m_game->getRinkTop());
		Point bottomRightRink   = Point(m_game->getRinkRight(), m_game->getRinkTop() + m_game->getSubstitutionAreaHeight());
		Statistics::Side mySide = Statistics::eUNKNOWN;

		if (me.getNetFront() < m_world->getOpponentPlayer().getNetFront())
		{
			// my side is on the left
			mySide = Statistics::eLEFT_SIDE;
			bottomRightRink.x /= 2.0;
		}
		else
		{
		    mySide = Statistics::eRIGHT_SIDE;
			topLeftRink.x = bottomRightRink.x / 2.0;
		}
		
		Statistics::init(Range(topLeftRink, bottomRightRink), mySide, m_world->getMyPlayer().getName());
	}

	// update player statistics
	Statistics::instance()->getPlayer().update(me.getGoalCount(), m_world->getOpponentPlayer().getGoalCount(), me.isStrategyCrashed());

	// update puck statistics
	PuckStatistics& puckStatistics = Statistics::instance()->getPuck();
	if (isRestTime() && !puckStatistics.m_isJustReset)
	{
		puckStatistics.reset();
		m_initialDefenderId = -1;
	}
	else
	{
		long long puckPlayerId = m_world->getPuck().getOwnerPlayerId();
		if (puckStatistics.m_lastPlayerId != puckPlayerId)
		{
			puckStatistics.m_isFirstCatch = puckStatistics.m_isJustReset;
			puckStatistics.m_lastPlayerId = puckPlayerId;
			puckStatistics.m_isJustReset  = false;
		}
	}
}

Point MyStrategy::getSubstitutionPoint() const
{
	Point                  result      = Point(m_self->getX(), m_self->getY());
	const Range            targetRange = Statistics::instance()->getSubstitutionRange();
	const Statistics::Side mySide      = Statistics::instance()->getMySide();
	
	if (targetRange.isPointInside(result))
		return result;
	
	result.y = m_game->getRinkTop() + m_self->getRadius();
	if (!targetRange.isXInside(result.x))
	{
		result.x = mySide == Statistics::eLEFT_SIDE 
		             ? targetRange.m_rightBottom.x - m_self->getRadius()
					 : targetRange.m_topLeft.x     + m_self->getRadius();
	}
	
	return result;
}

model::Hockeyist MyStrategy::getGhost(const model::Hockeyist& from, unsigned ticksIncrement)
{
	static const double kFrictionLoses = 0.95;
	const double speedLose = pow(kFrictionLoses, ticksIncrement);

	double x = from.getX() + from.getSpeedX() * ticksIncrement * kFrictionLoses;
	double y = from.getY() + from.getSpeedY() * ticksIncrement * kFrictionLoses;
	return Hockeyist(0, 0, 0, 0, from.getRadius(),  x, y, from.getSpeedX() * speedLose, from.getSpeedY() * speedLose, from.getAngle(), from.getAngularSpeed(), 
		from.isTeammate(), from.getType(), 0, 0, 0, 0, 0, from.getState(), 0, 0, 0, 0, from.getLastAction(), from.getLastActionTick());
}

void MyStrategy::findInitialDefender()
{
	if (m_initialDefenderId != -1)
		return; // already set, no change allowed

	const Hockeyist* puckOwner       = getPuckOwner();
	const Hockeyist* nearestTeammate = nullptr;
	const Point      net             = getNet(m_world->getMyPlayer(), puckOwner ? *puckOwner : *m_self);

	for (const Hockeyist& h: m_world->getHockeyists())
	{
		if (!h.isTeammate() || h.getType() == GOALIE)
			continue;

		if (!nearestTeammate || h.getDistanceTo(net.x, net.y) < nearestTeammate->getDistanceTo(net.x, net.y))
			nearestTeammate = &h;
	}

	assert(nearestTeammate && "should be found!");
	m_initialDefenderId = nearestTeammate ? nearestTeammate->getId() : -1;
}

const model::Hockeyist* MyStrategy::getPuckOwner() const
{
	long long ownerUnitId = m_world->getPuck().getOwnerHockeyistId();
	return find_unit(getHockeyists(), [ownerUnitId](const Hockeyist& h) {return h.getId() == ownerUnitId;});
}

bool MyStrategy::isInBetween(const Point& first, const model::Unit& inBetween, const model::Unit& second, double gap) const
{
	double distanceToFirst   = second.getDistanceTo(first.x, first.y);
	double distanceToBetween = second.getDistanceTo(inBetween);
	if (distanceToBetween >= distanceToFirst)
		return false;

	/*             . second
	              /
			     /
	            / 
	           / \ dL 
	          /   \
		     /     'inBetween 
            /
	  first'    

	  return dL < gap;
	*/
	double dA = std::abs(second.getAngleTo(first.x, first.y) - second.getAngleTo(inBetween));
	double dL  = std::tan(dA) * distanceToBetween;
	return dL < gap;
}
