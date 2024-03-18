//============== Copyleft arbabf, all rights not reserved.================
//
// Purpose: Human grunts, who shoot bullets, heal things, and torch
// doors.
//
//========================================================================

#include "cbase.h"
#include "npc_hgrunt.h"

#include "hl2_player.h"
#include "items.h"

#include "ai_squad.h"
#include "ai_pathfinder.h"
#include "ai_route.h"
#include "ai_hint.h"
#include "ai_interactions.h"
#include "ai_looktarget.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar sk_healthkit;
extern ConVar sk_healthvial;
extern ConVar ai_citizen_debug_commander;
// todo: autosummon
extern ConVar player_squad_autosummon_debug;
extern ConVar player_squad_autosummon_player_tolerance;
extern ConVar player_squad_autosummon_time_after_combat;
extern ConVar player_squad_autosummon_time;
extern ConVar player_squad_autosummon_move_tolerance;

const int MAX_PLAYER_SQUAD = 8;

ConVar sk_hgrunt_health( "sk_hgrunt_health", "60" );
ConVar sk_hgrunt_medic_heal_amount( "sk_hgrunt_medic_heal_amount", "40" ); // how much to heal the target for
ConVar sk_hgrunt_medic_heal_cooldown( "sk_hgrunt_medic_heal_cooldown", "60" ); // heal once every this seconds
ConVar sk_hgrunt_medic_heal_threshold( "sk_hgrunt_medic_heal_threshold", "40" ); // heal if target is less than this hp
ConVar sk_hgrunt_medic_max_heal_charge( "sk_hgrunt_medic_heal_charge", "200" ); // how much healing the hgrunt medic can have stored

#define HGRUNT_HEAL_RANGE 512.0f
#define FRIENDLY_FIRE_TOLERANCE_LIMIT 3
#define DebuggingCommanderMode() (ai_citizen_debug_commander.GetBool() && (m_debugOverlays & OVERLAY_NPC_SELECTED_BIT))
#define COMMAND_POINT_CLASSNAME "info_target_command_point"
// todo: clean up autosquad function
#define ShouldAutosquad() (HasSpawnFlags(SF_HGRUNT_JOIN_WHEN_NEARBY) && !m_bRemovedFromPlayerSquad && !HasSpawnFlags(SF_HGRUNT_NOT_COMMANDABLE))

LINK_ENTITY_TO_CLASS( npc_hgrunt, CNPC_HGrunt );

BEGIN_DATADESC( CNPC_HGrunt )
DEFINE_KEYFIELD(m_iSquadRole, FIELD_INTEGER, "squadrole"),
DEFINE_KEYFIELD( m_bNeverLeavePlayerSquad, FIELD_BOOLEAN, "neverleaveplayersquad" ),
DEFINE_FIELD(m_flLastHealTime, FIELD_TIME),
DEFINE_FIELD( m_flTimeLastCloseToPlayer, FIELD_TIME ),
DEFINE_FIELD( m_flTimeJoinedPlayerSquad, FIELD_TIME ),
DEFINE_FIELD(m_hLastHealTarget, FIELD_EHANDLE),
DEFINE_FIELD(m_iHealCharge, FIELD_INTEGER),
DEFINE_FIELD(m_iFriendlyFireTolerance, FIELD_INTEGER),
DEFINE_FIELD(m_iOldNpcState, FIELD_INTEGER),
DEFINE_FIELD( m_bWasInPlayerSquad, FIELD_BOOLEAN ),
DEFINE_FIELD(m_bRemovedFromPlayerSquad, FIELD_BOOLEAN),
DEFINE_FIELD( m_vAutoSummonAnchor, FIELD_POSITION_VECTOR ),
DEFINE_FIELD( m_iszOriginalSquad, FIELD_STRING ),
DEFINE_EMBEDDED( m_AutoSummonTimer ),
END_DATADESC()

CSimpleSimTimer CNPC_HGrunt::gm_PlayerSquadEvaluateTimer;

void CNPC_HGrunt::Spawn( void )
{
	BaseClass::Spawn();

	m_iMaxHealth = sk_hgrunt_health.GetFloat();
	m_iHealth = sk_hgrunt_health.GetFloat();

	// tint hgrunt based on role and apply any role modifiers, for now
	m_iHealCharge = -1;
	switch (m_iSquadRole)
	{
	case HGRUNT_MEDIC:
		SetRenderColor( 128, 255, 128 ); // slightly green
		m_iHealCharge = sk_hgrunt_medic_max_heal_charge.GetInt();
		break;

	case HGRUNT_ENGINEER:
		SetRenderColor( 128, 128, 255 ); // slightly blue
		break;
	}
	m_flLastHealTime = -1;
	m_bWasInPlayerSquad = IsInPlayerSquad();
	m_iszOriginalSquad = m_SquadName;
	m_iFriendlyFireTolerance = 0;
	m_bRemovedFromPlayerSquad = false;
	NPCInit();

	SetUse( &CNPC_HGrunt::CommanderUse );
}

// TODO: remove
#define HGrunt_FOLLOWER_DESERT_FUNCTANK_DIST	45.0f*12.0f
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
			if (vecFollowGoal.DistToSqr( GetAbsOrigin() ) > Square( HGrunt_FOLLOWER_DESERT_FUNCTANK_DIST ))
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

	// If the player is standing near a medic and can see the medic, 
	// assume the player is 'staring' and wants health.
	if (CanHeal())
	{
		CBasePlayer *pPlayer = AI_GetSinglePlayer();

		if (!pPlayer)
		{
			return;
		}

		if (ShouldHealTarget( pPlayer ))
		{
			SetCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER );
		}
		else
		{
			ClearCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER );
		}

#ifdef HL2_EPISODIC
		// Heal if I'm on an assault. The player hasn't had time to stare at me.
		if (m_AssaultBehavior.IsRunning() && IsMoving())
		{
			SetCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER );
		}
