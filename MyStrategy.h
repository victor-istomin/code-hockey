#pragma once

#ifndef _MY_STRATEGY_H_
#define _MY_STRATEGY_H_

#include "Strategy.h"
#include "Utils.h"

class MyStrategy : public Strategy 
{
	const model::Hockeyist* m_self;
	const model::World*     m_world; 
	const model::Game*      m_game; 
	model::Move*            m_move;

	static const double STRIKE_ANGLE;

	void update(const model::Hockeyist* self, const model::World* world, const model::Game* game, model::Move* move)
	{
		m_self  = self;
		m_world = world;
		m_move  = move;
		m_game  = game;
	}

public:
    MyStrategy();


    void move(const model::Hockeyist& self, const model::World& world, const model::Game& game, model::Move& move);

private:
	//! get puck ownership
	void attackPuck();

	//! attack opponent's net
	void attackNet();

	//! defend teammate
	void defendTeammate();

	typedef void (MyStrategy::* TActionPtr)();

	// get current strategy action
	TActionPtr getCurrentAction();

	// ---- utils

	Point getOpponentNet() const;              //! get preferred attack point in net
	const model::Hockeyist* getNearestOpponent() const;
};

#endif
