//============= Copyleft arbabf, all rights not reserved. ================
//
// Purpose: Human grunts, who shoot bullets, heal things, and torch
// doors.
//
//========================================================================

#include "npc_playercompanion.h"

struct SquadCandidateHGrunt_t;

enum HGruntType_t
{
	HGRUNT_GENERIC,
	HGRUNT_MEDIC,
	HGRUNT_ENGINEER
};

//----------------------------
// spawnflags
//----------------------------
#define SF_HGRUNT_JOIN_WHEN_NEARBY (1 << 16)
#define SF_HGRUNT_NOT_COMMANDABLE (1 << 17)

class CNPC_HGrunt : public CNPC_PlayerCompanion
{
	DECLARE_CLASS( CNPC_HGrunt, CNPC_PlayerCompanion );
	DECLARE_DATADESC();

public:
	virtual void	Spawn();
	virtual void	PostNPCInit();
	void			OnRestore();
	Class_T 		Classify()					{ return CLASS_PLAYER_ALLY; }
	bool			ShouldAlwaysThink();

	//---------------------------------
	// HGrunt-specific utility functions
	//---------------------------------
	bool			IsMedic()					{ return m_iSquadRole == HGRUNT_MEDIC; }
	bool			IsEngineer()				{ return m_iSquadRole == HGRUNT_ENGINEER; }
	void			TurnSquadHostileToPlayer();
	void			TurnHostileToPlayer();
	void			AddSquadToPlayerSquad();
	bool			SquadHasSpecial(int type);
	int				HGruntRole()				{ return m_iSquadRole; }

	//---------------------------------
	// Healing-related functions
	//---------------------------------
	bool			CanHeal();
	void			Heal( );
	bool			ShouldHealTarget( CBaseEntity *pTarget );
	void			AddHealCharge( int charge );
	void			RemoveHealCharge( int charge );
	bool			IsHealRequestActive();
	bool			ShouldCallMedic();

	//---------------------------------
	// Behavior
	//---------------------------------
	bool			ShouldBehaviorSelectSchedule( CAI_BehaviorBase *pBehavior );
	void 			GatherConditions();
	void			PredictPlayerPush();
	void 			PrescheduleThink();
	void			BuildScheduleTestBits();

	bool			FInViewCone( CBaseEntity *pEntity );

	int				SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode );

	int 			SelectSchedulePriorityAction();
	int 			SelectScheduleHeal();
	int 			SelectScheduleRetrieveItem();
	int 			SelectScheduleNonCombat();
	int 			SelectScheduleCombat();
	bool			ShouldDeferToFollowBehavior();
	int 			TranslateSchedule( int scheduleType );

	bool			ShouldAcceptGoal( CAI_BehaviorBase *pBehavior, CAI_GoalEntity *pGoal );
	void			OnClearGoal( CAI_BehaviorBase *pBehavior, CAI_GoalEntity *pGoal );

	void 			StartTask( const Task_t *pTask );
	void 			RunTask( const Task_t *pTask );

	Activity		NPC_TranslateActivity( Activity eNewActivity );
	void 			HandleAnimEvent( animevent_t *pEvent );
	void			TaskFail( AI_TaskFailureCode_t code );

	void 			PickupItem( CBaseEntity *pItem );
	bool			ShouldLookForHealthItem();

	void 			SimpleUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	virtual void	OnChangeRunningBehavior( CAI_BehaviorBase *pOldBehavior, CAI_BehaviorBase *pNewBehavior );

	//---------------------------------
	// Senses
	//---------------------------------
	bool			QueryHearSound( CSound *pSound );
	int				GetSoundInterests( void );
	//---------------------------------
	// Commander mode
	//---------------------------------
	bool 			IsCommandable();
	bool			CanJoinPlayerSquad();
	bool			WasInPlayerSquad();
	bool			HaveCommandGoal() const;
	bool			IsCommandMoving();
	bool			ShouldAutoSummon();
	bool 			IsValidCommandTarget( CBaseEntity *pTarget );
	bool 			NearCommandGoal();
	bool 			VeryFarFromCommandGoal();
	bool 			TargetOrder( CBaseEntity *pTarget, CAI_BaseNPC **Allies, int numAllies );
	void 			MoveOrder( const Vector &vecDest, CAI_BaseNPC **Allies, int numAllies );
	void			OnMoveOrder();
	void 			CommanderUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	bool			ShouldSpeakRadio( CBaseEntity *pListener );
	void			OnMoveToCommandGoalFailed();
	void			AddToPlayerSquad();
	void			RemoveFromPlayerSquad();
	void 			TogglePlayerSquadState();
	void			UpdatePlayerSquad();
	static int __cdecl PlayerSquadCandidateSortFunc( const SquadCandidateHGrunt_t *, const SquadCandidateHGrunt_t * );
	void 			FixupPlayerSquad();
	void 			ClearFollowTarget();
	void 			UpdateFollowCommandPoint();
	bool			IsFollowingCommandPoint();
	CAI_BaseNPC *	GetSquadCommandRepresentative();
	void			SetSquad( CAI_Squad *pSquad );
	bool			SpeakCommandResponse( AIConcept_t concept, const char *modifiers = NULL );

	//---------------------------------
	// Combat
	//---------------------------------
	bool 			OnBeginMoveAndShoot();
	void 			OnEndMoveAndShoot();

	virtual bool	UseAttackSquadSlots()	{ return false; }

	//---------------------------------
	// Damage handling
	//---------------------------------
	int 			OnTakeDamage_Alive( const CTakeDamageInfo &info );
	virtual bool	PassesDamageFilter( const CTakeDamageInfo &info );

	//---------------------------------
	// Input/output functions
	//---------------------------------
	void			InputSetHealCharge( inputdata_t &inputdata );
	void			InputSetCommandable( inputdata_t &inputdata );
	void			InputRemoveFromPlayerSquad( inputdata_t &inputdata ) { RemoveFromPlayerSquad(); }
	void			InputSetRoleMedic( inputdata_t &inputdata );
	void			InputSetRoleEngineer( inputdata_t &inputdata );
	void			InputSetRoleGeneric( inputdata_t &inputdata );

	COutputEvent		m_OnJoinedPlayerSquad;
	COutputEvent		m_OnLeftPlayerSquad;
	COutputEvent		m_OnFollowOrder;
	COutputEvent		m_OnStationOrder;
	COutputEvent		m_OnPlayerUse;
	COutputEvent		m_OnNavFailBlocked;
	COutputEvent		m_OnHealedEntity;

