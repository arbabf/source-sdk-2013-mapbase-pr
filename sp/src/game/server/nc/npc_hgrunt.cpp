#include "cbase.h"
#include "npc_hgrunt.h"

LINK_ENTITY_TO_CLASS( npc_hgrunt, CNPC_HGrunt );

BEGIN_DATADESC( CNPC_HGrunt )
DEFINE_KEYFIELD(m_iSquadRole, FIELD_INTEGER, "squadrole"),
END_DATADESC()

void CNPC_HGrunt::Spawn( void )
{
	BaseClass::Spawn();

	// tint hgrunt based on class type, for now
	switch (m_iSquadRole)
	{
	case HGRUNT_MEDIC:
		SetRenderColor( 128, 255, 128 ); // slightly green
		break;

	case HGRUNT_ENGINEER:
		SetRenderColor( 128, 128, 255 ); // slightly blue
		break;
	}
}

void CNPC_HGrunt::SelectModel( void )
{
	// force model to be hgrunt
	SetModelName( AllocPooledString( "models/Humans/group01/male_07.mdl" ) );
	
	//SetModelName( AllocPooledString("models/hgrunt/hgrunt.mdl") );

	// if we were doing custom models for med + engi we would put them here.
}

// TODO: remove
#define CITIZEN_FOLLOWER_DESERT_FUNCTANK_DIST	45.0f*12.0f
bool CNPC_HGrunt::ShouldBehaviorSelectSchedule( CAI_BehaviorBase *pBehavior )
{
	if (pBehavior == &m_FollowBehavior)
	{

	}
	else if (IsInPlayerSquad() )
	{
		if (m_FollowBehavior.GetFollowTarget())
		{
			Vector vecFollowGoal = m_FollowBehavior.GetFollowTarget()->GetAbsOrigin();
			if (vecFollowGoal.DistToSqr( GetAbsOrigin() ) > Square( CITIZEN_FOLLOWER_DESERT_FUNCTANK_DIST ))
			{
				return false;
			}
		}
	}

	return BaseClass::ShouldBehaviorSelectSchedule( pBehavior );
}