#endif
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::PredictPlayerPush()
{
	if (!AI_IsSinglePlayer())
		return;

	if (HasCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER ))
		return;

	bool bHadPlayerPush = HasCondition( COND_PLAYER_PUSHING );

	BaseClass::PredictPlayerPush();

	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if (!bHadPlayerPush && HasCondition( COND_PLAYER_PUSHING ) &&
		pPlayer->FInViewCone( this ) && CanHeal())
	{
		if (ShouldHealTarget( pPlayer ))
		{
			ClearCondition( COND_PLAYER_PUSHING );
			SetCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER );
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

	// reset friendly fire tolerance so subsequent encounters don't trigger hostility
	// todo: time-based tolerance as well
	if (m_iOldNpcState == NPC_STATE_COMBAT && GetState() == NPC_STATE_ALERT || GetState() == NPC_STATE_IDLE)
		m_iFriendlyFireTolerance = 0; 
	m_iOldNpcState = GetState();

	if (IsInPlayerSquad())
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

	// TODO: this might actually be useful
	//if (GetEnemy() && g_ai_HGrunt_show_enemy.GetBool())
	//{
	//	NDebugOverlay::Line( EyePosition(), GetEnemy()->EyePosition(), 255, 0, 0, false, .1 );
	//}

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

	if (IsMedic() && IsCustomInterruptConditionSet( COND_HEAR_MOVE_AWAY ))
	{
		if (!IsCurSchedule( SCHED_RELOAD, false ))
		{
			// Since schedule selection code prioritizes reloading over requests to heal
			// the player, we must prevent this condition from breaking the reload schedule.
			SetCustomInterruptCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER );
		}

		// TODO: command heal is useful
		//SetCustomInterruptCondition( COND_CIT_COMMANDHEAL );
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
			SetCustomInterruptCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER );
		}

		// TODO: command heal is useful
		//SetCustomInterruptCondition( COND_CIT_COMMANDHEAL );
	}
