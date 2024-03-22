//============== Copyleft arbabf, all rights not reserved.================
//
// Purpose: Human grunts, who shoot bullets, heal things, and torch
// doors.
//
//========================================================================

#include "npc_playercompanion.h"
//#include "ai_squad.h"

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

// todo: either redo CAI_HGruntSquad or remove all references to it

/*
class CNPC_HGrunt;

class CAI_HGruntSquad : public CAI_Squad
{
	DECLARE_CLASS( CAI_HGruntSquad, CAI_Squad );
	DECLARE_DATADESC();
public:
	~CAI_HGruntSquad();

	bool	SquadHasMedic( void ) { return m_Medics.Count() > 0; }
	bool	SquadHasEngineer( void ) { return m_Engineers.Count() > 0; }
	void	AddSpecialGrunt( CNPC_HGrunt *pHGrunt );
	void	RemoveSpecialGrunt( CNPC_HGrunt *pHGrunt );

	CUtlVectorFixed<CHandle<CNPC_HGrunt>, MAX_SQUAD_MEMBERS> m_Medics;
	CUtlVectorFixed<CHandle<CNPC_HGrunt>, MAX_SQUAD_MEMBERS> m_Engineers;
private:
};
*/

class CNPC_HGrunt : public CNPC_PlayerCompanion
{
	DECLARE_CLASS( CNPC_HGrunt, CNPC_PlayerCompanion );
	DECLARE_DATADESC();

public:
	virtual void	Spawn();

	//---------------------------------
	// HGrunt-specific utility functions
	//---------------------------------
	bool			IsMedic()					{ return m_iSquadRole == HGRUNT_MEDIC; }
	bool			IsEngineer()				{ return m_iSquadRole == HGRUNT_ENGINEER; }
	Class_T 		Classify()					{ return CLASS_PLAYER_ALLY; }
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
	int				SelectSchedule();

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

	bool			IgnorePlayerPushing( void );

	// todo: this
	//int				DrawDebugTextOverlays( void );

	virtual const char *SelectRandomExpressionForState( NPC_STATE state );
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
	bool			IsPlayerAlly( CBasePlayer *pPlayer = NULL );
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
	void			SetSquad( CAI_Squad/*CAI_HGruntSquad*/ *pSquad );
	bool			SpeakCommandResponse( AIConcept_t concept, const char *modifiers = NULL );

	//CAI_HGruntSquad *	GetHGruntSquad( void ) { return assert_cast<CAI_HGruntSquad *>(GetSquad()); }
	//void				AddToSquad(string_t name);
	//void				RemoveFromSquad();

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

	// todo: input/output funcs

private:
	enum
	{
		COND_HGRUNT_MEDIC_HEAL_PLAYER = BaseClass::NEXT_CONDITION,
		COND_HGRUNT_NEED_HEALING,
		COND_HGRUNT_MEDIC_READY_TO_HEAL,
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

	static CSimpleSimTimer gm_PlayerSquadEvaluateTimer;
	DEFINE_CUSTOM_AI;
};