void CNPC_HGrunt::OnChangeRunningBehavior( CAI_BehaviorBase *pOldBehavior, CAI_BehaviorBase *pNewBehavior )
{
	BaseClass::OnChangeRunningBehavior( pOldBehavior, pNewBehavior );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::GatherConditions()
{
	BaseClass::GatherConditions();

	if (IsInPlayerSquad() && hl2_episodic.GetBool())
	{
		// Leave the player squad if someone has made me neutral to player.
		if (IRelationType( UTIL_GetLocalPlayer() ) == D_NU)
		{
			RemoveFromPlayerSquad();
		}
	}

	if (!SpokeConcept( TLK_JOINPLAYER ) && IsRunningScriptedSceneWithSpeech( this, true ))
	{
		SetSpokeConcept( TLK_JOINPLAYER, NULL );
		for (int i = 0; i < g_AI_Manager.NumAIs(); i++)
		{
			CAI_BaseNPC *pNpc = g_AI_Manager.AccessAIs()[i];
			if (pNpc != this && pNpc->GetClassname() == GetClassname() && pNpc->GetAbsOrigin().DistToSqr( GetAbsOrigin() ) < Square( 15 * 12 ) && FVisible( pNpc ))
			{
				(assert_cast<CNPC_HGrunt *>(pNpc))->SetSpokeConcept( TLK_JOINPLAYER, NULL );
			}
		}
	}

	if (ShouldLookForHealthItem())
	{
		if (FindHealthItem( GetAbsOrigin(), Vector( 240, 240, 240 ) ))
			SetCondition( COND_HEALTH_ITEM_AVAILABLE );
		else
			ClearCondition( COND_HEALTH_ITEM_AVAILABLE );

		m_flNextHealthSearchTime = gpGlobals->curtime + 4.0;
	}

	// If the player is standing near a medic and can see the medic, 
	// assume the player is 'staring' and wants health.
	if (CanHeal())
	{
		CBasePlayer *pPlayer = AI_GetSinglePlayer();

		if (!pPlayer)
		{
			m_flTimePlayerStare = FLT_MAX;
			return;
		}

		float flDistSqr = (GetAbsOrigin() - pPlayer->GetAbsOrigin()).Length2DSqr();
		float flStareDist = sk_citizen_player_stare_dist.GetFloat();
		float flPlayerDamage = pPlayer->GetMaxHealth() - pPlayer->GetHealth();

		if (pPlayer->IsAlive() && flPlayerDamage > 0 && (flDistSqr <= flStareDist * flStareDist) && pPlayer->FInViewCone( this ) && pPlayer->FVisible( this ))
		{
			if (m_flTimePlayerStare == FLT_MAX)
			{
				// Player wasn't looking at me at last think. He started staring now.
				m_flTimePlayerStare = gpGlobals->curtime;
			}

			// Heal if it's been long enough since last time I healed a staring player.
			if (gpGlobals->curtime - m_flTimePlayerStare >= sk_citizen_player_stare_time.GetFloat() && gpGlobals->curtime > m_flTimeNextHealStare && !IsCurSchedule( SCHED_CITIZEN_HEAL ))
			{
				if (ShouldHealTarget( pPlayer, true ))
				{
					SetCondition( COND_CIT_PLAYERHEALREQUEST );
				}
				else
				{
					m_flTimeNextHealStare = gpGlobals->curtime + sk_citizen_stare_heal_time.GetFloat() * .5f;
					ClearCondition( COND_CIT_PLAYERHEALREQUEST );
				}
			}

#ifdef HL2_EPISODIC
			// Heal if I'm on an assault. The player hasn't had time to stare at me.
			if (m_AssaultBehavior.IsRunning() && IsMoving())
			{
				SetCondition( COND_CIT_PLAYERHEALREQUEST );
			}
#endif
		}
		else
		{
			m_flTimePlayerStare = FLT_MAX;
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::PredictPlayerPush()
{
	if (!AI_IsSinglePlayer())
		return;

	if (HasCondition( COND_CIT_PLAYERHEALREQUEST ))
		return;

	bool bHadPlayerPush = HasCondition( COND_PLAYER_PUSHING );

	BaseClass::PredictPlayerPush();

	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if (!bHadPlayerPush && HasCondition( COND_PLAYER_PUSHING ) &&
		pPlayer->FInViewCone( this ) && CanHeal())
	{
		if (ShouldHealTarget( pPlayer, true ))
		{
			ClearCondition( COND_PLAYER_PUSHING );
			SetCondition( COND_CIT_PLAYERHEALREQUEST );
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::PrescheduleThink()
{
	BaseClass::PrescheduleThink();

	UpdatePlayerSquad();
	UpdateFollowCommandPoint();

	if (!npc_citizen_insignia.GetBool() && npc_citizen_squad_marker.GetBool() && IsInPlayerSquad())
	{
		Vector mins = WorldAlignMins() * .5 + GetAbsOrigin();
		Vector maxs = WorldAlignMaxs() * .5 + GetAbsOrigin();

		float rMax = 255;
		float gMax = 255;
		float bMax = 255;

		float rMin = 255;
		float gMin = 128;
		float bMin = 0;

		const float TIME_FADE = 1.0;
		float timeInSquad = gpGlobals->curtime - m_flTimeJoinedPlayerSquad;
		timeInSquad = MIN( TIME_FADE, MAX( timeInSquad, 0 ) );

		float fade = (1.0 - timeInSquad / TIME_FADE);

		float r = rMin + (rMax - rMin) * fade;
		float g = gMin + (gMax - gMin) * fade;
		float b = bMin + (bMax - bMin) * fade;

		// THIS IS A PLACEHOLDER UNTIL WE HAVE A REAL DESIGN & ART -- DO NOT REMOVE
		NDebugOverlay::Line( Vector( mins.x, GetAbsOrigin().y, GetAbsOrigin().z + 1 ), Vector( maxs.x, GetAbsOrigin().y, GetAbsOrigin().z + 1 ), r, g, b, false, .11 );
		NDebugOverlay::Line( Vector( GetAbsOrigin().x, mins.y, GetAbsOrigin().z + 1 ), Vector( GetAbsOrigin().x, maxs.y, GetAbsOrigin().z + 1 ), r, g, b, false, .11 );
	}
	if (GetEnemy() && g_ai_citizen_show_enemy.GetBool())
	{
		NDebugOverlay::Line( EyePosition(), GetEnemy()->EyePosition(), 255, 0, 0, false, .1 );
	}

	if (DebuggingCommanderMode())
	{
		if (HaveCommandGoal())
		{
			CBaseEntity *pCommandPoint = gEntList.FindEntityByClassname( NULL, COMMAND_POINT_CLASSNAME );

			if (pCommandPoint)
			{
				NDebugOverlay::Cross3D( pCommandPoint->GetAbsOrigin(), 16, 0, 255, 255, false, 0.1 );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Allows for modification of the interrupt mask for the current schedule.
//			In the most cases the base implementation should be called first.
//-----------------------------------------------------------------------------
void CNPC_HGrunt::BuildScheduleTestBits()
{
	BaseClass::BuildScheduleTestBits();

	if (IsCurSchedule( SCHED_IDLE_STAND ) || IsCurSchedule( SCHED_ALERT_STAND ))
	{
		SetCustomInterruptCondition( COND_CIT_START_INSPECTION );
	}

	if (IsMedic() && IsCustomInterruptConditionSet( COND_HEAR_MOVE_AWAY ))
	{
		if (!IsCurSchedule( SCHED_RELOAD, false ))
		{
			// Since schedule selection code prioritizes reloading over requests to heal
			// the player, we must prevent this condition from breaking the reload schedule.
			SetCustomInterruptCondition( COND_CIT_PLAYERHEALREQUEST );
		}

		SetCustomInterruptCondition( COND_CIT_COMMANDHEAL );
	}

	if (!IsCurSchedule( SCHED_NEW_WEAPON ))
	{
		SetCustomInterruptCondition( COND_RECEIVED_ORDERS );
	}

	if (GetCurSchedule()->HasInterrupt( COND_IDLE_INTERRUPT ))
	{
		SetCustomInterruptCondition( COND_BETTER_WEAPON_AVAILABLE );
	}

#ifdef HL2_EPISODIC
	if (IsMedic() && m_AssaultBehavior.IsRunning())
	{
		if (!IsCurSchedule( SCHED_RELOAD, false ))
		{
			SetCustomInterruptCondition( COND_CIT_PLAYERHEALREQUEST );
		}

		SetCustomInterruptCondition( COND_CIT_COMMANDHEAL );
	}
#else
	if (IsMedic() && m_AssaultBehavior.IsRunning() && !IsMoving())
	{
		if (!IsCurSchedule( SCHED_RELOAD, false ))
		{
			SetCustomInterruptCondition( COND_CIT_PLAYERHEALREQUEST );
		}

		SetCustomInterruptCondition( COND_CIT_COMMANDHEAL );
	}
#endif
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::FInViewCone( CBaseEntity *pEntity )
{
#if 0
	if (IsMortar( pEntity ))
	{
		// @TODO (toml 11-20-03): do this only if have heard mortar shell recently and it's active
		return true;
	}
#endif
	return BaseClass::FInViewCone( pEntity );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode )
{
	switch (failedSchedule)
	{
	case SCHED_NEW_WEAPON:
		// If failed trying to pick up a weapon, try again in one second. This is because other AI code
		// has put this off for 10 seconds under the assumption that the citizen would be able to 
		// pick up the weapon that they found. 
		m_flNextWeaponSearchTime = gpGlobals->curtime + 1.0f;
		break;

	case SCHED_ESTABLISH_LINE_OF_FIRE_FALLBACK:
	case SCHED_MOVE_TO_WEAPON_RANGE:
		if (!IsMortar( GetEnemy() ))
		{
			if (GetActiveWeapon() && (GetActiveWeapon()->CapabilitiesGet() & bits_CAP_WEAPON_RANGE_ATTACK1) && random->RandomInt( 0, 1 ) && HasCondition( COND_SEE_ENEMY ) && !HasCondition( COND_NO_PRIMARY_AMMO ))
				return TranslateSchedule( SCHED_RANGE_ATTACK1 );

			return SCHED_STANDOFF;
		}
		break;
	}

	return BaseClass::SelectFailSchedule( failedSchedule, failedTask, taskFailCode );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectSchedule()
{
	return BaseClass::SelectSchedule();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectSchedulePriorityAction()
{
	int schedule = SelectScheduleHeal();
	if (schedule != SCHED_NONE)
		return schedule;

	schedule = BaseClass::SelectSchedulePriorityAction();
	if (schedule != SCHED_NONE)
		return schedule;

	schedule = SelectScheduleRetrieveItem();
	if (schedule != SCHED_NONE)
		return schedule;

	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
// Determine if citizen should perform heal action.
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectScheduleHeal()
{
	// episodic medics may toss the healthkits rather than poke you with them
#if HL2_EPISODIC

	if (CanHeal())
	{
		CBaseEntity *pEntity = PlayerInRange( GetLocalOrigin(), HEAL_TOSS_TARGET_RANGE );
		if (pEntity)
		{
			if (USE_EXPERIMENTAL_MEDIC_CODE() && IsMedic())
			{
				// use the new heal toss algorithm
				if (ShouldHealTossTarget( pEntity, HasCondition( COND_CIT_PLAYERHEALREQUEST ) ))
				{
					SetTarget( pEntity );
					return SCHED_CITIZEN_HEAL_TOSS;
				}
			}
			else if (PlayerInRange( GetLocalOrigin(), HEAL_MOVE_RANGE ))
			{
				// use old mechanism for ammo
				if (ShouldHealTarget( pEntity, HasCondition( COND_CIT_PLAYERHEALREQUEST ) ))
				{
					SetTarget( pEntity );
					return SCHED_CITIZEN_HEAL;
				}
			}

		}

		if (m_pSquad)
		{
			pEntity = NULL;
			float distClosestSq = HEAL_MOVE_RANGE*HEAL_MOVE_RANGE;
			float distCurSq;

			AISquadIter_t iter;
			CAI_BaseNPC *pSquadmate = m_pSquad->GetFirstMember( &iter );
			while (pSquadmate)
			{
				if (pSquadmate != this)
				{
					distCurSq = (GetAbsOrigin() - pSquadmate->GetAbsOrigin()).LengthSqr();
					if (distCurSq < distClosestSq && ShouldHealTarget( pSquadmate ))
					{
						distClosestSq = distCurSq;
						pEntity = pSquadmate;
					}
				}

				pSquadmate = m_pSquad->GetNextMember( &iter );
			}

			if (pEntity)
			{
				SetTarget( pEntity );
				return SCHED_CITIZEN_HEAL;
			}
		}
	}
	else
	{
		if (HasCondition( COND_CIT_PLAYERHEALREQUEST ))
			DevMsg( "Would say: sorry, need to recharge\n" );
	}

	return SCHED_NONE;

#else

	if (CanHeal())
	{
		CBaseEntity *pEntity = PlayerInRange( GetLocalOrigin(), HEAL_MOVE_RANGE );
		if (pEntity && ShouldHealTarget( pEntity, HasCondition( COND_CIT_PLAYERHEALREQUEST ) ))
		{
			SetTarget( pEntity );
			return SCHED_CITIZEN_HEAL;
		}

		if (m_pSquad)
		{
			pEntity = NULL;
			float distClosestSq = HEAL_MOVE_RANGE*HEAL_MOVE_RANGE;
			float distCurSq;

			AISquadIter_t iter;
			CAI_BaseNPC *pSquadmate = m_pSquad->GetFirstMember( &iter );
			while (pSquadmate)
			{
				if (pSquadmate != this)
				{
					distCurSq = (GetAbsOrigin() - pSquadmate->GetAbsOrigin()).LengthSqr();
					if (distCurSq < distClosestSq && ShouldHealTarget( pSquadmate ))
					{
						distClosestSq = distCurSq;
						pEntity = pSquadmate;
					}
				}

				pSquadmate = m_pSquad->GetNextMember( &iter );
			}

			if (pEntity)
			{
				SetTarget( pEntity );
				return SCHED_CITIZEN_HEAL;
			}
		}
	}
	else
	{
		if (HasCondition( COND_CIT_PLAYERHEALREQUEST ))
			DevMsg( "Would say: sorry, need to recharge\n" );
	}

	return SCHED_NONE;

#endif
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectScheduleRetrieveItem()
{
	if (HasCondition( COND_BETTER_WEAPON_AVAILABLE ))
	{
		CBaseHLCombatWeapon *pWeapon = dynamic_cast<CBaseHLCombatWeapon *>(Weapon_FindUsable( WEAPON_SEARCH_DELTA ));
		if (pWeapon)
		{
			m_flNextWeaponSearchTime = gpGlobals->curtime + 10.0;
			// Now lock the weapon for several seconds while we go to pick it up.
			pWeapon->Lock( 10.0, this );
			SetTarget( pWeapon );
			return SCHED_NEW_WEAPON;
		}
	}

	if (HasCondition( COND_HEALTH_ITEM_AVAILABLE ))
	{
		if (!IsInPlayerSquad())
		{
			// Been kicked out of the player squad since the time I located the health.
			ClearCondition( COND_HEALTH_ITEM_AVAILABLE );
		}
		else
		{
			CBaseEntity *pBase = FindHealthItem( m_FollowBehavior.GetFollowTarget()->GetAbsOrigin(), Vector( 120, 120, 120 ) );
			CItem *pItem = dynamic_cast<CItem *>(pBase);

			if (pItem)
			{
				SetTarget( pItem );
				return SCHED_GET_HEALTHKIT;
			}
		}
	}
	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectScheduleNonCombat()
{
	if (m_NPCState == NPC_STATE_IDLE)
	{

	}

	ClearCondition( COND_CIT_START_INSPECTION );

	if (m_bShouldPatrol)
		return SCHED_CITIZEN_PATROL;

	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectScheduleCombat()
{
	int schedule = SelectScheduleManhackCombat();
	if (schedule != SCHED_NONE)
		return schedule;

	return BaseClass::SelectScheduleCombat();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::ShouldDeferToFollowBehavior()
{
#if 0
	if (HaveCommandGoal())
		return false;
#endif

	return BaseClass::ShouldDeferToFollowBehavior();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::TranslateSchedule( int scheduleType )
{
	CBasePlayer *pLocalPlayer = AI_GetSinglePlayer();

	switch (scheduleType)
	{
	case SCHED_IDLE_STAND:
	case SCHED_ALERT_STAND:
		if (m_NPCState != NPC_STATE_COMBAT && pLocalPlayer && !pLocalPlayer->IsAlive() && CanJoinPlayerSquad())
		{
			// Player is dead! 
			float flDist;
			flDist = (pLocalPlayer->GetAbsOrigin() - GetAbsOrigin()).Length();

			if (flDist < 50 * 12)
			{
				AddSpawnFlags( SF_CITIZEN_NOT_COMMANDABLE );
				return SCHED_CITIZEN_MOURN_PLAYER;
			}
		}
		break;

	case SCHED_ESTABLISH_LINE_OF_FIRE:
	case SCHED_MOVE_TO_WEAPON_RANGE:
		if (!IsMortar( GetEnemy() ) && HaveCommandGoal())
		{
			if (GetActiveWeapon() && (GetActiveWeapon()->CapabilitiesGet() & bits_CAP_WEAPON_RANGE_ATTACK1) && random->RandomInt( 0, 1 ) && HasCondition( COND_SEE_ENEMY ) && !HasCondition( COND_NO_PRIMARY_AMMO ))
				return TranslateSchedule( SCHED_RANGE_ATTACK1 );

			return SCHED_STANDOFF;
		}
		break;

	case SCHED_CHASE_ENEMY:
		if (!IsMortar( GetEnemy() ) && HaveCommandGoal())
		{
			return SCHED_STANDOFF;
		}
		break;

	case SCHED_RANGE_ATTACK1:
		// If we have an RPG, we use a custom schedule for it
		if (!IsMortar( GetEnemy() ) && GetActiveWeapon() && FClassnameIs( GetActiveWeapon(), "weapon_rpg" ))
		{
			if (GetEnemy() && GetEnemy()->ClassMatches( "npc_strider" ))
			{
				if (OccupyStrategySlotRange( SQUAD_SLOT_CITIZEN_RPG1, SQUAD_SLOT_CITIZEN_RPG2 ))
				{
					return SCHED_CITIZEN_STRIDER_RANGE_ATTACK1_RPG;
				}
				else
				{
					return SCHED_STANDOFF;
				}
			}
			else
			{
				CBasePlayer *pPlayer = AI_GetSinglePlayer();
				if (pPlayer && GetEnemy() && ((GetEnemy()->GetAbsOrigin() -
					pPlayer->GetAbsOrigin()).LengthSqr() < RPG_SAFE_DISTANCE * RPG_SAFE_DISTANCE))
				{
					// Don't fire our RPG at an enemy too close to the player
					return SCHED_STANDOFF;
				}
				else
				{
					return SCHED_CITIZEN_RANGE_ATTACK1_RPG;
				}
			}
		}
		break;
	}

	return BaseClass::TranslateSchedule( scheduleType );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::ShouldAcceptGoal( CAI_BehaviorBase *pBehavior, CAI_GoalEntity *pGoal )
{
	if (BaseClass::ShouldAcceptGoal( pBehavior, pGoal ))
	{
		CAI_FollowBehavior *pFollowBehavior = dynamic_cast<CAI_FollowBehavior *>(pBehavior);
		if (pFollowBehavior)
		{
			if (IsInPlayerSquad())
			{
				m_hSavedFollowGoalEnt = (CAI_FollowGoal *)pGoal;
				return false;
			}
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::OnClearGoal( CAI_BehaviorBase *pBehavior, CAI_GoalEntity *pGoal )
{
	if (m_hSavedFollowGoalEnt == pGoal)
		m_hSavedFollowGoalEnt = NULL;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::StartTask( const Task_t *pTask )
{
	switch (pTask->iTask)
	{
	case TASK_CIT_HEAL:
#if HL2_EPISODIC
	case TASK_CIT_HEAL_TOSS:
#endif
		if (IsMedic())
		{
			if (GetTarget() && GetTarget()->IsPlayer() && GetTarget()->m_iMaxHealth == GetTarget()->m_iHealth)
			{
				// Doesn't need us anymore
				TaskComplete();
				break;
			}

			Speak( TLK_HEAL );
		}
		else if (IsAmmoResupplier())
		{
			Speak( TLK_GIVEAMMO );
		}
		SetIdealActivity( (Activity)ACT_CIT_HEAL );
		break;

	case TASK_CIT_SPEAK_MOURNING:
		if (!IsSpeaking() && CanSpeakAfterMyself())
		{
			//CAI_AllySpeechManager *pSpeechManager = GetAllySpeechManager();

			//if ( pSpeechManager-> )

			Speak( TLK_PLDEAD );
		}
		TaskComplete();
		break;

	default:
		BaseClass::StartTask( pTask );
		break;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::RunTask( const Task_t *pTask )
{
	switch (pTask->iTask)
	{
	case TASK_WAIT_FOR_MOVEMENT:
	{
		BaseClass::RunTask( pTask );
		break;
	}

	case TASK_MOVE_TO_TARGET_RANGE:
	{
		// If we're moving to heal a target, and the target dies, stop
		if (IsCurSchedule( SCHED_CITIZEN_HEAL ) && (!GetTarget() || !GetTarget()->IsAlive()))
		{
			TaskFail( FAIL_NO_TARGET );
			return;
		}

		BaseClass::RunTask( pTask );
		break;
	}

	case TASK_CIT_HEAL:
		if (IsSequenceFinished())
		{
			TaskComplete();
		}
		else if (!GetTarget())
		{
			// Our heal target was killed or deleted somehow.
			TaskFail( FAIL_NO_TARGET );
		}
		else
		{
			if ((GetTarget()->GetAbsOrigin() - GetAbsOrigin()).Length2D() > HEAL_MOVE_RANGE / 2)
				TaskComplete();

			GetMotor()->SetIdealYawToTargetAndUpdate( GetTarget()->GetAbsOrigin() );
		}
		break;


#if HL2_EPISODIC
	case TASK_CIT_HEAL_TOSS:
		if (IsSequenceFinished())
		{
			TaskComplete();
		}
		else if (!GetTarget())
		{
			// Our heal target was killed or deleted somehow.
			TaskFail( FAIL_NO_TARGET );
		}
		else
		{
			GetMotor()->SetIdealYawToTargetAndUpdate( GetTarget()->GetAbsOrigin() );
		}
		break;

#endif

	default:
		BaseClass::RunTask( pTask );
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : code - 
//-----------------------------------------------------------------------------
void CNPC_HGrunt::TaskFail( AI_TaskFailureCode_t code )
{
	// If our heal task has failed, push out the heal time
	if (IsCurSchedule( SCHED_CITIZEN_HEAL ))
	{
		m_flPlayerHealTime = gpGlobals->curtime + sk_citizen_heal_ally_delay.GetFloat();
	}

	if (code == FAIL_NO_ROUTE_BLOCKED && m_bNotifyNavFailBlocked)
	{
		m_OnNavFailBlocked.FireOutput( this, this );
	}

	BaseClass::TaskFail( code );
}

//-----------------------------------------------------------------------------
// Purpose: Override base class activiites
//-----------------------------------------------------------------------------
Activity CNPC_HGrunt::NPC_TranslateActivity( Activity activity )
{
	if (activity == ACT_MELEE_ATTACK1)
	{
		return ACT_MELEE_ATTACK_SWING;
	}

	// !!!HACK - Citizens don't have the required animations for shotguns, 
	// so trick them into using the rifle counterparts for now (sjb)
	if (activity == ACT_RUN_AIM_SHOTGUN)
		return ACT_RUN_AIM_RIFLE;
	if (activity == ACT_WALK_AIM_SHOTGUN)
		return ACT_WALK_AIM_RIFLE;
	if (activity == ACT_IDLE_ANGRY_SHOTGUN)
		return ACT_IDLE_ANGRY_SMG1;
	if (activity == ACT_RANGE_ATTACK_SHOTGUN_LOW)
		return ACT_RANGE_ATTACK_SMG1_LOW;

	return BaseClass::NPC_TranslateActivity( activity );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_HGrunt::HandleAnimEvent( animevent_t *pEvent )
{
	if (pEvent->event == AE_CITIZEN_GET_PACKAGE)
	{
		// Give the citizen a package
		CBaseCombatWeapon *pWeapon = Weapon_Create( "weapon_citizenpackage" );
		if (pWeapon)
		{
			// If I have a name, make my weapon match it with "_weapon" appended
			if (GetEntityName() != NULL_STRING)
			{
				pWeapon->SetName( AllocPooledString( UTIL_VarArgs( "%s_weapon", STRING( GetEntityName() ) ) ) );
			}
			Weapon_Equip( pWeapon );
		}
		return;
	}
	else if (pEvent->event == AE_CITIZEN_HEAL)
	{
		// Heal my target (if within range)
#if HL2_EPISODIC
		if (USE_EXPERIMENTAL_MEDIC_CODE() && IsMedic())
		{
			CBaseCombatCharacter *pTarget = dynamic_cast<CBaseCombatCharacter *>(GetTarget());
			Assert( pTarget );
			if (pTarget)
			{
				m_flPlayerHealTime = gpGlobals->curtime + sk_citizen_heal_toss_player_delay.GetFloat();;
				TossHealthKit( pTarget, Vector( 48.0f, 0.0f, 0.0f ) );
			}
		}
		else
		{
			Heal();
		}
#else
		Heal();
#endif
		return;
	}

	switch (pEvent->event)
	{
	case NPC_EVENT_LEFTFOOT:
	{
		EmitSound( "NPC_Citizen.FootstepLeft", pEvent->eventtime );
	}
	break;

	case NPC_EVENT_RIGHTFOOT:
	{
		EmitSound( "NPC_Citizen.FootstepRight", pEvent->eventtime );
	}
	break;

	default:
		BaseClass::HandleAnimEvent( pEvent );
		break;
	}
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CNPC_HGrunt::PickupItem( CBaseEntity *pItem )
{
	Assert( pItem != NULL );
	if (FClassnameIs( pItem, "item_healthkit" ))
	{
		if (TakeHealth( sk_healthkit.GetFloat(), DMG_GENERIC ))
		{
			RemoveAllDecals();
			UTIL_Remove( pItem );
		}
	}
	else if (FClassnameIs( pItem, "item_healthvial" ))
	{
		if (TakeHealth( sk_healthvial.GetFloat(), DMG_GENERIC ))
		{
			RemoveAllDecals();
			UTIL_Remove( pItem );
		}
	}
	else
	{
		DevMsg( "Citizen doesn't know how to pick up %s!\n", pItem->GetClassname() );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::IgnorePlayerPushing( void )
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Return a random expression for the specified state to play over 
//			the state's expression loop.
//-----------------------------------------------------------------------------
const char *CNPC_HGrunt::SelectRandomExpressionForState( NPC_STATE state )
{
	// Hacky remap of NPC states to expression states that we care about
	int iExpressionState = 0;
	switch (state)
	{
	case NPC_STATE_IDLE:
		iExpressionState = 0;
		break;

	case NPC_STATE_ALERT:
		iExpressionState = 1;
		break;

	case NPC_STATE_COMBAT:
		iExpressionState = 2;
		break;

	default:
		// An NPC state we don't have expressions for
		return NULL;
	}

	default:
		break;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::SimpleUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// Under these conditions, citizens will refuse to go with the player.
	// Robin: NPCs should always respond to +USE even if someone else has the semaphore.
	m_bDontUseSemaphore = true;

	// First, try to speak the +USE concept
	if (!SelectPlayerUseSpeech())
	{
		if (IRelationType( pActivator ) == D_NU)
		{
			// If I'm denying commander mode because a level designer has made that decision,
			// then fire this output in case they've hooked it to an event.
			m_OnDenyCommanderUse.FireOutput( this, this );
		}
	}

	m_bDontUseSemaphore = false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::OnBeginMoveAndShoot()
{
	if (BaseClass::OnBeginMoveAndShoot())
	{
		if (m_iMySquadSlot == SQUAD_SLOT_ATTACK1 || m_iMySquadSlot == SQUAD_SLOT_ATTACK2)
			return true; // already have the slot I need

		if (m_iMySquadSlot == SQUAD_SLOT_NONE && OccupyStrategySlotRange( SQUAD_SLOT_ATTACK1, SQUAD_SLOT_ATTACK2 ))
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::OnEndMoveAndShoot()
{
	VacateStrategySlot();
}

// schedules
AI_BEGIN_CUSTOM_NPC(npc_hgrunt, CNPC_HGrunt)
DECLARE_TASK(TASK_HGRUNT_MEDIC_HEAL)
AI_END_CUSTOM_NPC()