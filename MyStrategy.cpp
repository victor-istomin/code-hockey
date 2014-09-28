#include "MyStrategy.h"
#include "Statistics.h"
#define _USE_MATH_DEFINES

#include <cmath>
#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <set>

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
std::map<MyStrategy::TId, PreferredFire> MyStrategy::m_firePositionMap;

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
	m_move->setAction(m_self->getState() == SWINGING ? CANCEL_STRIKE : TAKE_PUCK);
	
	improveManeuverability(); // TODO - check me!
	
	// if can't get reach puck, but can reach opponent - feel free to punch him if there is no teammate in between
	// TODO - check this
	const Puck&      puck       = m_world->getPuck();
	const TId        ownerId    = puck.getOwnerHockeyistId(); 
	const Hockeyist* puckOwner  = find_unit(getHockeyists(), [ownerId](const Hockeyist& h){return !h.isTeammate() && h.getId() == ownerId;});
	if (!puckOwner)
		return;
	
	const TId        attackerId = m_self->getId();
	const Hockeyist* attacker   = m_self;
	const Hockeyist* teammateBetween = find_unit(getHockeyists(), [attacker, puckOwner, this/*bug*/](const Hockeyist& h) 
	{
		return h.isTeammate() && MyStrategy::isInBetween(Point(attacker->getX(), attacker->getY()), h, *puckOwner, attacker->getRadius());
	});
	const Hockeyist* enemyBetween = find_unit(getHockeyists(), [attacker, puckOwner, this/*bug*/](const Hockeyist& h) 
	{
		return !h.isTeammate() && MyStrategy::isInBetween(Point(attacker->getX(), attacker->getY()), h, *puckOwner, attacker->getRadius());
	});
	
	if (enemyBetween != nullptr)
		teammateBetween = nullptr; // don't miss a chance to hit two enemies
	
	static std::set<TId> swingingOnEnemySet;
	if ( !teammateBetween && m_self->getDistanceTo(puck) > m_game->getStickLength() && m_self->getDistanceTo(*puckOwner) < m_game->getStickLength()
	  && std::abs(m_self->getAngleTo(*puckOwner)) < m_game->getStickSector() / 2)
	{
		if (swingingOnEnemySet.find(attackerId) != swingingOnEnemySet.end() || puckOwner->getState() == SWINGING)
		{
			swingingOnEnemySet.erase(attackerId);
			m_move->setAction(STRIKE);
		}			
		else
		{
			swingingOnEnemySet.insert(attackerId);
			m_move->setAction(SWING);
		}
	}
	else
	{
		swingingOnEnemySet.erase(attackerId);
	}
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
			m_move->setSpeedUp(0.5);
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
			debugPrint( " !! strike: " + toString(m_self->getId())
				+ toString(m_self->getX()) + "," + toString(m_self->getY()) + "; s " 
				+ toString(m_self->getSpeedX()) + ", " + toString(m_self->getSpeedY()));
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

	if (m_firePositionMap[m_self->getId()] == PreferredFire::eUNKNOWN)
	{
		const Player& me         = m_world->getMyPlayer();
		const Player& opponent   = m_world->getOpponentPlayer();
		const double  center     = (m_game->getRinkBottom() + m_game->getRinkTop()) / 2;
		const double  upQuater   = (m_game->getRinkTop()    + center) / 2;
		const double  downQuater = (m_game->getRinkBottom() + center) / 2;

		double theirOrMinePart = m_self->getDistanceTo(opponent.getNetFront(), center) / m_self->getDistanceTo(me.getNetFront(), center);
		static const double kFarFromOpponentFactor = 2;
		if (theirOrMinePart > kFarFromOpponentFactor)
		{
			// how much is present of opponent in each half?
			double upScore   = 0;
			double downScore = 0;

			for(const Hockeyist& h: getHockeyists())
			{
				if(h.getType() == GOALIE || h.isTeammate())
					continue;

				upScore   += std::abs(upQuater   - h.getY());
				downScore += std::abs(downQuater - h.getY());
			}

			upScore   -= std::abs(upQuater   - m_self->getY());
			downScore -= std::abs(downQuater - m_self->getY());

			// choose half with less enemies score
			static const double k_minFactor = (m_game->getRinkBottom() - m_game->getRinkTop()) / 1.5;
			double quatersFactor = std::abs(upScore - downScore);
			if (quatersFactor > k_minFactor)
			{
				m_firePositionMap[m_self->getId()] = upScore < downScore ? PreferredFire::eDOWN : PreferredFire::eUP;
				Statistics::instance()->registerOnPuckLoose( [](){m_firePositionMap.clear();} );
			}
		}
	}

	const Point net       = getNet(m_world->getOpponentPlayer(), *m_self);
	const Point firePoint = getFirePoint();

	// TODO - variable [0, 10, 20] strike time?
	unsigned strikeTime = static_cast<unsigned>(m_game->getSwingActionCooldownTicks() + abs(m_self->getAngleTo(net.x, net.y) / m_game->getHockeyistTurnAngleFactor()));
	const Hockeyist ghost = getGhost(*m_self, strikeTime, m_self->getAngle());

	double angleToNet       = ghost.getAngleTo(net.x, net.y);
	double angleToFirePoint = m_self->getAngleTo(firePoint.x, firePoint.y);
	double distanceToFire   = ghost.getDistanceTo(firePoint.x, firePoint.y);

	if (distanceToFire < m_self->getRadius() * 2) // start aiming a bit before fire point
	{
		m_move->setTurn(angleToNet);
		m_move->setSpeedUp(1.0);

		// swing and fire
		if (std::abs(angleToNet) < STRIKE_ANGLE) 
		{
			m_move->setAction(ActionType::SWING);

			/**/
			debugPrint( " ?? strike prediction: " + toString(m_self->getId()) 
				+ "g: " + toString(ghost.getX()) + "," + toString(ghost.getY()) 
				+ "; s: " + toString(m_self->getX()) + "," + toString(m_self->getY())
				+ "; v: " + toString(ghost.getSpeedX()) + ", " + toString(ghost.getSpeedY()) 
				+ "; dt: " + toString(strikeTime));
			/**/
		}
	}
	else
	{
		// move to fire point. TODO: obstacles?
		m_move->setTurn(angleToFirePoint);
		m_move->setSpeedUp(1.0);
		improveManeuverability(); // TODO - check me!
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
	
	improveManeuverability(); // TODO - check me!
	
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
			debugPrint( "Tick: " + toString(m_world->getTick()) + " initial defend needed" );

			if (m_self->getId() == m_initialDefenderId)
				return &MyStrategy::defendInitial;
		}
		else if(m_initialDefenderId != -1)
		{
			debugPrint( "Tick: " + toString(m_world->getTick()) + " initial defend finished" );
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

Point MyStrategy::getNet(const Player& player, const Hockeyist& attacker, PreferredFire preffered) const
{
	double netY = (player.getNetBottom() + player.getNetTop()) / 2;
	if (preffered == PreferredFire::eUNKNOWN)
		preffered = attacker.getY() < netY ? PreferredFire::eUP : PreferredFire::eDOWN;

	double netX = (player.getNetBack() + player.getNetFront()) / 2;
	netY += (preffered == PreferredFire::eUP ? 0.5 : -0.5) * m_game->getGoalNetHeight();  // attack far corner

	return Point(netX, netY);
}

void MyStrategy::defendTeammate()
{
	static const double kSAFE_ANGLE      = m_game->getStrikeAngleDeviation() + STRIKE_ANGLE;
	static const double kDANGEROUS_ANGLE = m_game->getStickSector() / 2;

	const Hockeyist* nearestSafe   = nullptr;
	const Hockeyist* nearestUnsafe = nullptr;
	const Puck&      puck          = m_world->getPuck();
	const Hockeyist* vip           = find_unit(getHockeyists(), [&puck](const Hockeyist& h){return h.getId() == puck.getOwnerHockeyistId();} );

	assert(vip && "no one to defend");
	if (!vip)
		return;

	// get nearest enemies
	for (const Hockeyist& h: getHockeyists())
	{
		if (h.isTeammate() || h.getType() == GOALIE || h.getState() == HockeyistState::RESTING || h.getState() == KNOCKED_DOWN)
			continue;

		const double distance = m_self->getDistanceTo(h);
		const double angle    = m_self->getAngleTo(h);
		bool isSafe = true;
		if (distance < m_game->getStickLength())
		{
			const Hockeyist attacking = getGhost(*m_self, 0, m_self->getAngle() + angle);
			if (attacking.getAngleTo(puck) < kSAFE_ANGLE || attacking.getAngleTo(*vip) < kSAFE_ANGLE)
				isSafe = false;
		}

		typedef const Hockeyist* TPtr;
		TPtr& nearest = isSafe ? nearestSafe : nearestUnsafe;

		if (nearest == nullptr || distance < m_self->getDistanceTo(nearest->getX(), nearest->getY()))
		{
			nearest = &h;
		}
	}
	
	static const double centerX = (m_game->getRinkRight() - m_game->getRinkLeft()) / 2;
	const Hockeyist* nearest = nearestSafe ? nearestSafe : nearestUnsafe;

	double angleToNearest = m_self->getAngleTo(*nearest); 
	bool   isCanStrike    = m_self->getDistanceTo(*nearest) < m_game->getStickLength() 
		                 && m_self->getRemainingCooldownTicks() == 0
		                 && angleToNearest < (m_game->getStickSector() / 2.0);

	const Player& me = m_world->getMyPlayer();
	double netX = me.getNetFront()	+ m_self->getRadius() * (Statistics::instance()->getMySide() == Statistics::eLEFT_SIDE ? -2 : 2);
	double netY = (me.getNetTop() + me.getNetBottom()) / 2;

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
			Hockeyist attacking   = getGhost(*m_self, 0, m_self->getAngle() + angleToNearest);
			double    angleToPuck = attacking.getAngleTo(puck);
			double    angleToVip  = attacking.getAngleTo(*vip);
			double    teammateDistance = std::min(attacking.getDistanceTo(puck), attacking.getDistanceTo(*vip));

			if (teammateDistance <= m_game->getStickLength())
			{
				double correction = 0;
				/* TODO
				if (std::abs(angleToPuck) < kDANGEROUS_ANGLE)
				{
					int sign    = (angleToPuck > kDANGEROUS_ANGLE) ? -1 : 1;
					correction += (kDANGEROUS_ANGLE - std::abs(angleToPuck) + kSAFE_ANGLE) * sign;
					attacking   = getGhost(*m_self, 0, m_self->getAngle() + angleToNearest + correction);
					angleToVip  = attacking.getAngleTo(*vip);
					angleToPuck = attacking.getAngleTo(puck);
				}

				if (std::abs(angleToVip) < kDANGEROUS_ANGLE)
				{
					int sign    = (angleToVip > kDANGEROUS_ANGLE) ? -1 : 1;
					correction += (kDANGEROUS_ANGLE - std::abs(angleToVip) + kSAFE_ANGLE) * sign;
					attacking   = getGhost(*m_self, 0, m_self->getAngle() + angleToNearest + correction);
					angleToVip  = attacking.getAngleTo(*vip);
					angleToPuck = attacking.getAngleTo(puck);
				}*/

				angleToNearest += correction;
				if ( (std::abs(angleToVip) < kDANGEROUS_ANGLE) || (std::abs(angleToPuck) < kDANGEROUS_ANGLE) )
				{
					// still can't strike, go backwards to my net?
					isCanStrike = false;

					double angle = - m_self->getAngleTo(netX, netY);
					m_move->setTurn(angle);
					m_move->setSpeedUp(-1);
					return;  // break processing
				}
			}
		}

		if (isCanStrike && std::abs(angleToNearest) < (kDANGEROUS_ANGLE - m_game->getStrikeAngleDeviation()))
		{
			m_move->setAction(ActionType::SWING);
		}
		else
		{
			// take attack position between enemy and my net
			static const  double stickLength = m_game->getStickLength();
			double attackDistance = std::min(stickLength, m_self->getRadius() * 2);
			double dx = 0;
			double dy = 0;

			double distance = m_self->getDistanceTo(*nearest);
			if (distance > stickLength / 2)
			{
				double distanceFactor = std::min(3.0, distance / stickLength);
				dx = distanceFactor * netX > nearest->getX() ? attackDistance : -attackDistance;
				dy = distanceFactor * netY > nearest->getY() ? attackDistance : -attackDistance;

			}
			else
			{
				debugPrint("Opponent is too close, try punching from this position");
			}

			m_move->setSpeedUp(1.0);
			m_move->setTurn   (m_self->getAngleTo(nearest->getX() + dx, nearest->getY() + dy));
			improveManeuverability();
		}
	}
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
	static const int    top       = static_cast<int>(m_game->getRinkTop());
	static const int    bottom    = static_cast<int>(m_game->getRinkBottom());
	static const double netHeight = m_world->getOpponentPlayer().getNetBottom() - m_world->getOpponentPlayer().getNetTop();
	static const int    width     = static_cast<int>(m_game->getWorldWidth());
	int unitRadius = static_cast<int>(m_self->getRadius());

	const auto& hockeists = getHockeyists();
	const Hockeyist* goalkeeper = find_unit(hockeists, [](const Hockeyist& h){ return !h.isTeammate() && h.getType() == GOALIE;});

	positions.reserve(std::min(bottom - top, width) / unitRadius * 2);

	PreferredFire fireFrom = m_firePositionMap[m_self->getId()];
	Point goal = getNet(m_world->getOpponentPlayer(), *m_self, fireFrom);

	if (fireFrom == PreferredFire::eUNKNOWN)
	{
		fireFrom = m_self->getY() > goal.y ? PreferredFire::eDOWN : PreferredFire::eUP;
	}

	int xDirection = m_world->getMyPlayer().getNetFront() > m_world->getOpponentPlayer().getNetFront() ? 1 /*I'm at right*/ : -1/*I'm at left*/;
	int yDirection = fireFrom == PreferredFire::eDOWN ? 1 : -1;
	
	double yThreshold = yDirection > 0 ? bottom - unitRadius : top + unitRadius;
    double yMargin    = yDirection * (goalkeeper ?  netHeight : unitRadius * 2);   // don't go too close to goalkeeper.

    auto isBottomCrossed = [yThreshold](double y){return y > yThreshold;};
    auto isTopCrossed    = [yThreshold](double y){return y < yThreshold;};

	for (double y = goal.y + yMargin; yDirection > 0 ? !isBottomCrossed(y) : !isTopCrossed(y); y += yDirection * unitRadius / 2.0)
	{
		double delta   = abs(y - goal.y);
		double x       = goal.x + delta * xDirection;
		int    penalty = 0;

		// calculate danger penalty: TODO: what if path to (x,y) is blocked?
		bool isEnemyAtPosition = false;
		bool isEnemyInBetween  = false;
		bool isEnemyStickThere = false;
		for (const Hockeyist& h: hockeists)
		{
			static const double kMAX_STICK_ANGLE     = m_game->getStickSector()/2;
			static const double kPUCK_SIZE           = m_world->getPuck().getRadius();
			static const double kSTICK_LENGTH        = m_game->getStickLength();

			if(h.isTeammate())
				continue;

			const double enemyDistance = h.getDistanceTo(x, y);
			const double enemyAngle    = std::abs(h.getAngleTo(x, y));
			isEnemyAtPosition = isEnemyAtPosition || ( enemyDistance <= h.getRadius() && enemyAngle <= PI / 2 );
			isEnemyStickThere = isEnemyStickThere || ( !isEnemyAtPosition && enemyAngle <= kMAX_STICK_ANGLE && enemyDistance <= kSTICK_LENGTH );
			isEnemyInBetween  = isEnemyInBetween  || ( !isEnemyAtPosition && enemyAngle <= PI / 2 && isInBetween(Point(x,y), h, *m_self, kPUCK_SIZE) );
		}

		static const int kENEMY_PENALTY         = 180; 
		static const int kENEMY_BETWEEN_PENALTY = 165; 
		static const int kENEMY_STICK_PENALTY   = 60;
		if (isEnemyAtPosition)
			penalty += kENEMY_PENALTY;
		if (isEnemyInBetween)
			penalty += kENEMY_BETWEEN_PENALTY;
		if (isEnemyStickThere)
			penalty += kENEMY_STICK_PENALTY;

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
		double goalkeeperX = goalkeeper ? goalkeeper->getX() : m_world->getMyPlayer().getNetFront();
		double targetX     = Statistics::instance()->getMySide() == Statistics::eLEFT_SIDE
			? goalkeeperX + m_self->getRadius() * 4
			: goalkeeperX - m_self->getRadius() * 4;

		return TFirePositions(1, FirePosition(Point(targetX, defender->getY()), 0, 0));
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
		double angleDegrees = toDegrees(std::abs(defender->getAngleTo(x, y)));
		int    penalty = static_cast<int>(angleDegrees / 2);

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
			if (puckPlayerId != m_world->getMyPlayer().getId())
				Statistics::instance()->onPuckLoose();

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

model::Hockeyist MyStrategy::getGhost(const model::Hockeyist& from, unsigned ticksIncrement, double overrideAngle)
{
	static const double kFrictionLoses = 0.95;
	const double speedLose = pow(kFrictionLoses, ticksIncrement);

	double x = from.getX() + from.getSpeedX() * ticksIncrement * kFrictionLoses;
	double y = from.getY() + from.getSpeedY() * ticksIncrement * kFrictionLoses;
	return Hockeyist(0, 0, 0, 0, from.getRadius(),  x, y, from.getSpeedX() * speedLose, from.getSpeedY() * speedLose, overrideAngle, from.getAngularSpeed(), 
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

bool MyStrategy::isInBetween(const Point& first, const model::Unit& inBetween, const model::Unit& second, double gap)
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

void MyStrategy::improveManeuverability()
{
	static const double kBrakeAngleThreshold = PI/4;
	static const double kBrakeSpeedThreshold = m_game->getHockeyistSpeedDownFactor();
	if (std::abs(m_move->getTurn()) > kBrakeAngleThreshold && toVectorSpeed(m_self->getSpeedX(), m_self->getSpeedY()) > kBrakeSpeedThreshold)
	{
		// TODO: try to move backwards if reasonable?
		m_move->setSpeedUp(-1.0);
	}	
}