#else
	if (IsMedic() && m_AssaultBehavior.IsRunning() && !IsMoving())
	{
		if (!IsCurSchedule( SCHED_RELOAD, false ))
		{
			SetCustomInterruptCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER );
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
		// has put this off for 10 seconds under the assumption that the HGrunt would be able to 
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
// Determine if HGrunt should perform heal action.
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectScheduleHeal()
{
	if (CanHeal())
	{
		CBaseEntity *pEntity = PlayerInRange( GetLocalOrigin(), HGRUNT_HEAL_RANGE );
		if (pEntity && ShouldHealTarget( pEntity ))
		{
			SetTarget( pEntity );
			return SCHED_HGRUNT_MEDIC_HEAL;
		}

		if (m_pSquad)
		{
			pEntity = NULL;
			float distClosestSq = HGRUNT_HEAL_RANGE*HGRUNT_HEAL_RANGE;
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
				return SCHED_HGRUNT_MEDIC_HEAL;
			}
		}
	}
	else
	{
		if (HasCondition( COND_HGRUNT_MEDIC_HEAL_PLAYER ))
			DevMsg( "Would say: sorry, need to recharge\n" );
	}

	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectScheduleRetrieveItem()
{
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
	return SCHED_NONE;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::SelectScheduleCombat()
{
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

			// TODO: remove this
			//if (flDist < 50 * 12)
			//{
			//	AddSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE );
			//	return SCHED_HGrunt_MOURN_PLAYER;
			//}
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

	// TODO: get rid of this
	case SCHED_RANGE_ATTACK1:
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
	case TASK_HGRUNT_MEDIC_HEAL:
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
		// heal activity goes here
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
		if (IsCurSchedule( SCHED_HGRUNT_MEDIC_HEAL ) && (!GetTarget() || !GetTarget()->IsAlive()))
		{
			TaskFail( FAIL_NO_TARGET );
			return;
		}

		BaseClass::RunTask( pTask );
		break;
	}

	case TASK_HGRUNT_MEDIC_HEAL:
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
			if ((GetTarget()->GetAbsOrigin() - GetAbsOrigin()).Length2D() > HGRUNT_HEAL_RANGE / 2)
				TaskComplete();

			GetMotor()->SetIdealYawToTargetAndUpdate( GetTarget()->GetAbsOrigin() );
		}
		break;

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
	if (IsCurSchedule( SCHED_HGRUNT_MEDIC_HEAL ))
	{
		m_flLastHealTime = gpGlobals->curtime + sk_hgrunt_medic_heal_cooldown.GetFloat() * 0.1f;
	}

	// TODO: remove this
	//if (code == FAIL_NO_ROUTE_BLOCKED && m_bNotifyNavFailBlocked)
	//{
	//	m_OnNavFailBlocked.FireOutput( this, this );
	//}

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

	// !!!HACK - HGrunts don't have the required animations for shotguns, 
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
	// heal animevent

	// footstep animevents

	BaseClass::HandleAnimEvent( pEvent );
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
		if (IsMedic())
		{
			AddHealCharge( sk_hgrunt_medic_heal_amount.GetInt() * 3 );
			if (pItem)
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
		if (IsMedic())
		{
			AddHealCharge( sk_hgrunt_medic_heal_amount.GetInt() );
			if (pItem)
				UTIL_Remove( pItem );
		}
	}
	else
	{
		DevMsg( "HGrunt doesn't know how to pick up %s!\n", pItem->GetClassname() );
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

	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::SimpleUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// Under these conditions, HGrunts will refuse to go with the player.
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

bool CNPC_HGrunt::CanHeal()
{
	if (gpGlobals->curtime < m_flLastHealTime + sk_hgrunt_medic_heal_cooldown.GetFloat())
		return false;

	if (m_iHealCharge <= 0)
		return false;

	return true;
}

void CNPC_HGrunt::Heal( CBaseCombatCharacter *pTarget )
{
	int hpTaken = pTarget->TakeHealth( sk_hgrunt_medic_heal_amount.GetInt(), DMG_GENERIC );
	if (hpTaken == 0)
		Warning( "Heal target was unable to be healed!\n" );
	RemoveHealCharge( hpTaken );
	// play anim and stuff
}

bool CNPC_HGrunt::ShouldHealTarget( CBaseEntity *pTarget )
{
	// heal them only if they're hurt
	if (pTarget->GetHealth() > sk_hgrunt_medic_heal_threshold.GetInt())
		return false;

	// don't let one character hog the heals
	if (pTarget == m_hLastHealTarget)
		return false;

	return true;
}

void CNPC_HGrunt::AddHealCharge( int charge )
{
	if (m_iHealCharge + charge > sk_hgrunt_medic_max_heal_charge.GetInt())
	{
		m_iHealCharge = sk_hgrunt_medic_max_heal_charge.GetInt();
	}
	else
	{
		m_iHealCharge += charge;
	}
}

void CNPC_HGrunt::RemoveHealCharge( int charge )
{
	if (m_iHealCharge - charge < 0)
	{
		m_iHealCharge = 0;
	}
	else
	{
		m_iHealCharge -= charge;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::IsCommandable()
{
	return (!HasSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE ) && IsInPlayerSquad());
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::IsPlayerAlly( CBasePlayer *pPlayer )
{
	// todo: touch this up
	return BaseClass::IsPlayerAlly( pPlayer );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::CanJoinPlayerSquad()
{
	if (!AI_IsSinglePlayer())
		return false;

	if (m_NPCState == NPC_STATE_SCRIPT || m_NPCState == NPC_STATE_PRONE)
		return false;

	if (HasSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE ))
		return false;

	if (IsInAScript())
		return false;

	// Don't bother people who don't want to be bothered
	if (!CanBeUsedAsAFriend())
		return false;

	if (IRelationType( UTIL_GetLocalPlayer() ) != D_LI)
		return false;

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::WasInPlayerSquad()
{
	return m_bWasInPlayerSquad;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::HaveCommandGoal() const
{
	if (GetCommandGoal() != vec3_invalid)
		return true;
	return false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::IsCommandMoving()
{
	if (AI_IsSinglePlayer() && IsInPlayerSquad())
	{
		if (m_FollowBehavior.GetFollowTarget() == UTIL_GetLocalPlayer() ||
			IsFollowingCommandPoint())
		{
			return (m_FollowBehavior.IsMovingToFollowTarget());
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::ShouldAutoSummon()
{
	if (!AI_IsSinglePlayer() || !IsFollowingCommandPoint() || !IsInPlayerSquad())
		return false;

	CHL2_Player *pPlayer = (CHL2_Player *)UTIL_GetLocalPlayer();

	float distMovedSq = (pPlayer->GetAbsOrigin() - m_vAutoSummonAnchor).LengthSqr();
	float moveTolerance = player_squad_autosummon_move_tolerance.GetFloat() * 12;
	const Vector &vCommandGoal = GetCommandGoal();

	if (distMovedSq < Square( moveTolerance * 10 ) && (GetAbsOrigin() - vCommandGoal).LengthSqr() > Square( 10 * 12 ) && IsCommandMoving())
	{
		m_AutoSummonTimer.Set( player_squad_autosummon_time.GetFloat() );
		if (player_squad_autosummon_debug.GetBool())
			DevMsg( "Waiting for arrival before initiating autosummon logic\n" );
	}
	else if (m_AutoSummonTimer.Expired())
	{
		bool bSetFollow = false;
		bool bTestEnemies = true;

		// Auto summon unconditionally if a significant amount of time has passed
		if (gpGlobals->curtime - m_AutoSummonTimer.GetNext() > player_squad_autosummon_time.GetFloat() * 2)
		{
			bSetFollow = true;
			if (player_squad_autosummon_debug.GetBool())
				DevMsg( "Auto summoning squad: long time (%f)\n", (gpGlobals->curtime - m_AutoSummonTimer.GetNext()) + player_squad_autosummon_time.GetFloat() );
		}

		// Player must move for autosummon
		if (distMovedSq > Square( 12 ))
		{
			bool bCommandPointIsVisible = pPlayer->FVisible( vCommandGoal + pPlayer->GetViewOffset() );

			// Auto summon if the player is close by the command point
			if (!bSetFollow && bCommandPointIsVisible && distMovedSq > Square( 24 ))
			{
				float closenessTolerance = player_squad_autosummon_player_tolerance.GetFloat() * 12;
				if ((pPlayer->GetAbsOrigin() - vCommandGoal).LengthSqr() < Square( closenessTolerance ) &&
					((m_vAutoSummonAnchor - vCommandGoal).LengthSqr() > Square( closenessTolerance )))
				{
					bSetFollow = true;
					if (player_squad_autosummon_debug.GetBool())
						DevMsg( "Auto summoning squad: player close to command point (%f)\n", (GetAbsOrigin() - vCommandGoal).Length() );
				}
			}

			// Auto summon if moved a moderate distance and can't see command point, or moved a great distance
			if (!bSetFollow)
			{
				if (distMovedSq > Square( moveTolerance * 2 ))
				{
					bSetFollow = true;
					bTestEnemies = (distMovedSq < Square( moveTolerance * 10 ));
					if (player_squad_autosummon_debug.GetBool())
						DevMsg( "Auto summoning squad: player very far from anchor (%f)\n", sqrt( distMovedSq ) );
				}
				else if (distMovedSq > Square( moveTolerance ))
				{
					if (!bCommandPointIsVisible)
					{
						bSetFollow = true;
						if (player_squad_autosummon_debug.GetBool())
							DevMsg( "Auto summoning squad: player far from anchor (%f)\n", sqrt( distMovedSq ) );
					}
				}
			}
		}

		// Auto summon only if there are no readily apparent enemies
		if (bSetFollow && bTestEnemies)
		{
			for (int i = 0; i < g_AI_Manager.NumAIs(); i++)
			{
				CAI_BaseNPC *pNpc = g_AI_Manager.AccessAIs()[i];
				float timeSinceCombatTolerance = player_squad_autosummon_time_after_combat.GetFloat();

				if (pNpc->IsInPlayerSquad())
				{
					if (gpGlobals->curtime - pNpc->GetLastAttackTime() > timeSinceCombatTolerance ||
						gpGlobals->curtime - pNpc->GetLastDamageTime() > timeSinceCombatTolerance)
						continue;
				}
				else if (pNpc->GetEnemy())
				{
					CBaseEntity *pNpcEnemy = pNpc->GetEnemy();
					if (!IsSniper( pNpc ) && (gpGlobals->curtime - pNpc->GetEnemyLastTimeSeen()) > timeSinceCombatTolerance)
						continue;

					if (pNpcEnemy == pPlayer)
					{
						if (pNpc->CanBeAnEnemyOf( pPlayer ))
						{
							bSetFollow = false;
							break;
						}
					}
					else if (pNpcEnemy->IsNPC() && (pNpcEnemy->MyNPCPointer()->GetSquad() == GetSquad() || pNpcEnemy->Classify() == CLASS_PLAYER_ALLY_VITAL))
					{
						if (pNpc->CanBeAnEnemyOf( this ))
						{
							bSetFollow = false;
							break;
						}
					}
				}
			}
			if (!bSetFollow && player_squad_autosummon_debug.GetBool())
				DevMsg( "Auto summon REVOKED: Combat recent \n" );
		}

		return bSetFollow;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Is this entity something that the HGrunt should interact with (return true)
// or something that he should try to get close to (return false)
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::IsValidCommandTarget( CBaseEntity *pTarget )
{
	return false;
}

//-----------------------------------------------------------------------------
bool CNPC_HGrunt::SpeakCommandResponse( AIConcept_t concept, const char *modifiers )
{
	return SpeakIfAllowed( concept,
		CFmtStr( "numselected:%d,"
		"useradio:%d%s",
		(GetSquad()) ? GetSquad()->NumMembers() : 1,
		ShouldSpeakRadio( AI_GetSinglePlayer() ),
		(modifiers) ? CFmtStr( ",%s", modifiers ).operator const char *() : "" ) );
}

//-----------------------------------------------------------------------------
// Purpose: return TRUE if the commander mode should try to give this order
//			to more people. return FALSE otherwise. For instance, we don't
//			try to send all 3 selectedHGrunts to pick up the same gun.
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::TargetOrder( CBaseEntity *pTarget, CAI_BaseNPC **Allies, int numAllies )
{
	if (pTarget->IsPlayer())
	{
		// I'm the target! Toggle follow!
		if (m_FollowBehavior.GetFollowTarget() != pTarget)
		{
			ClearFollowTarget();
			SetCommandGoal( vec3_invalid );

			// Turn follow on!
			m_AssaultBehavior.Disable();
			m_FollowBehavior.SetFollowTarget( pTarget );
			m_FollowBehavior.SetParameters( AIF_SIMPLE );
			SpeakCommandResponse( TLK_STARTFOLLOW );

			// todo: outputs
			//m_OnFollowOrder.FireOutput( this, this );
		}
		else if (m_FollowBehavior.GetFollowTarget() == pTarget)
		{
			// Stop following.
			m_FollowBehavior.SetFollowTarget( NULL );
			SpeakCommandResponse( TLK_STOPFOLLOW );
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Turn off following before processing a move order.
//-----------------------------------------------------------------------------
void CNPC_HGrunt::MoveOrder( const Vector &vecDest, CAI_BaseNPC **Allies, int numAllies )
{
	if (!AI_IsSinglePlayer())
		return;

	CHL2_Player *pPlayer = (CHL2_Player *)UTIL_GetLocalPlayer();

	// TODO: autosummon
	/*m_AutoSummonTimer.Set( player_squad_autosummon_time.GetFloat() );
	m_vAutoSummonAnchor = pPlayer->GetAbsOrigin();*/

	if (m_StandoffBehavior.IsRunning())
	{
		m_StandoffBehavior.SetStandoffGoalPosition( vecDest );
	}

	// If in assault, cancel and move.
	if (m_AssaultBehavior.HasHitRallyPoint() && !m_AssaultBehavior.HasHitAssaultPoint())
	{
		m_AssaultBehavior.Disable();
		ClearSchedule( "Moving from rally point to assault point" );
	}

	bool spoke = false;

	CAI_BaseNPC *pClosest = NULL;
	float closestDistSq = FLT_MAX;

	for (int i = 0; i < numAllies; i++)
	{
		if (Allies[i]->IsInPlayerSquad())
		{
			Assert( Allies[i]->IsCommandable() );
			float distSq = (pPlayer->GetAbsOrigin() - Allies[i]->GetAbsOrigin()).LengthSqr();
			if (distSq < closestDistSq)
			{
				pClosest = Allies[i];
				closestDistSq = distSq;
			}
		}
	}

	if (m_FollowBehavior.GetFollowTarget() && !IsFollowingCommandPoint())
	{
		ClearFollowTarget();
#if 0
		if ((pPlayer->GetAbsOrigin() - GetAbsOrigin()).LengthSqr() < Square( 180 ) &&
			((vecDest - pPlayer->GetAbsOrigin()).LengthSqr() < Square( 120 ) ||
			(vecDest - GetAbsOrigin()).LengthSqr() < Square( 120 )))
		{
			if (pClosest == this)
				SpeakIfAllowed( TLK_STOPFOLLOW );
			spoke = true;
		}
#endif
	}

	if (!spoke && pClosest == this)
	{
		float destDistToPlayer = (vecDest - pPlayer->GetAbsOrigin()).Length();
		float destDistToClosest = (vecDest - GetAbsOrigin()).Length();
		CFmtStr modifiers( "commandpoint_dist_to_player:%.0f,"
			"commandpoint_dist_to_npc:%.0f",
			destDistToPlayer,
			destDistToClosest );

		SpeakCommandResponse( TLK_COMMANDED, modifiers );
	}

	// todo: outputs
	//m_OnStationOrder.FireOutput( this, this );

	BaseClass::MoveOrder( vecDest, Allies, numAllies );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::OnMoveOrder()
{
	SetReadinessLevel( AIRL_STIMULATED, false, false );
	BaseClass::OnMoveOrder();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::CommanderUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// todo: outputs
	//m_OnPlayerUse.FireOutput( pActivator, pCaller );

	// Under these conditions, HGrunts will refuse to go with the player.
	// Robin: NPCs should always respond to +USE even if someone else has the semaphore.
	if (!AI_IsSinglePlayer() || !CanJoinPlayerSquad())
	{
		SimpleUse( pActivator, pCaller, useType, value );
		return;
	}

	if (pActivator == UTIL_GetLocalPlayer())
	{
		// Don't say hi after you've been addressed by the player
		SetSpokeConcept( TLK_HELLO, NULL );

		// todo: this
		if (!ShouldAutosquad())
			TogglePlayerSquadState();
		else if (!IsInPlayerSquad())
			AddToPlayerSquad();
		else if (IsInPlayerSquad())
		{
			m_bRemovedFromPlayerSquad = true;
			RemoveFromPlayerSquad();
		}
			
		//else if (GetCurSchedule() && ConditionInterruptsCurSchedule( COND_IDLE_INTERRUPT ))
		//{
		//	if (SpeakIfAllowed( TLK_QUESTION, NULL, true ))
		//	{
		//		if (random->RandomInt( 1, 4 ) < 4)
		//		{
		//			CBaseEntity *pRespondant = FindSpeechTarget( AIST_NPCS );
		//			if (pRespondant)
		//			{
		//				g_EventQueue.AddEvent( pRespondant, "SpeakIdleResponse", (GetTimeSpeechComplete() - gpGlobals->curtime) + .2, this, this );
		//			}
		//		}
		//	}
		//}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::ShouldSpeakRadio( CBaseEntity *pListener )
{
	if (!pListener)
		return false;

	const float		radioRange = 384 * 384;
	Vector			vecDiff;

	vecDiff = WorldSpaceCenter() - pListener->WorldSpaceCenter();

	if (vecDiff.LengthSqr() > radioRange)
	{
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::OnMoveToCommandGoalFailed()
{
	// Clear the goal.
	SetCommandGoal( vec3_invalid );

	// Announce failure.
	SpeakCommandResponse( TLK_COMMAND_FAILED );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::AddToPlayerSquad()
{
	Assert( !IsInPlayerSquad() );
	Warning( "Joining squad!\n" );
	AddToSquad( AllocPooledString( PLAYER_SQUADNAME ) );
	m_hSavedFollowGoalEnt = m_FollowBehavior.GetFollowGoal();
	m_FollowBehavior.SetFollowGoalDirect( NULL );

	FixupPlayerSquad();

	SetCondition( COND_PLAYER_ADDED_TO_SQUAD );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::RemoveFromPlayerSquad()
{
	Assert( IsInPlayerSquad() );
	Warning( "Leaving squad!\n" );
	ClearFollowTarget();
	ClearCommandGoal();
	if (m_iszOriginalSquad != NULL_STRING && strcmp( STRING( m_iszOriginalSquad ), PLAYER_SQUADNAME ) != 0)
		AddToSquad( m_iszOriginalSquad );
	else
		RemoveFromSquad();

	if (m_hSavedFollowGoalEnt)
		m_FollowBehavior.SetFollowGoal( m_hSavedFollowGoalEnt );

	SetCondition( COND_PLAYER_REMOVED_FROM_SQUAD );

	// Don't evaluate the player squad for 2 seconds. 
	gm_PlayerSquadEvaluateTimer.Set( 2.0 );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::TogglePlayerSquadState()
{
	if (!AI_IsSinglePlayer())
		return;

	if (!IsInPlayerSquad())
	{
		AddToPlayerSquad();

		// todo: speech
		/*if (HaveCommandGoal())
		{
			SpeakCommandResponse( TLK_COMMANDED );
		}
		else if (m_FollowBehavior.GetFollowTarget() == UTIL_GetLocalPlayer())
		{
			SpeakCommandResponse( TLK_STARTFOLLOW );
		}*/
	}
	else
	{
		SpeakCommandResponse( TLK_STOPFOLLOW );
		m_bRemovedFromPlayerSquad = true;
		RemoveFromPlayerSquad();
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
struct SquadCandidateHGrunt_t
{
	CNPC_HGrunt *pHGrunt;
	bool		  bIsInSquad;
	float		  distSq;
	int			  iSquadIndex;
};

void CNPC_HGrunt::UpdatePlayerSquad()
{
	if (!AI_IsSinglePlayer())
		return;

	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ((pPlayer->GetAbsOrigin().AsVector2D() - GetAbsOrigin().AsVector2D()).LengthSqr() < Square( 20 * 12 ))
		m_flTimeLastCloseToPlayer = gpGlobals->curtime;

	if (!gm_PlayerSquadEvaluateTimer.Expired())
		return;

	gm_PlayerSquadEvaluateTimer.Set( 2.0 );

	// Remove stragglers
	CAI_Squad *pPlayerSquad = g_AI_SquadManager.FindSquad( MAKE_STRING( PLAYER_SQUADNAME ) );
	if (pPlayerSquad)
	{
		CUtlVectorFixed<CNPC_HGrunt *, MAX_PLAYER_SQUAD> squadMembersToRemove;
		AISquadIter_t iter;

		for (CAI_BaseNPC *pPlayerSquadMember = pPlayerSquad->GetFirstMember( &iter ); pPlayerSquadMember; pPlayerSquadMember = pPlayerSquad->GetNextMember( &iter ))
		{
			if (pPlayerSquadMember->GetClassname() != GetClassname())
				continue;

			CNPC_HGrunt *pHGrunt = assert_cast<CNPC_HGrunt *>(pPlayerSquadMember);

			if (!pHGrunt->m_bNeverLeavePlayerSquad &&
				pHGrunt->m_FollowBehavior.GetFollowTarget() &&
				!pHGrunt->m_FollowBehavior.FollowTargetVisible() &&
				pHGrunt->m_FollowBehavior.GetNumFailedFollowAttempts() > 0 &&
				gpGlobals->curtime - pHGrunt->m_FollowBehavior.GetTimeFailFollowStarted() > 20 &&
				(fabsf( (pHGrunt->m_FollowBehavior.GetFollowTarget()->GetAbsOrigin().z - pHGrunt->GetAbsOrigin().z) ) > 196 ||
				(pHGrunt->m_FollowBehavior.GetFollowTarget()->GetAbsOrigin().AsVector2D() - pHGrunt->GetAbsOrigin().AsVector2D()).LengthSqr() > Square( 50 * 12 )))
			{
				if (DebuggingCommanderMode())
				{
					DevMsg( "Player follower is lost (%d, %f, %d)\n",
						pHGrunt->m_FollowBehavior.GetNumFailedFollowAttempts(),
						gpGlobals->curtime - pHGrunt->m_FollowBehavior.GetTimeFailFollowStarted(),
						(int)((pHGrunt->m_FollowBehavior.GetFollowTarget()->GetAbsOrigin().AsVector2D() - pHGrunt->GetAbsOrigin().AsVector2D()).Length()) );
				}

				squadMembersToRemove.AddToTail( pHGrunt );
			}
		}

		for (int i = 0; i < squadMembersToRemove.Count(); i++)
		{
			squadMembersToRemove[i]->RemoveFromPlayerSquad();
		}
	}

	// Autosquadding
	const float JOIN_PLAYER_XY_TOLERANCE_SQ = Square( 36 * 12 );
	const float UNCONDITIONAL_JOIN_PLAYER_XY_TOLERANCE_SQ = Square( 12 * 12 );
	const float UNCONDITIONAL_JOIN_PLAYER_Z_TOLERANCE = 5 * 12;
	const float SECOND_TIER_JOIN_DIST_SQ = Square( 48 * 12 );
	if (pPlayer && ShouldAutosquad() && !(pPlayer->GetFlags() & FL_NOTARGET) && pPlayer->IsAlive())
	{
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		CUtlVector<SquadCandidateHGrunt_t> candidates;
		const Vector &vPlayerPos = pPlayer->GetAbsOrigin();
		bool bFoundNewGuy = false;
		int i;

		for (i = 0; i < g_AI_Manager.NumAIs(); i++)
		{
			if (ppAIs[i]->GetState() == NPC_STATE_DEAD)
				continue;

			if (ppAIs[i]->GetClassname() != GetClassname())
				continue;

			CNPC_HGrunt *pHGrunt = assert_cast<CNPC_HGrunt *>(ppAIs[i]);
			int iNew;

			if (pHGrunt->IsInPlayerSquad())
			{
				iNew = candidates.AddToTail();
				candidates[iNew].pHGrunt = pHGrunt;
				candidates[iNew].bIsInSquad = true;
				candidates[iNew].distSq = 0;
				candidates[iNew].iSquadIndex = pHGrunt->GetSquad()->GetSquadIndex( pHGrunt );
			}
			else
			{
				float distSq = (vPlayerPos.AsVector2D() - pHGrunt->GetAbsOrigin().AsVector2D()).LengthSqr();
				if (distSq > JOIN_PLAYER_XY_TOLERANCE_SQ &&
					(pHGrunt->m_flTimeJoinedPlayerSquad == 0 || gpGlobals->curtime - pHGrunt->m_flTimeJoinedPlayerSquad > 60.0) &&
					(pHGrunt->m_flTimeLastCloseToPlayer == 0 || gpGlobals->curtime - pHGrunt->m_flTimeLastCloseToPlayer > 15.0))
					continue;

				if (!pHGrunt->CanJoinPlayerSquad())
					continue;

				bool bShouldAdd = false;

				if (pHGrunt->HasCondition( COND_SEE_PLAYER ))
					bShouldAdd = true;
				else
				{
					bool bPlayerVisible = pHGrunt->FVisible( pPlayer );
					if (bPlayerVisible)
					{
						if (pHGrunt->HasCondition( COND_HEAR_PLAYER ))
							bShouldAdd = true;
						else if (distSq < UNCONDITIONAL_JOIN_PLAYER_XY_TOLERANCE_SQ && fabsf( vPlayerPos.z - pHGrunt->GetAbsOrigin().z ) < UNCONDITIONAL_JOIN_PLAYER_Z_TOLERANCE)
							bShouldAdd = true;
					}
				}

				if (bShouldAdd)
				{
					// @TODO (toml 05-25-04): probably everyone in a squad should be a candidate if one of them sees the player
					AI_Waypoint_t *pPathToPlayer = pHGrunt->GetPathfinder()->BuildRoute( pHGrunt->GetAbsOrigin(), vPlayerPos, pPlayer, 5 * 12, NAV_NONE, true );
					GetPathfinder()->UnlockRouteNodes( pPathToPlayer );

					if (!pPathToPlayer)
						continue;

					CAI_Path tempPath;
					tempPath.SetWaypoints( pPathToPlayer ); // path object will delete waypoints

					iNew = candidates.AddToTail();
					candidates[iNew].pHGrunt = pHGrunt;
					candidates[iNew].bIsInSquad = false;
					candidates[iNew].distSq = distSq;
					candidates[iNew].iSquadIndex = -1;

					bFoundNewGuy = true;
				}
			}
		}

		if (bFoundNewGuy)
		{
			// Look for second order guys
			int initialCount = candidates.Count();
			for (i = 0; i < initialCount; i++)
				candidates[i].pHGrunt->AddSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE ); // Prevents double-add
			for (i = 0; i < initialCount; i++)
			{
				if (candidates[i].iSquadIndex == -1)
				{
					for (int j = 0; j < g_AI_Manager.NumAIs(); j++)
					{
						if (ppAIs[j]->GetState() == NPC_STATE_DEAD)
							continue;

						if (ppAIs[j]->GetClassname() != GetClassname())
							continue;

						if (ppAIs[j]->HasSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE ))
							continue;

						CNPC_HGrunt *pHGrunt = assert_cast<CNPC_HGrunt *>(ppAIs[j]);

						float distSq = (vPlayerPos - pHGrunt->GetAbsOrigin()).Length2DSqr();
						if (distSq > JOIN_PLAYER_XY_TOLERANCE_SQ)
							continue;

						distSq = (candidates[i].pHGrunt->GetAbsOrigin() - pHGrunt->GetAbsOrigin()).Length2DSqr();
						if (distSq > SECOND_TIER_JOIN_DIST_SQ)
							continue;

						if (!pHGrunt->CanJoinPlayerSquad())
							continue;

						if (!pHGrunt->FVisible( pPlayer ))
							continue;

						int iNew = candidates.AddToTail();
						candidates[iNew].pHGrunt = pHGrunt;
						candidates[iNew].bIsInSquad = false;
						candidates[iNew].distSq = distSq;
						candidates[iNew].iSquadIndex = -1;
						pHGrunt->AddSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE ); // Prevents double-add
					}
				}
			}
			for (i = 0; i < candidates.Count(); i++)
				candidates[i].pHGrunt->RemoveSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE );

			if (candidates.Count() > MAX_PLAYER_SQUAD)
			{
				candidates.Sort( PlayerSquadCandidateSortFunc );

				for (i = MAX_PLAYER_SQUAD; i < candidates.Count(); i++)
				{
					if (candidates[i].pHGrunt->IsInPlayerSquad())
					{
						candidates[i].pHGrunt->RemoveFromPlayerSquad();
					}
				}
			}

			if (candidates.Count())
			{
				CNPC_HGrunt *pClosest = NULL;
				float closestDistSq = FLT_MAX;
				int nJoined = 0;

				for (i = 0; i < candidates.Count() && i < MAX_PLAYER_SQUAD; i++)
				{
					if (!candidates[i].pHGrunt->IsInPlayerSquad())
					{
						candidates[i].pHGrunt->AddToPlayerSquad();
						nJoined++;

						if (candidates[i].distSq < closestDistSq)
						{
							pClosest = candidates[i].pHGrunt;
							closestDistSq = candidates[i].distSq;
						}
					}
				}

				// todo: speaking
				//if (pClosest)
				//{
				//	if (!pClosest->SpokeConcept( TLK_JOINPLAYER ))
				//	{
				//		pClosest->SpeakCommandResponse( TLK_JOINPLAYER, CFmtStr( "numjoining:%d", nJoined ) );
				//	}
				//	else
				//	{
				//		pClosest->SpeakCommandResponse( TLK_STARTFOLLOW );
				//	}

				//	for (i = 0; i < candidates.Count() && i < MAX_PLAYER_SQUAD; i++)
				//	{
				//		candidates[i].pHGrunt->SetSpokeConcept( TLK_JOINPLAYER, NULL );
				//	}
				//}
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_HGrunt::PlayerSquadCandidateSortFunc( const SquadCandidateHGrunt_t *pLeft, const SquadCandidateHGrunt_t *pRight )
{
	// "Bigger" means less approprate 
	CNPC_HGrunt *pLeftHGrunt = pLeft->pHGrunt;
	CNPC_HGrunt *pRightHGrunt = pRight->pHGrunt;

	// Medics are better than anyone
	if (pLeftHGrunt->IsMedic() && !pRightHGrunt->IsMedic())
		return -1;

	if (!pLeftHGrunt->IsMedic() && pRightHGrunt->IsMedic())
		return 1;

	// engineers are better than regular hgrunts
	if (pLeftHGrunt->IsEngineer() && !pRightHGrunt->IsEngineer())
		return -1;

	if (!pLeftHGrunt->IsEngineer() && pRightHGrunt->IsEngineer())
		return 1;

	CBaseCombatWeapon *pLeftWeapon = pLeftHGrunt->GetActiveWeapon();
	CBaseCombatWeapon *pRightWeapon = pRightHGrunt->GetActiveWeapon();

	// People with weapons are better than those without
	if (pLeftWeapon && !pRightWeapon)
		return -1;

	if (!pLeftWeapon && pRightWeapon)
		return 1;

	// Existing squad members are better than non-members
	if (pLeft->bIsInSquad && !pRight->bIsInSquad)
		return -1;

	if (!pLeft->bIsInSquad && pRight->bIsInSquad)
		return 1;

	// New squad members are better than older ones
	if (pLeft->bIsInSquad && pRight->bIsInSquad)
		return pRight->iSquadIndex - pLeft->iSquadIndex;

	// Finally, just take the closer
	return (int)(pRight->distSq - pLeft->distSq);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::FixupPlayerSquad()
{
	if (!AI_IsSinglePlayer())
		return;

	m_flTimeJoinedPlayerSquad = gpGlobals->curtime;
	m_bWasInPlayerSquad = true;
	if (m_pSquad->NumMembers() > MAX_PLAYER_SQUAD)
	{
		CAI_BaseNPC *pFirstMember = m_pSquad->GetFirstMember( NULL );
		m_pSquad->RemoveFromSquad( pFirstMember );
		pFirstMember->ClearCommandGoal();

		CNPC_HGrunt *pFirstMemberHGrunt = dynamic_cast< CNPC_HGrunt * >(pFirstMember);
		if (pFirstMemberHGrunt)
		{
			pFirstMemberHGrunt->ClearFollowTarget();
		}
		else
		{
			CAI_FollowBehavior *pOldMemberFollowBehavior;
			if (pFirstMember->GetBehavior( &pOldMemberFollowBehavior ))
			{
				pOldMemberFollowBehavior->SetFollowTarget( NULL );
			}
		}
	}

	ClearFollowTarget();

	CAI_BaseNPC *pLeader = NULL;
	AISquadIter_t iter;
	for (CAI_BaseNPC *pAllyNpc = m_pSquad->GetFirstMember( &iter ); pAllyNpc; pAllyNpc = m_pSquad->GetNextMember( &iter ))
	{
		if (pAllyNpc->IsCommandable())
		{
			pLeader = pAllyNpc;
			break;
		}
	}

	if (pLeader && pLeader != this)
	{
		const Vector &commandGoal = pLeader->GetCommandGoal();
		if (commandGoal != vec3_invalid)
		{
			SetCommandGoal( commandGoal );
			SetCondition( COND_RECEIVED_ORDERS );
			OnMoveOrder();
		}
		else
		{
			CAI_FollowBehavior *pLeaderFollowBehavior;
			if (pLeader->GetBehavior( &pLeaderFollowBehavior ))
			{
				m_FollowBehavior.SetFollowTarget( pLeaderFollowBehavior->GetFollowTarget() );
				m_FollowBehavior.SetParameters( m_FollowBehavior.GetFormation() );
			}

		}
	}
	else
	{
		m_FollowBehavior.SetFollowTarget( UTIL_GetLocalPlayer() );
		m_FollowBehavior.SetParameters( AIF_SIMPLE );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::ClearFollowTarget()
{
	m_FollowBehavior.SetFollowTarget( NULL );
	m_FollowBehavior.SetParameters( AIF_SIMPLE );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::UpdateFollowCommandPoint()
{
	if (!AI_IsSinglePlayer())
		return;

	if (IsInPlayerSquad())
	{
		if (HaveCommandGoal())
		{
			CBaseEntity *pFollowTarget = m_FollowBehavior.GetFollowTarget();
			CBaseEntity *pCommandPoint = gEntList.FindEntityByClassname( NULL, COMMAND_POINT_CLASSNAME );

			if (!pCommandPoint)
			{
				DevMsg( "**\nVERY BAD THING\nCommand point vanished! Creating a new one\n**\n" );
				pCommandPoint = CreateEntityByName( COMMAND_POINT_CLASSNAME );
			}

			if (pFollowTarget != pCommandPoint)
			{
				pFollowTarget = pCommandPoint;
				m_FollowBehavior.SetFollowTarget( pFollowTarget );
				m_FollowBehavior.SetParameters( AIF_COMMANDER );
			}

			if ((pCommandPoint->GetAbsOrigin() - GetCommandGoal()).LengthSqr() > 0.01)
			{
				UTIL_SetOrigin( pCommandPoint, GetCommandGoal(), false );
			}
		}
		else
		{
			if (IsFollowingCommandPoint())
				ClearFollowTarget();
			if (m_FollowBehavior.GetFollowTarget() != UTIL_GetLocalPlayer())
			{
				DevMsg( "Expected to be following player, but not\n" );
				m_FollowBehavior.SetFollowTarget( UTIL_GetLocalPlayer() );
				m_FollowBehavior.SetParameters( AIF_SIMPLE );
			}
		}
	}
	else if (IsFollowingCommandPoint())
		ClearFollowTarget();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNPC_HGrunt::IsFollowingCommandPoint()
{
	CBaseEntity *pFollowTarget = m_FollowBehavior.GetFollowTarget();
	if (pFollowTarget)
		return FClassnameIs( pFollowTarget, COMMAND_POINT_CLASSNAME );
	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
struct HGruntSquadMemberInfo_t
{
	CNPC_HGrunt *	pMember;
	bool			bSeesPlayer;
	float			distSq;
};

int __cdecl HGruntSquadSortFunc( const HGruntSquadMemberInfo_t *pLeft, const HGruntSquadMemberInfo_t *pRight )
{
	if (pLeft->bSeesPlayer && !pRight->bSeesPlayer)
	{
		return -1;
	}

	if (!pLeft->bSeesPlayer && pRight->bSeesPlayer)
	{
		return 1;
	}

	return (pLeft->distSq - pRight->distSq);
}

CAI_BaseNPC *CNPC_HGrunt::GetSquadCommandRepresentative()
{
	if (!AI_IsSinglePlayer())
		return NULL;

	if (IsInPlayerSquad())
	{
		static float lastTime;
		static AIHANDLE hCurrent;

		if (gpGlobals->curtime - lastTime > 2.0 || !hCurrent || !hCurrent->IsInPlayerSquad()) // hCurrent will be NULL after level change
		{
			lastTime = gpGlobals->curtime;
			hCurrent = NULL;

			CUtlVectorFixed<HGruntSquadMemberInfo_t, MAX_SQUAD_MEMBERS> candidates;
			CBasePlayer *pPlayer = UTIL_GetLocalPlayer();

			if (pPlayer)
			{
				AISquadIter_t iter;
				for (CAI_BaseNPC *pAllyNpc = m_pSquad->GetFirstMember( &iter ); pAllyNpc; pAllyNpc = m_pSquad->GetNextMember( &iter ))
				{
					if (pAllyNpc->IsCommandable() && dynamic_cast<CNPC_HGrunt *>(pAllyNpc))
					{
						int i = candidates.AddToTail();
						candidates[i].pMember = (CNPC_HGrunt *)(pAllyNpc);
						candidates[i].bSeesPlayer = pAllyNpc->HasCondition( COND_SEE_PLAYER );
						candidates[i].distSq = (pAllyNpc->GetAbsOrigin() - pPlayer->GetAbsOrigin()).LengthSqr();
					}
				}

				if (candidates.Count() > 0)
				{
					candidates.Sort( HGruntSquadSortFunc );
					hCurrent = candidates[0].pMember;
				}
			}
		}

		if (hCurrent != NULL)
		{
			Assert( dynamic_cast<CNPC_HGrunt *>(hCurrent.Get()) && hCurrent->IsInPlayerSquad() );
			return hCurrent;
		}
	}
	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_HGrunt::SetSquad( CAI_Squad *pSquad )
{
	bool bWasInPlayerSquad = IsInPlayerSquad();

	BaseClass::SetSquad( pSquad );

	if (IsInPlayerSquad() && !bWasInPlayerSquad)
	{
		// todo: outputs
		//m_OnJoinedPlayerSquad.FireOutput( this, this );
	}
	else if (!IsInPlayerSquad() && bWasInPlayerSquad)
	{
		// todo: outputs
		//m_OnLeftPlayerSquad.FireOutput( this, this );
	}
}

bool CNPC_HGrunt::PassesDamageFilter( const CTakeDamageInfo &info )
{
	// take all damage from the player
	if (info.GetAttacker() == AI_GetSinglePlayer())
		return true;

	return BaseClass::PassesDamageFilter( info );
}

int CNPC_HGrunt::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	// handle how the hgrunt reacts to damage from the player
	// todo: indirect crowbar, fatal kills under any circumstance, more tolerance i think
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if (info.GetAttacker() == pPlayer && IRelationType(UTIL_GetLocalPlayer()) == D_LI)
	{
		bool bHatePlayer = false;
		if (info.GetDamage() >= GetHealth())
		{
			// first, assume any fatal hit is intentional friendly fire
			bHatePlayer = true;
		}
		else if (info.GetDamageType() == DMG_BLAST)
		{
			// then, handle nearby explosions. if we take too much damage from the explosion assume it's intentional friendly fire
			if (info.GetDamage() >= GetMaxHealth() * 0.5f)
			{
				bHatePlayer = true;
			}
			else
			{
				m_iFriendlyFireTolerance++;
			}
		}
		else
		{
			// then, handle any other types of damage
			if (GetState() == NPC_STATE_IDLE || GetState() == NPC_STATE_ALERT)
			{
				// if the player attacks me here, assume that they're intentionally killing me, and defend myself
				bHatePlayer = true;
			}
			else if (GetState() == NPC_STATE_COMBAT)
			{
				// otherwise, assume it was probably unintentional friendly fire.
				m_iFriendlyFireTolerance++;
			}
			else
			{
				// we'll probably hit this at some point so i'd like to keep a case here
				// todo: fix
			}
		}
		
		if (!bHatePlayer && m_iFriendlyFireTolerance < FRIENDLY_FIRE_TOLERANCE_LIMIT)
		{
			Warning( "Don't do that again!\n" );
		}
		else if (m_iFriendlyFireTolerance >= FRIENDLY_FIRE_TOLERANCE_LIMIT)
		{
			bHatePlayer = true;
		}

		if (bHatePlayer)
		{
			// turn the npc and their squad hostile
			AddEntityRelationship( pPlayer, D_HT, 1 );
			AddSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE );
			Warning( "HGrunt hates the player!\n" );
			
			if (IsInSquad())
			{
				AISquadIter_t iter;
				for (CAI_BaseNPC *pAllyNpc = m_pSquad->GetFirstMember( &iter ); pAllyNpc; pAllyNpc = m_pSquad->GetNextMember( &iter ))
				{
					if (pAllyNpc != this)
					{
						pAllyNpc->AddEntityRelationship( pPlayer, D_HT, 1 );
						pAllyNpc->AddSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE );
						Warning( "HGrunt hates the player!\n" );
					}
				}
			}
			else
			{
				// if our npc is squadless then just trigger whoever's nearby
				CBaseEntity *pEntity = NULL;

				for (CEntitySphereQuery sphere( GetAbsOrigin(), 512, 0 ); (pEntity = sphere.GetCurrentEntity()) != NULL; sphere.NextEntity())
				{
					if (pEntity != this)
					{
						CNPC_HGrunt *pHGrunt = dynamic_cast<CNPC_HGrunt *>(pEntity);
						if (pHGrunt)
						{
							pHGrunt->AddEntityRelationship( pPlayer, D_HT, 1 );
							pHGrunt->AddSpawnFlags( SF_HGRUNT_NOT_COMMANDABLE );
							Warning( "HGrunt hates the player!\n" );
						}
					}
				}
			}
		}
	}

	return BaseClass::OnTakeDamage_Alive( info );
}

// schedules
AI_BEGIN_CUSTOM_NPC(npc_hgrunt, CNPC_HGrunt)
DECLARE_TASK(TASK_HGRUNT_MEDIC_HEAL)
AI_END_CUSTOM_NPC()