private:
	enum
	{
		COND_HGRUNT_MEDIC_HEAL_PLAYER = BaseClass::NEXT_CONDITION,
		COND_HGRUNT_NEED_HEALING,
		COND_HGRUNT_COMMANDHEAL,
		NEXT_CONDITION,

		SCHED_HGRUNT_MEDIC_HEAL = BaseClass::NEXT_SCHEDULE,
		SCHED_HGRUNT_ASK_HEAL,
		SCHED_HGRUNT_COVER_HEAL,
		NEXT_SCHEDULE,

		TASK_HGRUNT_MEDIC_HEAL = BaseClass::NEXT_TASK,
		TASK_CALL_MEDIC,
		NEXT_TASK
	};
	int				m_iSquadRole;
	float			m_flLastHealTime;
	int				m_iHealCharge; // how much healing the medic can do before they're unable to heal any more
	CBaseEntity		*m_hLastHealTarget;
	CHandle<CAI_FollowGoal>	m_hSavedFollowGoalEnt;
	float			m_flTimeJoinedPlayerSquad;
	bool			m_bWasInPlayerSquad;
	float			m_flTimeLastCloseToPlayer;
	bool			m_bNeverLeavePlayerSquad; // Don't leave the player squad unless killed, or removed via Entity I/O. 
	CSimpleSimTimer	m_AutoSummonTimer;
	Vector			m_vAutoSummonAnchor;
	string_t		m_iszOriginalSquad;
	int				m_iFriendlyFireCount; // how many times this npc has been friendly fired
	float			m_flLastFriendlyFireTime;
	bool			m_bRemovedFromPlayerSquad; // if this npc was intentionally removed from the player squad (via +use)
	float			m_flLastHealCallTime; // when we last called for a medic
	float			m_flNextHealthSearchTime;
	bool			m_bAwaitingMedic; // whether a medic has responded to our call
	bool			m_bCommanded; // whether the player has explicitly asked us to do something
	bool			m_bNotifyNavFailBlocked;
	bool			m_bHealed; // only used to notify an hgrunt if they got a heal; this is not saved

	static CSimpleSimTimer gm_PlayerSquadEvaluateTimer;
	DEFINE_CUSTOM_AI;
};

//---------------------------------------------------------
//---------------------------------------------------------
inline bool CNPC_HGrunt::NearCommandGoal()
{
	const float flDistSqr = COMMAND_GOAL_TOLERANCE * COMMAND_GOAL_TOLERANCE;
	return ((GetAbsOrigin() - GetCommandGoal()).LengthSqr() <= flDistSqr);
}

//---------------------------------------------------------
//---------------------------------------------------------
inline bool CNPC_HGrunt::VeryFarFromCommandGoal()
{
	const float flDistSqr = (12 * 50) * (12 * 50);
	return ((GetAbsOrigin() - GetCommandGoal()).LengthSqr() > flDistSqr);
}
