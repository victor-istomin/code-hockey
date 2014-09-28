#pragma once

#ifndef _MY_STRATEGY_H_
#define _MY_STRATEGY_H_

#include "Strategy.h"
#include "Utils.h"
#include <memory>
#include <map>

class Statistics;

class MyStrategy : public Strategy 
{
	typedef void (MyStrategy::* TActionPtr)();
	typedef std::vector<FirePosition>     TFirePositions;
	typedef std::vector<model::Hockeyist> THockeyists;
	typedef long long                     TId;

	const model::Hockeyist* m_self;
	const model::World*     m_world; 
	const model::Game*      m_game; 
	model::Move*            m_move;

	static const double                 STRIKE_ANGLE;
	static TId                          m_initialDefenderId;
	static std::map<TId, PreferredFire> m_firePositionMap;  // id of hockeyist which wants to fire from far (not near!) angle

	void update(const model::Hockeyist* self, const model::World* world, const model::Game* game, model::Move* move)
	{
		m_self  = self;
		m_world = world;
		m_move  = move;
		m_game  = game;
	}

public:
    MyStrategy();
	~MyStrategy();

    void move(const model::Hockeyist& self, const model::World& world, const model::Game& game, model::Move& move);

private:
	//! get puck ownership
	void attackPuck();

	//! attack opponent's net
	void attackNet();

	//! defend teammate
	void defendTeammate();

	//! initial net defend (puck got by opponent right at (sub-)round start
	void defendInitial();
	
	//! just rest after goal
	void haveRest();

	//! get current strategy action
	TActionPtr getCurrentAction();
	
	void updateStatistics();	
	
	//! let Hockeyist use brakes, if needed
	void improveManeuverability();

	// ---- utils

	Point getNet(const model::Player& player, const model::Hockeyist& attacker, PreferredFire preffered = PreferredFire::eUNKNOWN) const;        //! get preferred attack point in net
	Point getEstimatedPuckPos() const;
	Point getFirePoint() const;
	Point getSubstitutionPoint() const;

	const THockeyists&      getHockeyists() const { return m_world->getHockeyists(); }
	const model::Hockeyist* getPuckOwner() const;

	TFirePositions fillFirePositions() const;
	TFirePositions fillDefenderPositions(const model::Hockeyist* attacker, const model::Hockeyist* defender) const;
	void findInitialDefender();

	bool isRestTime() const {return m_world->getMyPlayer().isJustMissedGoal() || m_world->getOpponentPlayer().isJustMissedGoal(); }
	static bool isInBetween(const Point& first, const model::Unit& inBetween, const model::Unit& second, double gap);

	//! get ghost from the future
	model::Hockeyist getGhost(const model::Hockeyist& from, unsigned ticksIncrement, double overrideAngle);
};

#endif
